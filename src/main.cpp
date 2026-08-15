#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include "config.h"
#include "globals.h"
#include "hardware.h"
#include "web_interface.h"

// Инициализация глобальных объектов
GyverDBFile db(&LittleFS, "/data.db");
GyverDS3231 rtc;
SettingsGyverWS sett("Контроллер полива", &db);

// Инициализация глобальных переменных
String alert_f;
String status;
bool rtc_error = false;
bool RELAY_STATE[4] = {0};
bool already_watered = false;
bool watering_active = false;
uint32_t zone_start_millis = 0;
int zone_durations[3] = {0, 0, 0};
int current_zone = -1;

GTimer<millis> timer_focus(1500, false, GTMode::Timeout);
sets::Logger logger(512);

const size_t LED_NAMES[4] = {H(relay1), H(relay2), H(relay3), H(relay4)};

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
            Serial.print(F("Зона "));
            Serial.print(current_zone + 1);
            Serial.println(F(" пропущена (0 мин)."));
        }
    }

    relayOn();                                                                  // Включаем реле текущей зоны
    sett.updater().update(LED_NAMES[current_zone], RELAY_STATE[current_zone]); // обновляем светодиод

    zone_start_millis = millis();
    updateStatus();

    Serial.print(F(">>> Включена Зона "));
    Serial.print(current_zone + 1);
    Serial.print(F(" на "));
    Serial.print(zone_durations[current_zone]);
    Serial.println(F(" мин."));
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

    // Инициализация базы данных
    db.begin();
    // Настройки расписания
    db.init(kk::tm_hour, 8);
    db.init(kk::tm_min, 30);

    // Дни недели
    db.init(kk::d_1, true);
    db.init(kk::d_2, true);
    db.init(kk::d_3, true);
    db.init(kk::d_4, true);
    db.init(kk::d_5, true);
    db.init(kk::d_6, true);
    db.init(kk::d_7, true);

    // Длительность работы зон (минуты)
    db.init(kk::dur_1, 10);
    db.init(kk::dur_2, 15);
    db.init(kk::dur_3, 5);

    // Выключатели зон (по умолчанию все зоны активны)
    db.init(kk::z1_on, true);
    db.init(kk::z2_on, true);
    db.init(kk::z3_on, true);

    // Инициализация записей журнала
    for (int i = 0; i < 15; i++)
    {
        db.init(kk::log_0 + i, "");
    }
    // Загрузка записей в logger
    for (int i = 0; i < 15; i++)
    {
        String log_entry = db[kk::log_0 + i].toString();
        if (log_entry.length() > 0)
        {
            logger.println(log_entry);
        }
    }

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
    int cur_hour = sett.rtc.hour();
    int cur_min = sett.rtc.minute();
    int cur_day = sett.rtc.weekDay();
    int start_hour = db[kk::tm_hour].toInt();
    int start_min = db[kk::tm_min].toInt();
    // Массив ключей согласно порядку в enum kk
    static const kk day_keys[] = {kk::d_1, kk::d_2, kk::d_3, kk::d_4, kk::d_5, kk::d_6, kk::d_7};
    // Безопасный доступ
    bool day_allowed = false;

    if (cur_day >= 1 && cur_day <= 7)
        day_allowed = db[day_keys[cur_day - 1]].toBool();

    if (day_allowed && cur_hour == start_hour && cur_min == start_min)
    {
        if (!already_watered)
        {
            Serial.println(F("Время расписания пришло. Запуск поочередного полива."));
            already_watered = true;

            // Запуск автомата (он внутри сам проверит чекбоксы)
            startWateringSequence();
        }
    }

    if (cur_min != start_min)
        already_watered = false;
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
            Serial.println(sett.rtc.toString());
        }

        // 1. Проверяем таймер текущей активной зоны
        if (watering_active && current_zone >= 0 && current_zone < 3)
        {
            uint32_t elapsed = millis() - zone_start_millis;
            uint32_t limit = (uint32_t)zone_durations[current_zone] * 60 * 1000;

            if (elapsed >= limit)
                goToNextZone();

            // Обновление статуса полива только при фокусе
            if (sett.focused())
                updateStatus();
        }
        // 2. Проверяем наступление времени старта по расписанию
        if (!watering_active)
            checkSchedule();
    }

    if (timer_focus)
        updateWidgets();
}
