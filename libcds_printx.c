#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <wiringPi.h>
#include <wiringPiI2C.h>
#include "control_device.h"

// 함수 선언
int cds_read(void);

#define CDS_I2C_ADDR 0x48
#define CDS_CHANNEL 0
#define CDS_THRESHOLD 180
#define AUTO_LED_PIN 17  // 자동 제어용 LED (조도 센서 연동)

// 조도 센서 상태 관리
static device_state_t cds_state = {0, PTHREAD_MUTEX_INITIALIZER};
static int cds_fd = -1;
static int current_light_value = -1;
static int is_bright = -1;  // -1: unknown, 0: dark, 1: bright
static pthread_t auto_led_tid = 0;
static int auto_led_enabled = 0;
static int running = 1;

// 자동 LED (GPIO 17) 상태 관리
static device_state_t auto_led_state = {0, PTHREAD_MUTEX_INITIALIZER};

// 자동 LED 초기화
int auto_led_init(void) {
    pthread_mutex_lock(&auto_led_state.mutex);
    
    if (auto_led_state.is_initialized) {
        pthread_mutex_unlock(&auto_led_state.mutex);
        return 0;
    }
    
    pinMode(AUTO_LED_PIN, OUTPUT);
    digitalWrite(AUTO_LED_PIN, LOW);
    auto_led_state.is_initialized = 1;
    
    pthread_mutex_unlock(&auto_led_state.mutex);
    printf("[AUTO_LED] 초기화 완료 (GPIO %d)\n", AUTO_LED_PIN);
    return 0;
}

// 자동 LED 켜기
int auto_led_on(void) {
    pthread_mutex_lock(&auto_led_state.mutex);
    
    if (!auto_led_state.is_initialized && auto_led_init() < 0) {
        pthread_mutex_unlock(&auto_led_state.mutex);
        return -1;
    }
    
    digitalWrite(AUTO_LED_PIN, HIGH);
    printf("[AUTO_LED] ON (GPIO %d)\n", AUTO_LED_PIN);
    
    pthread_mutex_unlock(&auto_led_state.mutex);
    return 0;
}

// 자동 LED 끄기
int auto_led_off(void) {
    pthread_mutex_lock(&auto_led_state.mutex);
    
    if (!auto_led_state.is_initialized && auto_led_init() < 0) {
        pthread_mutex_unlock(&auto_led_state.mutex);
        return -1;
    }
    
    digitalWrite(AUTO_LED_PIN, LOW);
    printf("[AUTO_LED] OFF (GPIO %d)\n", AUTO_LED_PIN);
    
    pthread_mutex_unlock(&auto_led_state.mutex);
    return 0;
}

// 자동 LED 제어 스레드
void* auto_led_thread(void *arg) {
    (void)arg;
    
    printf("[CDS] 자동 LED 제어 시작 (어두우면 GPIO %d ON)\n", AUTO_LED_PIN);
    
    while (running && auto_led_enabled) {
        // 조도 센서 값 읽기
        if (cds_read() == 0) {
            pthread_mutex_lock(&cds_state.mutex);
            int bright = is_bright;
            int light_val = current_light_value;
            pthread_mutex_unlock(&cds_state.mutex);
            
            if (bright == 0) {  // 어두우면 LED ON
                auto_led_on();
                printf("[CDS] 어두움 감지 (값:%d) -> AUTO_LED ON\n", light_val);
            } else if (bright == 1) {  // 밝으면 LED OFF
                auto_led_off();
                printf("[CDS] 밝음 감지 (값:%d) -> AUTO_LED OFF\n", light_val);
            }
        }
        
        delay(1000);  // 1초마다 체크
    }
    
    printf("[CDS] 자동 LED 제어 종료\n");
    return NULL;
}

// 조도 센서 초기화
int cds_init(void) {
    pthread_mutex_lock(&cds_state.mutex);
    
    if (cds_state.is_initialized) {
        pthread_mutex_unlock(&cds_state.mutex);
        return 0;
    }

    // I2C 인터페이스 설정
    if ((cds_fd = wiringPiI2CSetupInterface("/dev/i2c-1", CDS_I2C_ADDR)) < 0) {
        fprintf(stderr, "[CDS] I2C 설정 실패\n");
        pthread_mutex_unlock(&cds_state.mutex);
        return -1;
    }
    
    // 자동 LED 초기화
    if (auto_led_init() < 0) {
        fprintf(stderr, "[CDS] 자동 LED 초기화 실패\n");
        pthread_mutex_unlock(&cds_state.mutex);
        return -1;
    }
    
    cds_state.is_initialized = 1;
    pthread_mutex_unlock(&cds_state.mutex);
    printf("[CDS] 조도 센서 초기화 완료 (I2C 주소: 0x%02X)\n", CDS_I2C_ADDR);
    return 0;
}

