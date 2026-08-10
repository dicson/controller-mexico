#include <Arduino.h>
#include <WiFi.h>
#include <GyverDBFile.h>     // База данных для автосохранения
#include <SettingsGyverWS.h> // Конструктор веб-интерфейса
#include <LittleFS.h>
#include <GyverDS3231.h>
#include <GTimer.h>
#include "driver/ledc.h"

#define MY_SDA 17
#define MY_SCL 18
#define VERSION "1.0"

// Определяем пин и параметры ШИМ
#define LED_PIN 23                        // Любой свободный GPIO на вашем ESP32-S3
#define LEDC_CHANNEL LEDC_CHANNEL_0       // Канал от 0 до 7
#define LEDC_MODE LEDC_LOW_SPEED_MODE     // Для S3 доступен только LOW_SPEED_MODE
#define LEDC_TIMER LEDC_TIMER_0           // Таймер от 0 до 3
#define LEDC_RESOLUTION LEDC_TIMER_12_BIT // Разрядность 12 бит (значения от 0 до 4095)
#define LEDC_FREQUENCY 5000               // Частота 5 кГц

const char *ap_ssid = "controller";
const char *ap_pass = "11111111";

GyverDBFile db(&LittleFS, "/data.db");
GyverDS3231 rtc;
SettingsGyverWS sett("Контроллер полива", &db);

String alert_f;
String status;
bool rtc_error = false;
const uint8_t RELAY_PINS[4] = {32, 33, 25, 26};
bool RELAY_STATE[4] = {0};
bool already_watered = false;
bool watering_active = false;
uint32_t zone_start_millis = 0;
int zone_durations[3] = {0, 0, 0};
int current_zone = -1;

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
};
const size_t RELAY_KEYS[4] = {H(relay1), H(relay2), H(relay3), H(relay4)};
static GTimer<millis> timer_focus(500, false, GTMode::Timeout);
sets::Logger logger(149 + 84);
void updateStatus();
void updateWidgets();

void updateHoldStatus()
{
    if (!watering_active)
    {
        auto u = sett.updater();
        if (db[kk::z1_on].toBool() || db[kk::z2_on].toBool() || db[kk::z3_on].toBool())
        {
            status = "Ожидание расписания";
            u.updateColor(H(Status), sets::Colors::Green);
        }
        else
        {
            status = "Автополив выключен";
            u.updateColor(H(Status), sets::Colors::Red);
        }
        u.updateText(H(Status), status);
    }
}

void onFocusChange()
{
    if (sett.focused())
        timer_focus.start();
}

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

// Функция принудительного выключения всех реле
void turnOffAllRelays()
{
    for (int i = 0; i < 4; i++)
    {
        digitalWrite(RELAY_PINS[i], LOW);
        RELAY_STATE[i] = false;
        sett.updater().update(RELAY_KEYS[i], RELAY_STATE[i]);
    }
}

void stopAction()
{
    turnOffAllRelays();
    watering_active = false;
    current_zone = -1;
    updateWidgets();
}

// Функция перехода к следующей зоне в очереди
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
            Serial.println(">>> Сессия полива полностью завершена <<<");
            stopAction();
            return;
        }

        // Проверяем, установлена ли длительность для зоны
        if (zone_durations[current_zone] > 0)
        {
            found_next = true; // Нашли следующую активную зону
        }
        else
        {
            Serial.print("Зона ");
            Serial.print(current_zone + 1);
            Serial.println(" пропущена (0 мин).");
        }
    }

    // Включаем реле текущей зоны
    digitalWrite(RELAY_PINS[current_zone], HIGH);
    digitalWrite(RELAY_PINS[3], HIGH);

    RELAY_STATE[current_zone] = true;
    RELAY_STATE[3] = true;

    // Выбираем соответствующий ключ из перечисления
    sett.updater().update(RELAY_KEYS[current_zone], RELAY_STATE[current_zone]);

    zone_start_millis = millis();
    updateStatus();

    Serial.print(">>> Включена Зона ");
    Serial.print(current_zone + 1);
    Serial.print(" на ");
    Serial.print(zone_durations[current_zone]);
    Serial.println(" мин.");
}

