#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <wiringPi.h>
#include <softTone.h>
#include "control_device.h"

#define BUZZER_PIN 19
#define TOTAL_NOTES 32

// 학교종 멜로디 (기존 코드와 동일)
static int school_bell_notes[] = {
    391, 391, 440, 440, 391, 391, 329.63, 329.63,
    391, 391, 329.63, 329.63, 293.66, 293.66, 293.66, 0,
    391, 391, 440, 440, 391, 391, 329.63, 329.63,
    391, 329.63, 293.66, 329.63, 261.63, 261.63, 261.63, 0
};

// 부저 상태 관리
static device_state_t buzzer_state = {0, PTHREAD_MUTEX_INITIALIZER};
static pthread_t music_thread_id = 0;
static int is_playing = 0;
static int running = 1;

// 학교종 멜로디 재생 스레드
void* melody_thread(void *arg) {
    (void)arg;

    pthread_mutex_lock(&buzzer_state.mutex);
    is_playing = 1;
    pthread_mutex_unlock(&buzzer_state.mutex);

    printf("[BUZZER] 학교종 멜로디 재생 시작\n");

    for (int i = 0; i < TOTAL_NOTES && is_playing && running; i++) {
        softToneWrite(BUZZER_PIN, school_bell_notes[i]);
        delay(280);  // 음의 전체 길이만큼 출력되도록 대기
    }

    softToneWrite(BUZZER_PIN, 0);  // 소리 중지

    pthread_mutex_lock(&buzzer_state.mutex);
    is_playing = 0;
    pthread_mutex_unlock(&buzzer_state.mutex);

    printf("[BUZZER] 학교종 멜로디 재생 완료\n");
    return NULL;
}

// 부저 초기화
int buzzer_init(void) {
    pthread_mutex_lock(&buzzer_state.mutex);

    if (buzzer_state.is_initialized) {
        pthread_mutex_unlock(&buzzer_state.mutex);
        return 0;
    }

    if (wiringPiSetupGpio() == -1) {
        fprintf(stderr, "[BUZZER] wiringPiSetupGpio 초기화 실패\n");
        pthread_mutex_unlock(&buzzer_state.mutex);
        return -1;
    }

    softToneCreate(BUZZER_PIN);
    softToneWrite(BUZZER_PIN, 0);  // 초기에는 소리 없음

    buzzer_state.is_initialized = 1;
    pthread_mutex_unlock(&buzzer_state.mutex);
    printf("[BUZZER] 초기화 완료 (GPIO %d)\n", BUZZER_PIN);
    return 0;
}

// 부저 재생 (학교종 멜로디)
int buzzer_play(void) {
    pthread_mutex_lock(&buzzer_state.mutex);

    if (!buzzer_state.is_initialized && buzzer_init() < 0) {
        pthread_mutex_unlock(&buzzer_state.mutex);
        return -1;
    }

    // 이미 재생 중이면 중지 후 새로 시작
    if (is_playing && music_thread_id != 0) {
        is_playing = 0;  // 이전 스레드 중지 신호
        pthread_mutex_unlock(&buzzer_state.mutex);
        pthread_join(music_thread_id, NULL);  // 이전 스레드 종료 대기
        pthread_mutex_lock(&buzzer_state.mutex);
        music_thread_id = 0;
    }

    // 새로운 멜로디 재생 스레드 시작
    if (pthread_create(&music_thread_id, NULL, melody_thread, NULL) != 0) {
        fprintf(stderr, "[BUZZER] 스레드 생성 실패\n");
        pthread_mutex_unlock(&buzzer_state.mutex);
        return -1;
    }

    pthread_detach(music_thread_id);
    pthread_mutex_unlock(&buzzer_state.mutex);
    return 0;
}

// 부저 중지
int buzzer_stop(void) {
    pthread_mutex_lock(&buzzer_state.mutex);

    if (!buzzer_state.is_initialized) {
        pthread_mutex_unlock(&buzzer_state.mutex);
        return 0;  // 초기화되지 않았으면 이미 꺼진 상태
    }

    // 재생 중이면 중지
    if (is_playing && music_thread_id != 0) {
        is_playing = 0;  // 스레드 중지 신호
        pthread_mutex_unlock(&buzzer_state.mutex);
        pthread_join(music_thread_id, NULL);  // 스레드 종료 대기
        pthread_mutex_lock(&buzzer_state.mutex);
        music_thread_id = 0;
    }

    softToneWrite(BUZZER_PIN, 0);  // 소리 완전 중지
    printf("[BUZZER] 중지\n");

    pthread_mutex_unlock(&buzzer_state.mutex);
    return 0;
}

// 부저 상태 확인
int buzzer_get_status(char* status_buf, int buf_size) {
    pthread_mutex_lock(&buzzer_state.mutex);

    if (!buzzer_state.is_initialized) {
        snprintf(status_buf, buf_size, "BUZZER: NOT_INITIALIZED");
    } else if (is_playing) {
        snprintf(status_buf, buf_size, "BUZZER: PLAYING");
    } else {
        snprintf(status_buf, buf_size, "BUZZER: IDLE");
    }

    pthread_mutex_unlock(&buzzer_state.mutex);
    return 0;
}

// 부저 자원 해제
void buzzer_cleanup(void) {
    pthread_mutex_lock(&buzzer_state.mutex);

    running = 0;

    // 재생 중이면 중지
    if (is_playing && music_thread_id != 0) {
        is_playing = 0;
        pthread_mutex_unlock(&buzzer_state.mutex);
        pthread_join(music_thread_id, NULL);
        pthread_mutex_lock(&buzzer_state.mutex);
        music_thread_id = 0;
    }

    if (buzzer_state.is_initialized) {
        softToneWrite(BUZZER_PIN, 0);
        buzzer_state.is_initialized = 0;
        printf("[BUZZER] 자원 해제\n");
    }

    pthread_mutex_unlock(&buzzer_state.mutex);
}
