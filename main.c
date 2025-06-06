#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>
#include <stdarg.h>
#include <syslog.h>
#include <sys/resource.h>
#include <errno.h>
#include "control_device.h"
#include "web_server.h"

#define PORT 8080
#define PID_FILE "/var/run/iot_server.pid"
#define MAX_LIBS 4

// 전역 변수
static int server_fd = -1, running = 1, daemon_mode = 0;
device_functions_t device_funcs = {0};

// 라이브러리 정보 구조체
typedef struct {
    const char* name;
    const char* filename;
    void** handle;
    const char* symbols[12];
    void** functions[12];
} lib_info_t;

// 라이브러리 정보 배열
static void *led_lib, *segment_lib, *buzzer_lib, *cds_lib;
static lib_info_t libs[MAX_LIBS] = {
    {"LED", "./libled.so", &led_lib, 
     {"led_init", "led_on", "led_off", "led_brightness", "led_cleanup", "led_get_status", NULL},
     {(void**)&device_funcs.led.init, (void**)&device_funcs.led.on, (void**)&device_funcs.led.off, 
      (void**)&device_funcs.led.brightness, (void**)&device_funcs.led.cleanup, (void**)&device_funcs.led.get_status}},
    
    {"SEGMENT", "./libsegment.so", &segment_lib,
     {"fnd_init", "fnd_display", "fnd_countdown", "fnd_stop", "fnd_off", "fnd_cleanup", "fnd_get_status", NULL},
     {(void**)&device_funcs.segment.init, (void**)&device_funcs.segment.display, (void**)&device_funcs.segment.countdown,
      (void**)&device_funcs.segment.stop, (void**)&device_funcs.segment.off, (void**)&device_funcs.segment.cleanup, (void**)&device_funcs.segment.get_status}},
    
    {"BUZZER", "./libbuzzer.so", &buzzer_lib,
     {"buzzer_init", "buzzer_play", "buzzer_stop", "buzzer_cleanup", "buzzer_get_status", NULL},
     {(void**)&device_funcs.buzzer.init, (void**)&device_funcs.buzzer.play, (void**)&device_funcs.buzzer.stop,
      (void**)&device_funcs.buzzer.cleanup, (void**)&device_funcs.buzzer.get_status}},
    
    {"CDS", "./libcds.so", &cds_lib,
     {"cds_init", "cds_read", "cds_get_value", "cds_is_bright", "cds_auto_led_start", "cds_auto_led_stop", 
      "auto_led_manual_on", "auto_led_manual_off", "cds_cleanup", "cds_get_status", NULL},
     {(void**)&device_funcs.cds.init, (void**)&device_funcs.cds.read, (void**)&device_funcs.cds.get_value,
      (void**)&device_funcs.cds.is_bright, (void**)&device_funcs.cds.auto_led_start, (void**)&device_funcs.cds.auto_led_stop,
      (void**)&device_funcs.cds.manual_on, (void**)&device_funcs.cds.manual_off, (void**)&device_funcs.cds.cleanup, (void**)&device_funcs.cds.get_status}}
};

// 명령어 처리 구조체
typedef struct {
    const char* cmd;
    int (*handler)(const char* cmd, char* response, int size);
} cmd_handler_t;

// 로그 함수
void write_log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    if (daemon_mode) {
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        syslog(LOG_INFO, "%s", buffer);
    } else {
        if (isatty(STDOUT_FILENO)) {
            vprintf(format, args);
            printf("\n");
            fflush(stdout);
        }
    }
    va_end(args);
}

// PID 파일 처리
int create_pid_file(void) {
    FILE* fp = fopen(PID_FILE, "w");
    if (!fp) return -1;
    fprintf(fp, "%d\n", getpid());
    fclose(fp);
    return 0;
}

void remove_pid_file(void) { unlink(PID_FILE); }

// 데몬화 함수 (기존과 동일)
int daemonize(const char* cmd) {
    struct sigaction sa;
    struct rlimit rl;
    int fd0, fd1, fd2, i;
    pid_t pid;

    umask(0);
    if(getrlimit(RLIMIT_NOFILE, &rl) < 0) return -1;
    if((pid = fork()) < 0) return -1;
    else if(pid != 0) exit(0);
    if (setsid() < 0) return -1;
    
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if(sigaction(SIGHUP, &sa, NULL) < 0) return -1;
    if((pid = fork()) < 0) return -1;
    else if(pid != 0) exit(0);

    char current_dir[1024];
    if (getcwd(current_dir, sizeof(current_dir)) == NULL) return -1;
    
    if(rl.rlim_max == RLIM_INFINITY) rl.rlim_max = 1024;
    for(i = 0; i < rl.rlim_max; i++) close(i);

    fd0 = open("/dev/null", O_RDWR);
    fd1 = dup(0);
    fd2 = dup(0);
    openlog(cmd, LOG_CONS, LOG_DAEMON);
    if(fd0 != 0 || fd1 != 1 || fd2 != 2) return -1;
    if(chdir(current_dir) < 0) return -1;

    daemon_mode = 1;
    if (create_pid_file() < 0) return -1;
    syslog(LOG_INFO, "IoT 서버 데몬 프로세스 시작 (PID: %d)", getpid());
    return 0;
}

