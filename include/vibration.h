#ifndef VIBRATION_H
#define VIBRATION_H

#include <Arduino.h>


void vibrationSetup();
void vibrationUpdate();
String vibrationGetStatus();

int vibrationGetTrendCount();
float vibrationGetTrendPoint(int minutesAgo);
float vibrationGetRMS();
float vibrationGetBaseline();
float vibrationGetCautionThreshold();
float vibrationGetWarningThreshold();

long vibrationGetLastGoodAgeMs();      // -1 if no valid read yet
bool vibrationPumpWindowSet();
int  vibrationPumpStartMinute();       // minute of day
int  vibrationPumpEndMinute();
int  vibrationPumpWindowState();       // 1 inside window, 0 outside, -1 unknown (no window or no RTC)


#endif