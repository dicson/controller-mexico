#include "globals.h"
#include <LittleFS.h>
#include "config.h"
#include "hardware.h"

// Глобальные объекты
GyverDBFile db(&LittleFS, "/data.db");
GyverDS3231 rtc;
SettingsGyverWS sett("Контроллер полива", &db);
GTimer<millis> timer_focus(1500, false, GTMode::Timeout);
sets::Logger logger(512);
const size_t LED_NAMES[4] = {H(relay1), H(relay2), H(relay3), H(relay4)};

// Глобальные переменные состояния
String alert_f;
String status;
bool rtc_error = false;
bool RELAY_STATE[4] = {0};
bool already_watered = false;
bool watering_active = false;
uint32_t zone_start_millis = 0;
int zone_durations[3] = {0, 0, 0};
int current_zone = -1;

void dbSetup()
{
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
}