// 라이브러리 로딩
int load_device_libraries(void) {
    for (int i = 0; i < MAX_LIBS; i++) {
        *(libs[i].handle) = dlopen(libs[i].filename, RTLD_LAZY);
        if (!*(libs[i].handle)) {
            write_log("%s 라이브러리 로딩 실패: %s", libs[i].name, dlerror());
            return -1;
        }
        for (int j = 0; libs[i].symbols[j]; j++) {
            *(libs[i].functions[j]) = dlsym(*(libs[i].handle), libs[i].symbols[j]);
        }
    }
    return 0;
}

void unload_device_libraries(void) {
    for (int i = 0; i < MAX_LIBS; i++) {
        if (*(libs[i].handle)) dlclose(*(libs[i].handle));
    }
}

// 시그널 핸들러
void signal_handler(int sig) {
    write_log("종료 신호 수신 (%d)", sig);
    running = 0;
    if (server_fd != -1) { shutdown(server_fd, SHUT_RDWR); close(server_fd); }
    
    alarm(2);
    if (device_funcs.led.cleanup) device_funcs.led.cleanup();
    if (device_funcs.segment.cleanup) device_funcs.segment.cleanup();
    if (device_funcs.buzzer.cleanup) device_funcs.buzzer.cleanup();
    if (device_funcs.cds.cleanup) device_funcs.cds.cleanup();
    alarm(0);
    
    unload_device_libraries();
    if (daemon_mode) { remove_pid_file(); closelog(); }
    _exit(0);
}

// 명령어 핸들러 함수들 (기존과 동일)
int handle_led_on(const char* cmd, char* resp, int size) {
    return snprintf(resp, size, device_funcs.led.on && device_funcs.led.on() == 0 ? 
                   "OK: LED 켜짐" : "ERROR: LED 켜기 실패");
}

int handle_led_off(const char* cmd, char* resp, int size) {
    return snprintf(resp, size, device_funcs.led.off && device_funcs.led.off() == 0 ? 
                   "OK: LED 꺼짐" : "ERROR: LED 끄기 실패");
}

int handle_led_brightness(const char* cmd, char* resp, int size) {
    int level;
    if (sscanf(cmd, "LED_BRIGHTNESS %d", &level) == 1) {
        return snprintf(resp, size, device_funcs.led.brightness && device_funcs.led.brightness(level) == 0 ?
                       "OK: LED 밝기 %d로 설정" : "ERROR: LED 밝기 설정 실패", level);
    }
    return snprintf(resp, size, "ERROR: LED_BRIGHTNESS <0-2> 형식으로 입력");
}

int handle_segment_display(const char* cmd, char* resp, int size) {
    int num;
    if (sscanf(cmd, "SEGMENT_DISPLAY %d", &num) == 1) {
        return snprintf(resp, size, device_funcs.segment.display && device_funcs.segment.display(num) == 0 ?
                       "OK: SEGMENT에 %d 표시" : "ERROR: SEGMENT 표시 실패", num);
    }
    return snprintf(resp, size, "ERROR: SEGMENT_DISPLAY <0-9> 형식으로 입력");
}

int handle_segment_countdown(const char* cmd, char* resp, int size) {
    int start;
    if (sscanf(cmd, "SEGMENT_COUNTDOWN %d", &start) == 1) {
        return snprintf(resp, size, device_funcs.segment.countdown && device_funcs.segment.countdown(start) == 0 ?
                       "OK: SEGMENT 카운트다운 %d부터 시작" : "ERROR: SEGMENT 카운트다운 실패", start);
    }
    return snprintf(resp, size, "ERROR: SEGMENT_COUNTDOWN <1-9> 형식으로 입력");
}

int handle_all_off(const char* cmd, char* resp, int size) {
    if (device_funcs.led.off) device_funcs.led.off();
    if (device_funcs.segment.off) device_funcs.segment.off();
    if (device_funcs.buzzer.stop) device_funcs.buzzer.stop();
    if (device_funcs.cds.auto_led_stop) device_funcs.cds.auto_led_stop();
    if (device_funcs.cds.manual_off) device_funcs.cds.manual_off();
    return snprintf(resp, size, "OK: 모든 디바이스 꺼짐");
}

