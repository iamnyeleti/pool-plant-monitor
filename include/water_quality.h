// water_quality.h
#ifndef WATER_QUALITY_H
#define WATER_QUALITY_H

#include <Arduino.h>

void waterQualitySetup();
void waterQualityUpdate();

// Latest water temperature in degrees Celsius.
// Returns NAN if the sensor is missing or has not given a valid reading yet.
float waterQualityGetTemperatureC();

// Preferred pH setpoint. Refuses NaN or values outside 7.2 to 7.8 (returns false).
// It never changes the pH "status"; it only drives the "preferred" block.
bool waterQualitySetPreferredPh(float value);

// Bench temperature range in degrees C. Refuses NaN or low >= high (returns false).
bool waterQualitySetTempRange(float low, float high);

bool waterQualityActive();
String waterQualityStatusJson();

#endif
