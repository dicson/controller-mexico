#ifndef HARDWARE_H
#define HARDWARE_H

#include <Arduino.h>
#include "globals.h"

// Функции управления реле и логикой полива
void turnOffAllRelays();
void switchRelay(int zone);
void stopAction();
void blinkRTCErrorLED();
void pinLedSetup();
void relayOn();
void rtcSetup();
void printMemoryUsage();

#endif // HARDWARE_H
