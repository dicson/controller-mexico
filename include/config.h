#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

#define MY_SDA 17
#define MY_SCL 18
#define VERSION "1.0"

// Определяем пин и параметры ШИМ
#define LED_PIN 23                        
#define LEDC_CHANNEL LEDC_CHANNEL_0       
#define LEDC_MODE LEDC_LOW_SPEED_MODE     
#define LEDC_TIMER LEDC_TIMER_0           
#define LEDC_RESOLUTION LEDC_TIMER_12_BIT 
#define LEDC_FREQUENCY 5000               

static const char *ap_ssid = "controller";
static const char *ap_pass = "80100000";

const uint8_t RELAY_PINS[4] = {32, 33, 25, 26};

enum kk : size_t
{
    // время запуска
    tm_hour,
    tm_min,
    // Дни недели
    d_1,
    d_2,
    d_3,
    d_4,
    d_5,
    d_6,
    d_7,
    // Длительность работы зон (минуты)
    dur_1,
    dur_2,
    dur_3,
    // Выключатели зон (по умолчанию все зоны активны)
    z1_on,
    z2_on,
    z3_on,
    z4_on,
    // Логи поливов (последние 15)
    log_0,
    log_1,
    log_2,
    log_3,
    log_4,
    log_5,
    log_6,
    log_7,
    log_8,
    log_9,
    log_10,
    log_11,
    log_12,
    log_13,
    log_14,
};

#endif // CONFIG_H
