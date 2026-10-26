#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <Arduino.h>

void scheduleSetup();
void scheduleUpdate();
bool scheduleIsReady();
String scheduleGetTimeString();
bool scheduleGetMinuteOfDay(int* minuteOfDay);
bool scheduleSetTime(int year, int month, int day, int hour, int minute, int second);
#endif