// Функция запуска последовательного полива с учетом чекбоксов
void startWateringSequence()
{
    if (db[kk::z1_on].toBool() || db[kk::z2_on].toBool() || db[kk::z3_on].toBool())
    {
        auto u = sett.updater();
        u.updateColor(H(Button), sets::Colors::Red)
            .updateText(H(Button), "ОСТАНОВИТЬ ВСЁ");
        logger.println(sett.rtc.toString());
        u.update(H(log), logger);
    }
    // Проверяем каждую зону: если тумблер включен, берем минуты из БД, если выключен — пишем 0 (пропуск)
    zone_durations[0] = db[kk::z1_on].toBool() ? db[kk::dur_1].toInt() : 0;
    zone_durations[1] = db[kk::z2_on].toBool() ? db[kk::dur_2].toInt() : 0;
    zone_durations[2] = db[kk::z3_on].toBool() ? db[kk::dur_3].toInt() : 0;

    watering_active = true;
    current_zone = -1;
    goToNextZone(); // Запускаем цепочку
}
void build(sets::Builder &b)
{
    if (rtc_error)
        b.Label(H(alert), "RTC", alert_f, sets::Colors::Red);
    else
        b.Label(H(alert), "RTC", alert_f, sets::Colors::Green);

    b.Label(H(Time), "Текущее время", sett.rtc.toString());

    if (b.beginRow("Состояние реле", sets::DivType::Block))
    {
        b.LED(H(relay1), "реле 1", RELAY_STATE[0]);
        b.LED(H(relay2), "реле 2", RELAY_STATE[1]);
        b.LED(H(relay3), "реле 3", RELAY_STATE[2]);
        b.endRow();
    }

    if (!watering_active)
    {
        if (db[kk::z1_on].toBool() || db[kk::z2_on].toBool() || db[kk::z3_on].toBool())
        {
            status = "Ожидание расписания";
            b.Label(H(Status), "Статус", status, sets::Colors::Green);
        }
        else
        {
            status = "Автополив выключен";
            b.Label(H(Status), "Статус", status, sets::Colors::Red);
        }
    }
    else
    {
        status = "ПОЛИВ: Зона " + String(current_zone + 1);
        b.Label(H(Status), "Статус", status);
    }

    // БЛОК РУЧНОГО ЗАПУСКА ВСЕЙ ЦЕПОЧКИ
    auto u = sett.updater();
    if (!watering_active)
    {
        if (b.Button(H(Button), "Запустить полив сейчас", sets::Colors::Green))
            startWateringSequence();
    }
    else
    {
        if (b.Button(H(Button), "ОСТАНОВИТЬ ВСЁ", sets::Colors::Red))
        {
            Serial.println("Принудительная остановка всей очереди");
            stopAction();
        }
    }

    // НАСТРОЙКИ ЗОН (Выключатель + Ползунок рядом)
    if (b.beginGroup("Настройка Зон Полива"))
    {
        if (b.beginRow("🌱 Зона 1", sets::DivType::Default))
        {
            if (b.Switch(kk::z1_on, "Поливать"))
                updateHoldStatus();
            b.Spinner(kk::dur_1, "(минут)", 0, 60, 1);
            b.endRow();
        }
        if (b.beginRow("🌱 Зона 2", sets::DivType::Block))
        {
            if (b.Switch(kk::z2_on, "Поливать"))
                updateHoldStatus();
            b.Spinner(kk::dur_2, "(минут)", 0, 60, 1);
            b.endRow();
        }
        if (b.beginRow("🌱 Зона 3", sets::DivType::Block))
        {
            if (b.Switch(kk::z3_on, "Поливать"))
                updateHoldStatus();
            b.Spinner(kk::dur_3, "(минут)", 0, 60, 1);
            b.endRow();
        }
        b.endGroup();
    }

    if (b.beginMenu("⏰ Расписание полива"))
    {
        if (b.beginGroup("Дни полива"))
        {
            b.Switch(kk::d_1, "Понедельник");
            b.Switch(kk::d_2, "Вторник");
            b.Switch(kk::d_3, "Среда");
            b.Switch(kk::d_4, "Четверг");
            b.Switch(kk::d_5, "Пятница");
            b.Switch(kk::d_6, "Суббота");
            b.Switch(kk::d_7, "Воскресенье");
            b.endGroup();
        }

        if (b.beginGroup("Время старта полива"))
        {
            b.Slider(kk::tm_hour, "Час старта", 0, 23, 1);
            b.Slider(kk::tm_min, "Минута старта", 0, 59, 1);
            b.endGroup();
        }
        if (b.beginMenu("Ручное управление"))
        {
            if (b.enterMenu())
            {
                stopAction();
                sett.updater().update(H(switch1), RELAY_STATE[0]).update(H(switch2), RELAY_STATE[1]).update(H(switch3), RELAY_STATE[2]).update(H(switch4), RELAY_STATE[3]);
                // db.dump(Serial);
            }

            if (b.Switch(H(switch1), "Реле зоны 1", &RELAY_STATE[0]))
                digitalWrite(RELAY_PINS[0], RELAY_STATE[0]);
            if (b.Switch(H(switch2), "Реле зоны 2", &RELAY_STATE[1]))
                digitalWrite(RELAY_PINS[1], RELAY_STATE[1]);
            if (b.Switch(H(switch3), "Реле зоны 3", &RELAY_STATE[2]))
                digitalWrite(RELAY_PINS[2], RELAY_STATE[2]);
            if (b.Switch(H(switch4), "Реле 24v", &RELAY_STATE[3]))
                digitalWrite(RELAY_PINS[3], RELAY_STATE[3]);

            b.Paragraph("  ВНИМАНИЕ❗", "При входе в это меню, выполняемая в данный момент программа прерывается!");

            // логгер
            b.Log(H(log), logger, "Журнал запусков полива");

            b.endMenu(); // Ручное управление
        }
        b.endMenu(); // Расписание полива
    }
} // build()

