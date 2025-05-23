#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <wiringPi.h>
#include <signal.h>
#include <dlfcn.h>
#include <sys/time.h>
#include "control_device.h"

#define FND_PINS_COUNT 4
static int fnd_pins[FND_PINS_COUNT] = {16, 20, 21, 12};
static int number_patterns[10][4] = {
    {0,0,0,0}, {0,0,0,1}, {0,0,1,0}, {0,0,1,1}, {0,1,0,0},
    {0,1,0,1}, {0,1,1,0}, {0,1,1,1}, {1,0,0,0}, {1,0,0,1}
};

// FND 상태 관리
static device_state_t fnd_state = {0, PTHREAD_MUTEX_INITIALIZER};
static int display_time = 1;
static pthread_t auto_off_tid = 0;
static pthread_t countdown_tid = 0;
static volatile int is_counting = 0;
static volatile int running = 1;
static volatile int countdown_stop_requested = 0;

// 부저 함수 포인터 (동적 로딩용)
static int (*buzzer_play_func)(void) = NULL;
static int (*buzzer_stop_func)(void) = NULL;
static void* buzzer_lib = NULL;

// 부저 라이브러리 로딩
static void load_buzzer_functions() {
    if (buzzer_lib == NULL) {
        buzzer_lib = dlopen("./libbuzzer.so", RTLD_LAZY);
        if (buzzer_lib) {
            buzzer_play_func = dlsym(buzzer_lib, "buzzer_play");
            buzzer_stop_func = dlsym(buzzer_lib, "buzzer_stop");
            printf("[FND] 부저 라이브러리 로딩 성공\n");
        } else {
            printf("[FND] 부저 라이브러리 로딩 실패: %s\n", dlerror());
        }
    }
}

// 자동 꺼짐 스레드
void *auto_off_thread(void *arg) {
    int seconds = *((int *)arg);
    free(arg);
    
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);

    sleep(seconds);
    
    pthread_mutex_lock(&fnd_state.mutex);
    if (fnd_state.is_initialized) {
        for (int i = 0; i < FND_PINS_COUNT; i++) {
            digitalWrite(fnd_pins[i], HIGH);
        }
        printf("[FND] 자동 꺼짐\n");
    }
    pthread_mutex_unlock(&fnd_state.mutex);
    
    auto_off_tid = 0;
    return NULL;
}

// 카운트다운 스레드 (수정된 버전)
void *countdown_thread(void *arg) {
    int start_num = *((int *)arg);
    free(arg);
    
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    
    pthread_mutex_lock(&fnd_state.mutex);
    is_counting = 1;
    countdown_stop_requested = 0;
    pthread_mutex_unlock(&fnd_state.mutex);

    printf("[FND] 카운트다운 스레드 시작: %d\n", start_num);

    for (int i = start_num; i >= 0; i--) {
        // 중지 요청 확인
        pthread_mutex_lock(&fnd_state.mutex);
        int should_stop = countdown_stop_requested || !running;
        pthread_mutex_unlock(&fnd_state.mutex);
        
        if (should_stop) {
            printf("[FND] 카운트다운 중지 요청 감지\n");
            break;
        }
        
        // 숫자 표시
        pthread_mutex_lock(&fnd_state.mutex);
        if (fnd_state.is_initialized) {
            for (int j = 0; j < FND_PINS_COUNT; j++) {
                digitalWrite(fnd_pins[j], number_patterns[i][j] ? HIGH : LOW);
            }
            printf("[FND] 카운트다운: %d\n", i);
        }
        pthread_mutex_unlock(&fnd_state.mutex);

        if (i > 0) {
            // 1초 대기 (취소 포인트)
            for (int wait = 0; wait < 10; wait++) {
                delay(100); // 0.1초씩 대기 (wiringPi의 delay 함수 사용)
                pthread_testcancel();
                
                pthread_mutex_lock(&fnd_state.mutex);
                int should_stop = countdown_stop_requested || !running;
                pthread_mutex_unlock(&fnd_state.mutex);
                
                if (should_stop) break;
            }
        } else {
            // 0이 되면 부저 울림
            printf("[FND] 카운트다운 완료! 부저 울림\n");
            
            // 부저 라이브러리 로딩 시도
            if (!buzzer_play_func) {
                load_buzzer_functions();
            }
            
            // 부저 함수 호출
            if (buzzer_play_func) {
                printf("[FND] 부저 재생 시작\n");
                buzzer_play_func();
                
                // 3초간 부저 재생
                for (int wait = 0; wait < 30; wait++) { // 3초 = 30 * 0.1초
                    delay(100); // 0.1초씩 대기 (wiringPi의 delay 함수 사용)
                    pthread_testcancel();
                    
                    pthread_mutex_lock(&fnd_state.mutex);
                    int should_stop = countdown_stop_requested || !running;
                    pthread_mutex_unlock(&fnd_state.mutex);
                    
                    if (should_stop) break;
                }
                
                if (buzzer_stop_func) {
                    printf("[FND] 부저 재생 중지\n");
                    buzzer_stop_func();
                }
            } else {
                printf("[FND] 부저 함수를 찾을 수 없음 - 대신 비ープ음 출력\n");
                // 대안: 시스템 비프음
                system("echo -e '\\a'");
            }
            
            // FND 끄기
            pthread_mutex_lock(&fnd_state.mutex);
            if (fnd_state.is_initialized) {
                for (int j = 0; j < FND_PINS_COUNT; j++) {
                    digitalWrite(fnd_pins[j], HIGH);
                }
                printf("[FND] 카운트다운 완료 후 꺼짐\n");
            }
            pthread_mutex_unlock(&fnd_state.mutex);
        }
    }

    // 스레드 종료 처리
    pthread_mutex_lock(&fnd_state.mutex);
    is_counting = 0;
    countdown_stop_requested = 0;
    countdown_tid = 0;
    pthread_mutex_unlock(&fnd_state.mutex);
    
    printf("[FND] 카운트다운 스레드 종료\n");
    return NULL;
}