// 조도 센서 값 읽기
int cds_read(void) {
    pthread_mutex_lock(&cds_state.mutex);
    
    if (!cds_state.is_initialized && cds_init() < 0) {
        pthread_mutex_unlock(&cds_state.mutex);
        return -1;
    }
    
    // ADC 채널 설정 및 읽기
    wiringPiI2CWrite(cds_fd, 0x00 | CDS_CHANNEL);
    (void)wiringPiI2CRead(cds_fd);
    int a2dVal = wiringPiI2CRead(cds_fd);  // 실제 값
    
    current_light_value = a2dVal;
    
    // 밝기 판단
    if (a2dVal < CDS_THRESHOLD) {
        is_bright = 1;  // 밝음
    } else {
        is_bright = 0;  // 어둠
    }
    
    printf("[CDS] 조도값: %d (%s)\n", a2dVal, is_bright ? "밝음" : "어둠");
    
    pthread_mutex_unlock(&cds_state.mutex);
    return 0;
}

// 조도 센서 값 가져오기
int cds_get_value(void) {
    pthread_mutex_lock(&cds_state.mutex);
    int value = current_light_value;
    pthread_mutex_unlock(&cds_state.mutex);
    return value;
}

// 밝기 상태 가져오기
int cds_is_bright(void) {
    pthread_mutex_lock(&cds_state.mutex);
    int bright = is_bright;
    pthread_mutex_unlock(&cds_state.mutex);
    return bright;
}

// 자동 LED 제어 시작
int cds_auto_led_start(void) {
    pthread_mutex_lock(&cds_state.mutex);
    
    if (!cds_state.is_initialized && cds_init() < 0) {
        pthread_mutex_unlock(&cds_state.mutex);
        return -1;
    }
    
    if (auto_led_enabled) {
        pthread_mutex_unlock(&cds_state.mutex);
        return 0;  // 이미 실행 중
    }
    
    auto_led_enabled = 1;
    
    if (pthread_create(&auto_led_tid, NULL, auto_led_thread, NULL) != 0) {
        fprintf(stderr, "[CDS] 자동 LED 스레드 생성 실패\n");
        auto_led_enabled = 0;
        pthread_mutex_unlock(&cds_state.mutex);
        return -1;
    }
    
    pthread_detach(auto_led_tid);
    pthread_mutex_unlock(&cds_state.mutex);
    printf("[CDS] 자동 LED 제어 시작 (GPIO %d)\n", AUTO_LED_PIN);
    return 0;
}

// 자동 LED 제어 중지
int cds_auto_led_stop(void) {
    pthread_mutex_lock(&cds_state.mutex);
    
    if (!auto_led_enabled) {
        pthread_mutex_unlock(&cds_state.mutex);
        return 0;  // 이미 중지됨
    }
    
    auto_led_enabled = 0;
    pthread_mutex_unlock(&cds_state.mutex);
    
    // 스레드 종료 대기
    if (auto_led_tid != 0) {
        pthread_join(auto_led_tid, NULL);
        auto_led_tid = 0;
    }
    
    // 자동 LED 끄기
    auto_led_off();
    
    printf("[CDS] 자동 LED 제어 중지\n");
    return 0;
}

// 수동 자동 LED 제어 함수들
int auto_led_manual_on(void) {
    return auto_led_on();
}

int auto_led_manual_off(void) {
    return auto_led_off();
}

// 조도 센서 상태 확인
int cds_get_status(char* status_buf, int buf_size) {
    pthread_mutex_lock(&cds_state.mutex);
    
    if (!cds_state.is_initialized) {
        snprintf(status_buf, buf_size, "CDS: NOT_INITIALIZED");
    } else if (auto_led_enabled) {
        snprintf(status_buf, buf_size, "CDS: AUTO_LED_ON (값:%d, %s)", 
                current_light_value, is_bright ? "밝음" : "어둠");
    } else {
        snprintf(status_buf, buf_size, "CDS: IDLE (값:%d, %s)", 
                current_light_value, is_bright ? "밝음" : "어둠");
    }
    
    pthread_mutex_unlock(&cds_state.mutex);
    return 0;
}

// 조도 센서 자원 해제
void cds_cleanup(void) {
    pthread_mutex_lock(&cds_state.mutex);
    
    running = 0;
    
    // 자동 LED 제어 중지
    if (auto_led_enabled) {
        auto_led_enabled = 0;
        pthread_mutex_unlock(&cds_state.mutex);
        if (auto_led_tid != 0) {
            pthread_join(auto_led_tid, NULL);
            auto_led_tid = 0;
        }
        pthread_mutex_lock(&cds_state.mutex);
    }
    
    // 자동 LED 끄기
    if (auto_led_state.is_initialized) {
        digitalWrite(AUTO_LED_PIN, LOW);
        auto_led_state.is_initialized = 0;
        printf("[AUTO_LED] 자원 해제\n");
    }
    
    if (cds_state.is_initialized) {
        if (cds_fd >= 0) {
            // I2C 연결 정리
        }
        cds_state.is_initialized = 0;
        printf("[CDS] 자원 해제\n");
    }
    
    pthread_mutex_unlock(&cds_state.mutex);
}
