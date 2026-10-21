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

// Result of a preferences request. ok = valid and applied (or nothing to change),
// changed = at least one value differed, saved = the changed values reached flash.
struct WaterConfigResult {
  bool ok;
  bool changed;
  bool saved;
};

// Validates and applies a temperature range and/or preferred pH. A missing temperature
// end is passed as NAN; hasTemp/hasPh false means "unchanged". Messages are appended to
// *errors, one per line, and any message (including ones already in *errors) blocks the change.
WaterConfigResult waterQualityApplyConfig(bool hasTemp, float low, float high, bool hasPh, float ph, String* errors);

// {"tempSet","tempLow","tempHigh","phSet","phPref"} for the dashboard form.
String waterQualityConfigJson();

bool waterQualityActive();
String waterQualityStatusJson();

#endif