// FND 초기화
int fnd_init(void) {
    pthread_mutex_lock(&fnd_state.mutex);

    if (fnd_state.is_initialized) {
        pthread_mutex_unlock(&fnd_state.mutex);
        return 0;
    }

    if (wiringPiSetupGpio() == -1) {
        fprintf(stderr, "[FND] wiringPiSetupGpio 초기화 실패\n");
        pthread_mutex_unlock(&fnd_state.mutex);
        return -1;
    }

    // FND 핀 초기화
    for (int i = 0; i < FND_PINS_COUNT; i++) {
        pinMode(fnd_pins[i], OUTPUT);
        digitalWrite(fnd_pins[i], HIGH);  // 초기에는 꺼진 상태
    }

    fnd_state.is_initialized = 1;
    running = 1;
    
    // 부저 라이브러리 미리 로딩
    load_buzzer_functions();
    
    pthread_mutex_unlock(&fnd_state.mutex);
    printf("[FND] 초기화 완료\n");
    return 0;
}

// FND에 숫자 표시
int fnd_display(int num) {
    pthread_mutex_lock(&fnd_state.mutex);

    if (!fnd_state.is_initialized && fnd_init() < 0) {
        pthread_mutex_unlock(&fnd_state.mutex);
        return -1;
    }

    if (num < 0 || num > 9) {
        pthread_mutex_unlock(&fnd_state.mutex);
        return -1;
    }

    // 진행 중인 카운트다운이 있으면 중지
    if (is_counting && countdown_tid != 0) {
        countdown_stop_requested = 1;
        pthread_mutex_unlock(&fnd_state.mutex);
        
        // 카운트다운 스레드 종료 대기
        printf("[FND] 기존 카운트다운 중지 중...\n");
        pthread_join(countdown_tid, NULL);
        
        pthread_mutex_lock(&fnd_state.mutex);
        is_counting = 0;
        countdown_stop_requested = 0;
        countdown_tid = 0;
    }

    for (int i = 0; i < FND_PINS_COUNT; i++) {
        digitalWrite(fnd_pins[i], number_patterns[num][i] ? HIGH : LOW);
    }

    printf("[FND] 숫자 %d 표시\n", num);
    pthread_mutex_unlock(&fnd_state.mutex);
    return 0;
}

// FND 끄기
void fnd_off(void) {
    pthread_mutex_lock(&fnd_state.mutex);

    // 진행 중인 카운트다운이 있으면 중지
    if (is_counting && countdown_tid != 0) {
        countdown_stop_requested = 1;
        pthread_mutex_unlock(&fnd_state.mutex);
        
        printf("[FND] 카운트다운 중지 중...\n");
        pthread_join(countdown_tid, NULL);
        
        pthread_mutex_lock(&fnd_state.mutex);
        is_counting = 0;
        countdown_stop_requested = 0;
        countdown_tid = 0;
    }

    if (fnd_state.is_initialized) {
        for (int i = 0; i < FND_PINS_COUNT; i++) {
            digitalWrite(fnd_pins[i], HIGH);
        }
        printf("[FND] 꺼짐\n");
    }

    pthread_mutex_unlock(&fnd_state.mutex);
}

