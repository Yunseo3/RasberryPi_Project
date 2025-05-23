#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
#define DEFAULT_PORT 8080

static volatile int running = 1;

// 시그널 핸들러 (SIGINT만 처리)
void signal_handler(int sig) {
    if (sig == SIGINT) {
        printf("\n종료 신호 수신. 클라이언트를 종료합니다.\n");
        running = 0;
    }
}

// 시그널 설정
void setup_signals(void) {
    // SIGINT만 처리, 나머지는 무시
    signal(SIGINT, signal_handler);
    signal(SIGTERM, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
}

// 사용법 출력
void print_usage(char* program_name) {
    printf("사용법: %s <서버IP> [포트]\n", program_name);
    printf("예시: %s 192.168.0.84\n", program_name);
    printf("     %s 192.168.0.84 8080\n", program_name);
}

// 대화형 모드
void interactive_mode(char* server_ip, int port) {
    printf("명령어 입력 (HELP: 도움말, Ctrl+C: 종료)\n");
    
    char command[BUFFER_SIZE];
    
    while (running) {
        printf("> ");
        fflush(stdout);
        
        if (fgets(command, sizeof(command), stdin) == NULL) break;
        
        command[strcspn(command, "\n")] = 0;
        if (strlen(command) == 0) continue;
        
        if (strcmp(command, "quit") == 0 || strcmp(command, "q") == 0) {
            printf("종료\n");
            break;
        }
        
        // 서버에 명령 전송
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            printf("소켓 생성 실패\n");
            continue;
        }
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        
        if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
            printf("잘못된 IP\n");
            close(sockfd);
            continue;
        }
        
        if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            printf("연결 실패\n");
            close(sockfd);
            continue;
        }
        
        if (send(sockfd, command, strlen(command), 0) < 0) {
            printf("전송 실패\n");
            close(sockfd);
            continue;
        }
        
        char buffer[BUFFER_SIZE] = {0};
        int bytes_received = read(sockfd, buffer, BUFFER_SIZE - 1);
        if (bytes_received > 0) {
            printf("%s", buffer);
        }
        
        close(sockfd);
    }
}

int main(int argc, char *argv[]) {
    char* server_ip;
    int port = DEFAULT_PORT;
    
    // 시그널 설정
    setup_signals();
    
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    
    server_ip = argv[1];
    
    if (argc >= 3) {
        port = atoi(argv[2]);
        if (port <= 0 || port > 65535) {
            printf("잘못된 포트 번호\n");
            return 1;
        }
    }
    
    interactive_mode(server_ip, port);
    return 0;
}
