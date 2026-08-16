#include "driver/ledc.h"
#include "hardware.h"
#include "config.h"
#include "web_interface.h"

#include <WiFi.h>
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
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

// Функция конвертации причины перезагрузки в понятный текст
const __FlashStringHelper *getResetReasonText(esp_reset_reason_t reason)
{
    switch (reason)
    {
    case ESP_RST_POWERON:
        return F("Power-on (Включение питания)");
    case ESP_RST_EXT:
        return F("External Pin (Сброс кнопкой/пином)");
    case ESP_RST_SW:
        return F("Software Reset (Программный перезапуск)");
    case ESP_RST_PANIC:
        return F("Exception/Panic (Сбой ядра/краш)");
    case ESP_RST_INT_WDT:
        return F("Interrupt Watchdog");
    case ESP_RST_TASK_WDT:
        return F("Task Watchdog");
    case ESP_RST_BROWNOUT:
        return F("Brownout (Просадка напряжения питания)");
    case ESP_RST_SDIO:
        return F("SDIO Reset");
    default:
        return F("Неизвестно");
    }
}

/**
 * @brief Собирает подробную статистику системы в строку (одна колонка, на русском).
 */
String getSystemInfo()
{
    String info;
    info.reserve(1024);

    info += String(F("\n--- [ Системная информация ] ---\n"));

    // Память
    info += String(F("ОЗУ Всего  : ")) + String(ESP.getHeapSize()) + String(F(" байт\n"));
    info += String(F("ОЗУ Свобод.: ")) + String(ESP.getFreeHeap()) + String(F(" байт\n"));
    info += String(F("ОЗУ Мин    : ")) + String(ESP.getMinFreeHeap()) + String(F(" байт\n"));
    info += String(F("Макс. блок : ")) + String(ESP.getMaxAllocHeap()) + String(F(" байт\n"));
    
    // Температура
    info += String(F("Темп. чипа : ")) + String(temperatureRead(), 2) + String(F(" °C\n"));

    // Чип
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);
    info += String(F("Чип        : ")) + String(ESP.getChipModel()) + String(F(" (версия v")) + String(chip_info.revision) + String(F(")\n"));
    info += String(F("Ядер       : ")) + String(chip_info.cores) + String(F("\n"));

    // Сеть / Флеш
    uint64_t mac = ESP.getEfuseMac();
    char mac_buf[16];
    snprintf(mac_buf, sizeof(mac_buf), "%04X%08X", (uint16_t)(mac >> 32), (uint32_t)mac);
    info += String(F("MAC UID    : ")) + String(mac_buf) + String(F("\n"));
    
    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);
    info += String(F("Flash      : ")) + String(flash_size / (1024 * 1024)) + String(F(" МБ\n"));
    info += String(F("Flash Скор : ")) + String(ESP.getFlashChipSpeed() / 1000000) + String(F(" МГц\n"));

    // CPU / Сброс
    info += String(F("Частота CPU: ")) + String(getCpuFrequencyMhz()) + String(F(" МГц\n"));
    info += String(F("Частота APB: ")) + String(getApbFrequency() / 1000000) + String(F(" МГц\n"));
    info += String(F("Сброс      : ")) + String(getResetReasonText(esp_reset_reason())) + String(F("\n"));
    info += String(F("Работа     : ")) + String(millis() / 1000) + String(F(" сек\n"));

    info += String(F("================================"));
    return info;
}

/**
 * @brief Выводит подробную статистику использования Heap-памяти в Serial.
 */
void printMemoryUsage()
{
    Serial.println(getSystemInfo());
}