int handle_help(const char* cmd, char* resp, int size) {
    return snprintf(resp, size, "LED: LED_ON, LED_OFF, LED_BRIGHTNESS [0-2]\n"
                               "SEGMENT: SEGMENT_DISPLAY [0-9], SEGMENT_COUNTDOWN [1-9], SEGMENT_STOP, SEGMENT_OFF\n"
                               "BUZZER: BUZZER_PLAY, BUZZER_STOP\n"
                               "CDS: CDS_READ, CDS_AUTO_START, CDS_AUTO_STOP, CDS_GET_STATUS\n"
                               "기타: ALL_OFF, HELP, QUIT");
}

int handle_quit(const char* cmd, char* resp, int size) {
    running = 0;
    return snprintf(resp, size, "OK: 서버 종료");
}

// CDS 관련 핸들러
int handle_cds_auto_start(const char* cmd, char* resp, int size) {
    return snprintf(resp, size, device_funcs.cds.auto_led_start && device_funcs.cds.auto_led_start() == 0 ?
                   "OK: 조도 센서 자동 LED 제어 시작" : "ERROR: 조도 센서 자동 제어 시작 실패");
}

int handle_cds_auto_stop(const char* cmd, char* resp, int size) {
    return snprintf(resp, size, device_funcs.cds.auto_led_stop && device_funcs.cds.auto_led_stop() == 0 ?
                   "OK: 조도 센서 자동 LED 제어 중지" : "ERROR: 조도 센서 자동 제어 중지 실패");
}

int handle_cds_read(const char* cmd, char* resp, int size) {
    if (device_funcs.cds.read && device_funcs.cds.read() == 0) {
        int value = device_funcs.cds.get_value ? device_funcs.cds.get_value() : -1;
        int bright = device_funcs.cds.is_bright ? device_funcs.cds.is_bright() : -1;
        return snprintf(resp, size, "OK: 조도값 %d (%s)", value, bright ? "밝음" : "어둠");
    } else {
        return snprintf(resp, size, "ERROR: 조도 센서 읽기 실패");
    }
}

int handle_cds_get_status(const char* cmd, char* resp, int size) {
    char status_buf[256];
    if (device_funcs.cds.get_status && device_funcs.cds.get_status(status_buf, sizeof(status_buf)) == 0) {
        return snprintf(resp, size, "OK: %s", status_buf);
    } else {
        return snprintf(resp, size, "ERROR: 조도 센서 상태 확인 실패");
    }
}

// 명령어 핸들러 테이블
cmd_handler_t cmd_handlers[] = {
    {"LED_ON", handle_led_on},
    {"LED_OFF", handle_led_off}, 
    {"LED_BRIGHTNESS", handle_led_brightness},
    {"SEGMENT_DISPLAY", handle_segment_display},
    {"SEGMENT_COUNTDOWN", handle_segment_countdown},
    {"CDS_AUTO_START", handle_cds_auto_start},
    {"CDS_AUTO_STOP", handle_cds_auto_stop},
    {"CDS_READ", handle_cds_read},
    {"CDS_GET_STATUS", handle_cds_get_status},
    {"ALL_OFF", handle_all_off},
    {"HELP", handle_help},
    {"QUIT", handle_quit},
    {NULL, NULL}
};

// 명령어 처리
int process_command(const char* command, char* response, int response_size) {
    for (int i = 0; cmd_handlers[i].cmd; i++) {
        if (strncmp(command, cmd_handlers[i].cmd, strlen(cmd_handlers[i].cmd)) == 0) {
            cmd_handlers[i].handler(command, response, response_size);
            return strcmp(cmd_handlers[i].cmd, "QUIT") == 0 ? -1 : 0;
        }
    }
    
    if (strcmp(command, "SEGMENT_STOP") == 0) {
        snprintf(response, response_size, device_funcs.segment.stop && device_funcs.segment.stop() == 0 ?
                "OK: SEGMENT 카운트다운 중지" : "ERROR: SEGMENT 중지 실패");
    } else if (strcmp(command, "SEGMENT_OFF") == 0) {
        if (device_funcs.segment.off) device_funcs.segment.off();
        snprintf(response, response_size, "OK: SEGMENT 꺼짐");
    } else if (strcmp(command, "BUZZER_PLAY") == 0) {
        snprintf(response, response_size, device_funcs.buzzer.play && device_funcs.buzzer.play() == 0 ?
                "OK: 부저 재생 시작" : "ERROR: 부저 재생 실패");
    } else if (strcmp(command, "BUZZER_STOP") == 0) {
        snprintf(response, response_size, device_funcs.buzzer.stop && device_funcs.buzzer.stop() == 0 ?
                "OK: 부저 중지" : "ERROR: 부저 중지 실패");
    } else {
        snprintf(response, response_size, "ERROR: 알 수 없는 명령어 '%s'", command);
    }
    return 0;
}

