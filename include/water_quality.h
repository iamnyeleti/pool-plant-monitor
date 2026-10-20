// water_quality.h
#ifndef WATER_QUALITY_H
#define WATER_QUALITY_H

#include <Arduino.h>

void waterQualitySetup();
void waterQualityUpdate();

// Latest water temperature in degrees Celsius.
// Returns NAN if the sensor is missing or has not given a valid reading yet.
float waterQualityGetTemperatureC();

bool waterQualityActive();
String waterQualityStatusJson();

#endif
