#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "config.h"
#include "globals.h"
#include "hardware.h"
#include "web_interface.h"

/**
 * @brief Функция перехода к следующей зоне в очереди.
 * Отключает текущее реле, находит следующую активную зону и включает её.
 */
void goToNextZone()
{
    turnOffAllRelays(); // Отключаем активное реле

    // Ищем следующую зону, которая должна поливаться
    bool found_next = false;
    while (!found_next)
    {
        current_zone++; // Переходим к следующему реле

        // Если прошли все 3 зоны — завершаем сессию
        if (current_zone >= 3)
        {
            Serial.println(F(">>> Сессия полива полностью завершена <<<"));
            stopAction();
            endTimeToLog();
            return;
        }

        // Проверяем, установлена ли длительность для зоны
        if (zone_durations[current_zone] > 0)
            found_next = true; // Нашли следующую активную зону
        else
        {
            Serial.printf(F("Зона %d пропущена (0 мин).\n"), current_zone + 1);
        }
    }

    relayOn();                                                                 // Включаем реле текущей зоны
    sett.updater().update(LED_NAMES[current_zone], RELAY_STATE[current_zone]); // обновляем светодиод

    zone_start_millis = millis();
    updateStatus();

    Serial.printf(F(">>> Включена Зона %d на %d мин.\n"), current_zone + 1, zone_durations[current_zone]);
}

/**
 * @brief Функция запуска последовательного полива с учетом чекбоксов.
 * Настраивает параметры полива и запускает процесс.
 */
void startWateringSequence()
{
    if (db[kk::z1_on].toBool() || db[kk::z2_on].toBool() || db[kk::z3_on].toBool())
    {
        auto u = sett.updater();
        u.updateColor(H(Button), sets::Colors::Red)
            .updateText(H(Button), F("ОСТАНОВИТЬ ВСЁ"));

        addLog(sett.rtc.toString()); // Добавляем лог при старте полива
    }
    else
        return;
    // Проверяем каждую зону: если тумблер включен, берем минуты из БД, если выключен — пишем 0 (пропуск)
    zone_durations[0] = db[kk::z1_on].toBool() ? db[kk::dur_1].toInt() : 0;
    zone_durations[1] = db[kk::z2_on].toBool() ? db[kk::dur_2].toInt() : 0;
    zone_durations[2] = db[kk::z3_on].toBool() ? db[kk::dur_3].toInt() : 0;

    watering_active = true;
    current_zone = -1;
    goToNextZone(); // Запускаем цепочку
}

void onFocusChange()
{
    if (sett.focused())
        timer_focus.start();
}

/**
 * @brief Функция настройки системы.
 * Инициализирует аппаратное обеспечение, файловую систему, базу данных, Wi-Fi и RTC.
 */
void setup()
{
    pinLedSetup();

    if (!LittleFS.begin(true))
        Serial.println("LittleFS error");
    // настройки вебморды
    sett.config.requestTout = 3000;
    sett.config.sliderTout = 500;
    sett.config.updateTout = 2500;
    sett.config.theme = sets::Colors::Green;

    dbSetup();

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_pass);

    sett.begin();
    setStampZone(3);
    sett.rtc.sync(1767214800);

    rtcSetup();

    sett.onBuild(build);
    sett.setVersion(VERSION);
    // установить инфо о проекте (отображается на вкладке настроек и файлов)
    sett.setProjectInfo("Контроллер полива на дачу", "https://github.com/dicson/controller-mexico");
    sett.onFocusChange(onFocusChange);
}

/**
 * @brief Функция проверки расписания полива.
 * Сравнивает текущее время с временем старта из БД.
 */
void checkSchedule()
{
    // Проверка наступления времени старта
    if (sett.rtc.hour() != db[kk::tm_hour].toInt() || sett.rtc.minute() != db[kk::tm_min].toInt())
    {
        already_watered = false;
        return;
    }

    if (already_watered)
        return;

    // Проверка дня недели (1-7)
    int cur_day = sett.rtc.weekDay();
    if (cur_day < 1 || cur_day > 7)
        return;

    static const kk day_keys[] = {kk::d_1, kk::d_2, kk::d_3, kk::d_4, kk::d_5, kk::d_6, kk::d_7};
    if (db[day_keys[cur_day - 1]].toBool())
    {
        Serial.println(F("Время расписания пришло. Запуск поочередного полива."));
        already_watered = true;
        startWateringSequence();
    }
}

/**
 * @brief Главный цикл программы.
 * Выполняет периодические действия (тиканье интерфейса, контроль расписания и полива).
 */
void loop()
{
    sett.tick();
    blinkRTCErrorLED();

    EVERY_S(1)
    {
        // Обновление UI только если веб-интерфейс в фокусе
        if (sett.focused())
        {
            sett.updater().update(H(Time), sett.rtc.toString());
            // Serial.println(sett.rtc.toString());
        }

        // 1. Проверяем таймер текущей активной зоны
        if (watering_active && current_zone >= 0 && current_zone < 3)
        {
            uint32_t elapsed = millis() - zone_start_millis;
            uint32_t limit = (uint32_t)zone_durations[current_zone] * 60 * 1000;

            if (elapsed >= limit)
                goToNextZone();

            // Обновление статуса полива только при фокусе
            updateStatus();
        }
        // 2. Проверяем наступление времени старта по расписанию
        if (!watering_active)
            checkSchedule();
        // printMemoryUsage();
    }

    if (timer_focus)
        updateWidgets();
}