// 카운트다운 시작 (수정된 버전)
int fnd_countdown(int start_num) {
    if (start_num < 1 || start_num > 9) {
        printf("[FND] 잘못된 카운트다운 값: %d (1-9 범위여야 함)\n", start_num);
        return -1;
    }

    pthread_mutex_lock(&fnd_state.mutex);

    if (!fnd_state.is_initialized && fnd_init() < 0) {
        pthread_mutex_unlock(&fnd_state.mutex);
        return -1;
    }

    // 기존 스레드들 정리
    if (auto_off_tid != 0) {
        pthread_cancel(auto_off_tid);
        auto_off_tid = 0;
    }

    // 이미 카운트다운 중이면 중지
    if (is_counting && countdown_tid != 0) {
        countdown_stop_requested = 1;
        pthread_mutex_unlock(&fnd_state.mutex);
        
        printf("[FND] 기존 카운트다운 중지 중...\n");
        pthread_join(countdown_tid, NULL);
        
        pthread_mutex_lock(&fnd_state.mutex);
        is_counting = 0;
        countdown_stop_requested = 0;
        countdown_tid = 0;
    }

    // 카운트다운 스레드 시작
    int *start_ptr = malloc(sizeof(int));
    if (!start_ptr) {
        printf("[FND] 메모리 할당 실패\n");
        pthread_mutex_unlock(&fnd_state.mutex);
        return -1;
    }
    *start_ptr = start_num;

    int result = pthread_create(&countdown_tid, NULL, countdown_thread, start_ptr);
    if (result != 0) {
        printf("[FND] 카운트다운 스레드 생성 실패: %d\n", result);
        free(start_ptr);
        pthread_mutex_unlock(&fnd_state.mutex);
        return -1;
    }

    // detach 사용으로 자동 정리
    pthread_detach(countdown_tid);

    printf("[FND] 카운트다운 시작: %d\n", start_num);
    pthread_mutex_unlock(&fnd_state.mutex);
    return 0;
}

// 카운트다운 중지
int fnd_stop(void) {
    pthread_mutex_lock(&fnd_state.mutex);

    if (is_counting && countdown_tid != 0) {
        countdown_stop_requested = 1;
        pthread_mutex_unlock(&fnd_state.mutex);
        
        printf("[FND] 카운트다운 중지 요청\n");
        
        // detach된 스레드는 join할 수 없으므로 플래그로만 제어
        // 스레드가 자연스럽게 종료되도록 대기
        sleep(1);
        
        pthread_mutex_lock(&fnd_state.mutex);
        is_counting = 0;
        countdown_stop_requested = 0;
        countdown_tid = 0;
        
        // FND 끄기
        if (fnd_state.is_initialized) {
            for (int i = 0; i < FND_PINS_COUNT; i++) {
                digitalWrite(fnd_pins[i], HIGH);
            }
        }
        
        printf("[FND] 카운트다운 중지 완료\n");
    }

    pthread_mutex_unlock(&fnd_state.mutex);
    return 0;
}

// 표시 시간 설정
int fnd_set_display_time(int seconds) {
    if (seconds < 1 || seconds > 3600) {
        return -1;
    }

    display_time = seconds;
    printf("[FND] 표시 시간을 %d초로 설정\n", seconds);
    return 0;
}

// FND 상태 확인
int fnd_get_status(char* status_buf, int buf_size) {
    pthread_mutex_lock(&fnd_state.mutex);

    if (!fnd_state.is_initialized) {
        snprintf(status_buf, buf_size, "FND: NOT_INITIALIZED");
    } else if (is_counting) {
        snprintf(status_buf, buf_size, "FND: COUNTING");
    } else {
        snprintf(status_buf, buf_size, "FND: IDLE");
    }

    pthread_mutex_unlock(&fnd_state.mutex);
    return 0;
}

// FND 자원 해제
void fnd_cleanup(void) {
    pthread_mutex_lock(&fnd_state.mutex);

    running = 0;
    countdown_stop_requested = 1;

    pthread_mutex_unlock(&fnd_state.mutex);

    // detach된 스레드들은 자연스럽게 종료되도록 대기
    if (countdown_tid != 0 || is_counting) {
        printf("[FND] 카운트다운 스레드 종료 대기 중...\n");
        sleep(2); // 스레드가 종료될 시간을 줌
    }

    if (auto_off_tid != 0) {
        pthread_cancel(auto_off_tid);
        auto_off_tid = 0;
    }

    pthread_mutex_lock(&fnd_state.mutex);
    
    if (fnd_state.is_initialized) {
        // FND 끄기
        for (int i = 0; i < FND_PINS_COUNT; i++) {
            digitalWrite(fnd_pins[i], HIGH);
        }
        fnd_state.is_initialized = 0;
        printf("[FND] 자원 해제 완료\n");
    }

    is_counting = 0;
    countdown_stop_requested = 0;
    countdown_tid = 0;
    
    // 부저 라이브러리 해제
    if (buzzer_lib) {
        dlclose(buzzer_lib);
        buzzer_lib = NULL;
        buzzer_play_func = NULL;
        buzzer_stop_func = NULL;
    }
    
    pthread_mutex_unlock(&fnd_state.mutex);
}