void onSyncCallback(uint32_t unix_time)
{
    rtc.setUnix(unix_time);
}

void setup()
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

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_pass);

    sett.begin();
    setStampZone(3);
    sett.rtc.sync(1767214800);

    // Инициализация I2C с указанными пинами
    Wire.end();
    Wire.begin(MY_SDA, MY_SCL);
    // delay(4000);
    // Инициализация RTC
    rtc.begin();
    if (!rtc.isOK())
    {
        Serial.println("Error: DS3231 RTC не найден!");
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
    sett.onBuild(build);
    sett.setVersion(VERSION);
    // установить инфо о проекте (отображается на вкладке настроек и файлов)
    sett.setProjectInfo("Контроллер полива на дачу", "https://github.com/dicson/controller-mexico");
    sett.onFocusChange(onFocusChange);
}

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
            Serial.println("Время расписания пришло. Запуск поочередного полива.");
            already_watered = true;

            // Запуск автомата (он внутри сам проверит чекбоксы)
            startWateringSequence();
        }
    }

    if (cur_min != start_min)
        already_watered = false;
}

void updateStatus()
{
    uint32_t elapsed = millis() - zone_start_millis;
    uint32_t limit = (uint32_t)zone_durations[current_zone] * 60 * 1000;
    if (limit > elapsed)
    {
        uint32_t passed = limit - elapsed;
        uint32_t allSeconds = passed / 1000;
        char buf[40];
        snprintf(buf, sizeof(buf), "ПОЛИВ: Зона %d (%02d:%02d)", current_zone + 1, (allSeconds / 60) % 60, allSeconds % 60);
        status = buf;
        auto u = sett.updater();
        u.update(H(Status), status.c_str());
    }
}

void updateWidgets()
{
    for (int i = 0; i < 3; i++)
    {
        sett.updater().update(RELAY_KEYS[i], RELAY_STATE[i]);
    }
    if (!watering_active)
    {
        // clang-format off
        sett.updater()
            .updateColor(H(Button), sets::Colors::Green)
            .updateText(H(Button), "Запустить полив сейчас");
    }
    else
    {
        sett.updater()
            .updateColor(H(Button), sets::Colors::Red)
            .updateText(H(Button), "ОСТАНОВИТЬ ВСЁ");
        // clang-format on
    }

    if (!rtc_error)
    {
        char buf[40];
        sprintf(buf, "RTC в норме %d °C", rtc.getTempInt());
        alert_f = buf;
        sett.updater().updateText(H(alert), alert_f);
    }

    updateHoldStatus();
}

void loop()
{
    sett.tick();
    blinkRTCErrorLED();

    EVERY_S(1)
    {
        sett.updater().update(H(Time), sett.rtc.toString());

        Serial.println(sett.rtc.toString());

        // 1. Проверяем таймер текущей активной зоны
        if (watering_active && current_zone >= 0 && current_zone < 3)
        {
            uint32_t elapsed = millis() - zone_start_millis;
            uint32_t limit = (uint32_t)zone_durations[current_zone] * 60 * 1000;

            if (elapsed >= limit)
                goToNextZone();
            updateStatus();
        }
        // 2. Проверяем наступление времени старта по расписанию
        if (!watering_active)
            checkSchedule();
    }

    if (timer_focus)
        updateWidgets();
}