// 클라이언트 처리 (간소화된 버전)
void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE] = {0};
    char response[MAX_RESPONSE_SIZE] = {0};

    int bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        close(client_fd);
        return;
    }
    
    buffer[bytes_received] = '\0';
    
    // HTTP 요청 처리
    if (is_http_request(buffer)) {
        write_log("HTTP 클라이언트 연결됨 (fd: %d)", client_fd);
        handle_http_request(client_fd, buffer);
        close(client_fd);
        return;
    }
    
    // TCP 소켓 처리
    send(client_fd, "연결완료\n", 9, 0);
    write_log("TCP 클라이언트 연결됨 (fd: %d)", client_fd);

    buffer[strcspn(buffer, "\r\n")] = '\0';
    if (strlen(buffer) > 0) {
        write_log("명령어 수신: %s", buffer);
        if (process_command(buffer, response, sizeof(response)) == -1) {
            close(client_fd);
            return;
        }
        strcat(response, "\n");
        send(client_fd, response, strlen(response), 0);
    }

    while (running) {
        memset(buffer, 0, BUFFER_SIZE);
        memset(response, 0, MAX_RESPONSE_SIZE);

        bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes_received <= 0) break;

        buffer[strcspn(buffer, "\r\n")] = '\0';
        if (strlen(buffer) == 0) continue;

        write_log("명령어 수신: %s", buffer);
        if (process_command(buffer, response, sizeof(response)) == -1) break;
        
        strcat(response, "\n");
        send(client_fd, response, strlen(response), 0);
    }

    write_log("TCP 클라이언트 연결 종료 (fd: %d)", client_fd);
    close(client_fd);
}

int main(int argc, char *argv[]) {
    if(argc < 2) {
        printf("Usage: %s [-d|-h]\n  -d: 데몬 모드\n  -h: 도움말\n", argv[0]);
        return -1;
    }

    if (strcmp(argv[1], "-d") == 0) {
        if (daemonize(argv[0]) < 0) { fprintf(stderr, "데몬화 실패\n"); return -1; }
    } else if (strcmp(argv[1], "-h") == 0) {
        printf("사용법: %s [-d|-h]\n  -d: 데몬 모드\n  -h: 도움말\n", argv[0]);
        return 0;
    } else {
        printf("잘못된 옵션: %s\n", argv[1]);
        return -1;
    }

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    write_log("IoT 서버 시작 중...");
    if (load_device_libraries() < 0) { write_log("동적 라이브러리 로딩 실패"); return -1; }

    // 디바이스 초기화
    if (device_funcs.led.init) device_funcs.led.init();
    if (device_funcs.segment.init) device_funcs.segment.init();
    if (device_funcs.buzzer.init) device_funcs.buzzer.init();
    if (device_funcs.cds.init) device_funcs.cds.init();

    // 소켓 설정
    struct sockaddr_in address;
    int opt = 1, addrlen = sizeof(address);
    
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) { 
        write_log("소켓 생성 실패: %s", strerror(errno)); 
        return -1; 
    }
    
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) { 
        write_log("소켓 옵션 설정 실패: %s", strerror(errno)); 
        close(server_fd);
        return -1; 
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) { 
        write_log("바인드 실패: %s (포트 %d가 이미 사용 중일 수 있음)", strerror(errno), PORT); 
        close(server_fd);
        return -1; 
    }
    
    if (listen(server_fd, 3) < 0) { 
        write_log("리스닝 실패: %s", strerror(errno)); 
        close(server_fd);
        return -1; 
    }

    write_log("IoT 서버 시작 완료 - 포트 %d", PORT);

    // 메인 루프
    write_log("메인 루프 시작 - 클라이언트 연결 대기 중...");
    while (running) {
        fd_set readfds;
        struct timeval timeout;
        
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int activity = select(server_fd + 1, &readfds, NULL, NULL, &timeout);
        
        if (activity < 0 && errno != EINTR) {
            write_log("select() 오류: %s", strerror(errno));
            break;
        }
        
        if (activity > 0 && FD_ISSET(server_fd, &readfds)) {
            int client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
            if (client_fd < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    write_log("accept() 실패: %s", strerror(errno));
                }
                continue;
            }
            write_log("새 클라이언트 연결 수락 (fd: %d)", client_fd);
            handle_client(client_fd);
        }
        
        if (!running) {
            write_log("종료 신호 감지, 메인 루프 종료");
            break;
        }
    }

    // 정리
    write_log("서버 종료 중...");
    if (device_funcs.led.cleanup) device_funcs.led.cleanup();
    if (device_funcs.segment.cleanup) device_funcs.segment.cleanup();
    if (device_funcs.buzzer.cleanup) device_funcs.buzzer.cleanup();
    if (device_funcs.cds.cleanup) device_funcs.cds.cleanup();
    unload_device_libraries();
    if (server_fd != -1) close(server_fd);
    if (daemon_mode) { remove_pid_file(); closelog(); }
    return 0;
}
