// vibration.h
#ifndef VIBRATION_H
#define VIBRATION_H

#include <Arduino.h>

void vibrationSetup();
void vibrationUpdate();
String vibrationGetStatus();

// For a future dashboard's trend graph. minutesAgo=0 is the most recent
// point, higher numbers reach further back (up to vibrationGetTrendCount()-1).
int vibrationGetTrendCount();
float vibrationGetTrendPoint(int minutesAgo);

#endif