#ifndef VIBRATION_H
#define VIBRATION_H

#include <Arduino.h>

void vibrationSetup();
void vibrationUpdate();
String vibrationGetStatus();

int vibrationGetTrendCount();
float vibrationGetTrendPoint(int minutesAgo);

#endif