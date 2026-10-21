#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <Arduino.h>

void scheduleSetup();
void scheduleUpdate();
bool scheduleIsReady();
String scheduleGetTimeString();
bool scheduleGetMinuteOfDay(int* minuteOfDay);
#endif