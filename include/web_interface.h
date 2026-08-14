#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

#include <SettingsGyverWS.h>
#include "globals.h"

// Функции интерфейса
void build(sets::Builder &b);
void onSyncCallback(uint32_t unix_time);
void updateWidgets();
void updateStatus();
void addLog(String entry);
void endTimeToLog();

extern void startWateringSequence();

#endif // WEB_INTERFACE_H
