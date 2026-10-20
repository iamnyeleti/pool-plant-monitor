// water_quality.h
#ifndef WATER_QUALITY_H
#define WATER_QUALITY_H

#include <Arduino.h>

void waterQualitySetup();
void waterQualityUpdate();

float waterQualityGetTemperatureC();

bool waterQualityActive();
String waterQualityStatusJson();

#endif
