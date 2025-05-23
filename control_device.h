#ifndef CONTROL_DEVICE_H
#define CONTROL_DEVICE_H

#include <pthread.h>

// 공통 정의
#define BUFFER_SIZE 1024
#define MAX_RESPONSE_SIZE 512

// 디바이스 상태 구조체
typedef struct {
    int is_initialized;
    pthread_mutex_t mutex;
} device_state_t;

// LED 함수 포인터 구조체
typedef struct {
    int (*init)(void);
    int (*on)(void);
    int (*off)(void);
    int (*brightness)(int level);
    void (*cleanup)(void);
    int (*get_status)(char* status_buf, int buf_size);
} led_functions_t;

// SEGMENT 함수 포인터 구조체
typedef struct {
    int (*init)(void);
    int (*display)(int num);
    int (*countdown)(int start_num);
    int (*stop)(void);
    void (*off)(void);
    void (*cleanup)(void);
    int (*get_status)(char* status_buf, int buf_size);
} segment_functions_t;

// 부저 함수 포인터 구조체
typedef struct {
    int (*init)(void);
    int (*play)(void);
    int (*stop)(void);
    void (*cleanup)(void);
    int (*get_status)(char* status_buf, int buf_size);
} buzzer_functions_t;

// 조도센서 함수 포인터 구조체
typedef struct {
    int (*init)(void);
    int (*read)(void);
    int (*get_value)(void);
    int (*is_bright)(void);
    int (*auto_led_start)(void);
    int (*auto_led_stop)(void);
    int (*manual_on)(void);
    int (*manual_off)(void);
    void (*cleanup)(void);
    int (*get_status)(char* status_buf, int buf_size);
} cds_functions_t;

// 전체 디바이스 함수 포인터 구조체
typedef struct {
    led_functions_t led;
    segment_functions_t segment;
    buzzer_functions_t buzzer;
    cds_functions_t cds;
} device_functions_t;

// 전역 디바이스 함수 포인터 (main.c에서 정의)
extern device_functions_t device_funcs;

// 동적 라이브러리 관리 함수
int load_device_libraries(void);
void unload_device_libraries(void);

#endif // CONTROL_DEVICE_H
