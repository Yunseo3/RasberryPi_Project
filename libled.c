#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <wiringPi.h>
#include "control_device.h"

#define LED_PIN 18

static device_state_t led_state = {0, PTHREAD_MUTEX_INITIALIZER};
static int current_brightness = -1;

int led_init(void) {
    pthread_mutex_lock(&led_state.mutex);
    
    if (led_state.is_initialized) {
        pthread_mutex_unlock(&led_state.mutex);
        return 0;
    }
    
    if (wiringPiSetupGpio() == -1) {
        fprintf(stderr, "[LED] wiringPi 초기화 실패\n");
        pthread_mutex_unlock(&led_state.mutex);
        return -1;
    }
    
    pinMode(LED_PIN, PWM_OUTPUT);
    pwmSetMode(PWM_MODE_MS);
    pwmSetRange(1024);
    pwmSetClock(375);
    pwmWrite(LED_PIN, 0);
    
    led_state.is_initialized = 1;
    pthread_mutex_unlock(&led_state.mutex);
    printf("[LED] 초기화 완료 (GPIO %d)\n", LED_PIN);
    return 0;
}

int led_on(void) {
    pthread_mutex_lock(&led_state.mutex);
    
    if (!led_state.is_initialized && led_init() < 0) {
        pthread_mutex_unlock(&led_state.mutex);
        return -1;
    }
    
    pwmWrite(LED_PIN, 1024);
    current_brightness = 1024;
    printf("[LED] ON\n");
    
    pthread_mutex_unlock(&led_state.mutex);
    return 0;
}

int led_off(void) {
    pthread_mutex_lock(&led_state.mutex);
    
    if (!led_state.is_initialized && led_init() < 0) {
        pthread_mutex_unlock(&led_state.mutex);
        return -1;
    }
    
    pwmWrite(LED_PIN, 0);
    current_brightness = 0;
    printf("[LED] OFF\n");
    
    pthread_mutex_unlock(&led_state.mutex);
    return 0;
}

int led_brightness(int level) {
    pthread_mutex_lock(&led_state.mutex);
    
    if (!led_state.is_initialized && led_init() < 0) {
        pthread_mutex_unlock(&led_state.mutex);
        return -1;
    }
    
    int pwm_value;
    switch (level) {
        case 0: pwm_value = 102; break;   // 10%
        case 1: pwm_value = 512; break;   // 50%
        case 2: pwm_value = 1024; break;  // 100%
        default:
            pthread_mutex_unlock(&led_state.mutex);
            return -1;
    }
    
    pwmWrite(LED_PIN, pwm_value);
    current_brightness = pwm_value;
    printf("[LED] 밝기 레벨 %d\n", level);
    
    pthread_mutex_unlock(&led_state.mutex);
    return 0;
}

int led_get_status(char* status_buf, int buf_size) {
    pthread_mutex_lock(&led_state.mutex);
    
    if (!led_state.is_initialized) {
        snprintf(status_buf, buf_size, "LED: NOT_INITIALIZED");
    } else if (current_brightness == 0) {
        snprintf(status_buf, buf_size, "LED: OFF");
    } else if (current_brightness <= 102) {
        snprintf(status_buf, buf_size, "LED: LOW");
    } else if (current_brightness <= 512) {
        snprintf(status_buf, buf_size, "LED: MIDDLE");
    } else {
        snprintf(status_buf, buf_size, "LED: HIGH");
    }
    
    pthread_mutex_unlock(&led_state.mutex);
    return 0;
}

void led_cleanup(void) {
    pthread_mutex_lock(&led_state.mutex);
    
    if (led_state.is_initialized) {
        pwmWrite(LED_PIN, 0);
        led_state.is_initialized = 0;
        current_brightness = 0;
        printf("[LED] 자원 해제\n");
    }
    
    pthread_mutex_unlock(&led_state.mutex);
}
