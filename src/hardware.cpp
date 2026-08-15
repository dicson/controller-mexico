#include "driver/ledc.h"
#include "hardware.h"
#include "config.h"
#include "web_interface.h"

/**
 * @brief Включает реле текущей зоны и общее реле питания.
 */
void relayOn()
{
    digitalWrite(RELAY_PINS[current_zone], HIGH);
    digitalWrite(RELAY_PINS[3], HIGH);
    RELAY_STATE[current_zone] = true;
    RELAY_STATE[3] = true;
}

/**
 * @brief Принудительно выключает все реле и сбрасывает их состояние в БД.
 */
void turnOffAllRelays()
{
    for (int i = 0; i < 4; i++)
    {
        digitalWrite(RELAY_PINS[i], LOW);
        RELAY_STATE[i] = false;
        sett.updater().update(LED_NAMES[i], RELAY_STATE[i]);
    }
}

/**
 * @brief Останавливает текущее действие (полив), выключает все реле и сбрасывает статус.
 */
void stopAction()
{
    turnOffAllRelays();
    watering_active = false;
    current_zone = -1;
    updateWidgets();
}

/**
 * @brief Переключает состояние конкретного реле зоны и обновляет веб-интерфейс.
 * @param zone Номер зоны (0-2).
 */
void switchRelay(int zone)
{
    digitalWrite(RELAY_PINS[zone], RELAY_STATE[zone]);
    // 24v (RELAY_PINS[3]) включен, если активна любая из зон 0, 1 или 2
    digitalWrite(RELAY_PINS[3], RELAY_STATE[0] || RELAY_STATE[1] || RELAY_STATE[2]);
    sett.updater().update(LED_NAMES[zone], RELAY_STATE[zone]);
}

/**
 * @brief Управляет миганием светодиода для индикации ошибок RTC или нормальной работы.
 */
void blinkRTCErrorLED()
{
    if (rtc_error)
    {
        EVERY_S(1.5)
        {
            ledc_set_fade_time_and_start(LEDC_MODE, LEDC_CHANNEL, 2000, 100, LEDC_FADE_NO_WAIT);
            ledc_set_fade_time_and_start(LEDC_MODE, LEDC_CHANNEL, 0, 100, LEDC_FADE_NO_WAIT);
        }
    }
    else
    {
        EVERY_S(4.5)
        {
            ledc_set_fade_time_and_start(LEDC_MODE, LEDC_CHANNEL, 1000, 1500, LEDC_FADE_NO_WAIT);
            ledc_set_fade_time_and_start(LEDC_MODE, LEDC_CHANNEL, 0, 1500, LEDC_FADE_NO_WAIT);
        }
    }
}

/**
 * @brief Инициализирует пины реле и параметры светодиода (LEDC).
 */
void pinLedSetup()
{
    Serial.begin(115200);

    for (int i = 0; i < 4; i++)
    {
        pinMode(RELAY_PINS[i], OUTPUT);
        digitalWrite(RELAY_PINS[i], LOW);
        RELAY_STATE[i] = false;
    }
    // Настройка программируемого светодиода D14
    // 1. Настройка таймера LEDC
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_RESOLUTION,
        .timer_num = LEDC_TIMER,
        .freq_hz = LEDC_FREQUENCY,
        .clk_cfg = LEDC_AUTO_CLK};
    ledc_timer_config(&ledc_timer);

    // 2. Настройка канала LEDC
    ledc_channel_config_t ledc_channel = {
        .gpio_num = LED_PIN,
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER,
        .duty = 0, // Начальная яркость 0
        .hpoint = 0};
    ledc_channel_config(&ledc_channel);

    // 3. ОБЯЗАТЕЛЬНО: Установка службы fade-анимаций
    ledc_fade_func_install(0);
}

/**
 * @brief Инициализирует I2C интерфейс и часы реального времени (RTC).
 */
void rtcSetup()
{
    // Инициализация I2C с указанными пинами
    Wire.end();
    Wire.begin(MY_SDA, MY_SCL);
    // delay(4000);
    // Инициализация RTC
    rtc.begin();
    if (!rtc.isOK())
    {
        Serial.println(F("Error: DS3231 RTC не найден!"));
        alert_f = "Не работает!!!";
        rtc_error = true;
    }
    else
    {
        char buf[40];
        sprintf(buf, "В норме %d °C", rtc.getTempInt());
        alert_f = buf;
        if (rtc.isReset())
        {
            // был сброс питания RTC, время некорректное
            alert_f = "Села батарейка!!!";
            rtc_error = true;
        }
        sett.rtc.sync(rtc);
        sett.rtc.onSync(onSyncCallback);
    }
}

/**
 * @brief Выводит подробную статистику использования Heap-памяти в Serial.
 */
void printMemoryUsage()
{
    Serial.println(F("\n--- [Статистика памяти] ---"));
    Serial.printf(F("Общий Heap: %u байт\n"), ESP.getHeapSize());
    Serial.printf(F("Свободный Heap: %u байт\n"), ESP.getFreeHeap());
    Serial.printf(F("Минимальный свободный Heap: %u байт\n"), ESP.getMinFreeHeap());
    Serial.printf(F("Максимальный доступный блок: %u байт\n"), ESP.getMaxAllocHeap());
    Serial.println(F("---------------------------\n"));
}