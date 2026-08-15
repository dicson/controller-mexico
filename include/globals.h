#ifndef GLOBALS_H
#define GLOBALS_H

#include <GyverDBFile.h>
#include <SettingsGyverWS.h>
#include <GyverDS3231.h>
#include <GTimer.h>

// Глобальные объекты
extern GyverDBFile db;
extern GyverDS3231 rtc;
extern SettingsGyverWS sett;
extern GTimer<millis> timer_focus;
extern sets::Logger logger;
extern const size_t LED_NAMES[4];

// Глобальные переменные состояния
extern String alert_f;
extern String status;
extern bool rtc_error;
extern bool RELAY_STATE[4];
extern bool already_watered;
extern bool watering_active;
extern uint32_t zone_start_millis;
extern int zone_durations[3];
extern int current_zone;

// Прототипы функций, используемых глобально
void updateStatus();
void updateWidgets();
void updateHoldStatus();
void dbSetup();

#endif // GLOBALS_H
