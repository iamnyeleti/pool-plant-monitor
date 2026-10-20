// water_quality.cpp
//
// STEP 3a: turbidity and the DS18B20 temperature sensor (from steps 1 and 2),
// plus the raw pH board voltage. At this step pH is only a voltage in millivolts.
// Converting it to a pH number (calibration) comes in the next step.
//
// Water quality stage 1: per-channel state machine (disabled/unavailable/stale/live).

#include "water_quality.h"
#include <math.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ---------------------------------------------------------------
// Pins
// ---------------------------------------------------------------
static const int TURBIDITY_PIN = 35;      // G35 on the board
static const int ONE_WIRE_PIN = 4;        // G4 on the board: DS18B20 data wire
static const int PH_PIN = 34;             // G34 on the board: pH board signal (0 to 3.0 V, so no divider needed)

static const float DIVIDER_FACTOR = 2.0;  // two equal 10k resistors halve the turbidity voltage,
                                          // so multiplying by 2 gives the sensor's real voltage

static const unsigned long PRINT_INTERVAL_MS = 1000;
static unsigned long lastPrintTime = 0;

// ---------------------------------------------------------------
// Compile-time enable constants (PROVISIONAL — stage 1)
// ---------------------------------------------------------------
static const bool WQ_TEMP_ENABLED     = true;
static const bool WQ_PH_ENABLED       = true;
static const bool WQ_TURBIDITY_ENABLED = true;

// ---------------------------------------------------------------
// Provisional validity ranges
// ---------------------------------------------------------------
static const float WQ_PH_MV_MIN          = 1.0;
static const float WQ_PH_MV_MAX          = 3050.0;
static const float WQ_TURB_VOLTS_MIN     = 0.05;
static const float WQ_TURB_VOLTS_MAX     = 4.6;
static const unsigned long WQ_MAX_AGE_MS = 3000;

// ---------------------------------------------------------------
// Module active flag
// ---------------------------------------------------------------
static bool wqSetupDone = false;

// ---------------------------------------------------------------
// Turbidity averaging (unchanged from step 1)
// ---------------------------------------------------------------
static unsigned long sumMilliVolts = 0;
static unsigned long sampleCount = 0;

// pH board voltage, averaged the same way
static unsigned long phSumMilliVolts = 0;
static unsigned long phSampleCount = 0;

// ---------------------------------------------------------------
// Temperature (DS18B20)
// ---------------------------------------------------------------
static OneWire oneWire(ONE_WIRE_PIN);
static DallasTemperature tempSensor(&oneWire);

static const uint8_t TEMP_RESOLUTION_BITS = 12;          // 12 bits = steps of 0.0625 degrees C
static const unsigned long TEMP_CONVERSION_MS = 750;     // time the chip needs to measure at 12 bits
static const unsigned long TEMP_MARGIN_MS = 50;          // extra wait so we never read too early
static const unsigned long TEMP_PERIOD_MS = 1000;        // start a new measurement once a second

static bool tempFound = false;
static bool tempConverting = false;
static unsigned long tempRequestTime = 0;
static float lastTempC = NAN;
static unsigned long lastTempValidTime = 0;

// ---------------------------------------------------------------
// pH and turbidity latest values + timestamps
// ---------------------------------------------------------------
static float lastPhMv = NAN;
static unsigned long lastPhValidTime = 0;

static float lastTurbVolts = NAN;
static unsigned long lastTurbValidTime = 0;

// ---------------------------------------------------------------
// Per-channel state tracking for event-only logging
// ---------------------------------------------------------------
static const char* prevTempState  = nullptr;
static const char* prevPhState    = nullptr;
static const char* prevTurbState  = nullptr;

// Starts a measurement, then comes back later to collect the result.
// It never waits inside the loop, so the other modules keep their timing.
static void temperatureUpdate() {
  if (!tempFound) return;

  unsigned long now = millis();

  if (!tempConverting) {
    if (now - tempRequestTime >= TEMP_PERIOD_MS) {
      tempSensor.requestTemperatures();   // returns straight away (waiting is switched off in setup)
      tempRequestTime = now;
      tempConverting = true;
    }
    return;
  }

  if (now - tempRequestTime >= TEMP_CONVERSION_MS + TEMP_MARGIN_MS) {
    float t = tempSensor.getTempCByIndex(0);
    tempConverting = false;

    // -127 means the chip did not answer. Exactly 85.00 is the value the chip
    // holds at power-up before it has measured anything, so it is not a real reading.
    if (t == DEVICE_DISCONNECTED_C || t == 85.0) {
      lastTempC = NAN;
    } else {
      lastTempC = t;
      lastTempValidTime = millis();
    }
  }
}

float waterQualityGetTemperatureC() {
  return lastTempC;

}

// ---------------------------------------------------------------
// Setup and update
// ---------------------------------------------------------------
void waterQualitySetup() {
  // 11 dB attenuation lets the ADC measure up to roughly 3.1 V.
  analogSetPinAttenuation(TURBIDITY_PIN, ADC_11db);
  analogSetPinAttenuation(PH_PIN, ADC_11db);

  tempSensor.begin();
  tempFound = (tempSensor.getDeviceCount() > 0);

  if (tempFound) {
    tempSensor.setResolution(TEMP_RESOLUTION_BITS);
    tempSensor.setWaitForConversion(false);   // do not wait inside requestTemperatures()
    Serial.println("DS18B20 temperature sensor found.");
  } else {
    Serial.println("DS18B20 NOT found. Check the three wires and the pull-up resistor on G4.");
  }

  Serial.println("Water quality module ready (turbidity, temperature and raw pH voltage).");

  Serial.print("[WQ] stage1 build ");
  Serial.print(__DATE__);
  Serial.print(" ");
  Serial.print(__TIME__);
  Serial.print(" heap=");
  Serial.println(ESP.getFreeHeap());

  wqSetupDone = true;
}

void waterQualityUpdate() {
  temperatureUpdate();

  sumMilliVolts += analogReadMilliVolts(TURBIDITY_PIN);
  sampleCount++;

  phSumMilliVolts += analogReadMilliVolts(PH_PIN);
  phSampleCount++;

  if (millis() - lastPrintTime >= PRINT_INTERVAL_MS) {
    lastPrintTime = millis();

    float pinVolts = (sumMilliVolts / (float)sampleCount) / 1000.0;
    float sensorVolts = pinVolts * DIVIDER_FACTOR;

    // Update turbidity latest value
    if (WQ_TURBIDITY_ENABLED && sensorVolts >= WQ_TURB_VOLTS_MIN && sensorVolts <= WQ_TURB_VOLTS_MAX) {
      lastTurbVolts = sensorVolts;
      lastTurbValidTime = millis();
    } else if (WQ_TURBIDITY_ENABLED) {
      lastTurbVolts = NAN;
    }

    Serial.print("[Turbidity] At G35 pin: ");
    Serial.print(pinVolts, 3);
    Serial.print(" V | Sensor output: ");
    Serial.print(sensorVolts, 3);
    Serial.print(" V");
    if (pinVolts > 2.3) {
      Serial.print("  <-- TOO HIGH, UNPLUG USB AND CHECK THE DIVIDER");
    }
    Serial.println();

    sumMilliVolts = 0;
    sampleCount = 0;

    if (isnan(lastTempC)) {
      Serial.println("[Temperature] no valid reading yet");
    } else {
      Serial.print("[Temperature] ");
      Serial.print(lastTempC, 2);
      Serial.println(" C");
    }

    float phMilliVolts = phSumMilliVolts / (float)phSampleCount;

    // Update pH latest value
    if (WQ_PH_ENABLED && phMilliVolts >= WQ_PH_MV_MIN && phMilliVolts <= WQ_PH_MV_MAX) {
      lastPhMv = phMilliVolts;
      lastPhValidTime = millis();
    } else if (WQ_PH_ENABLED) {
      lastPhMv = NAN;
    }

    Serial.print("[pH] Board output at G34: ");
    Serial.print(phMilliVolts, 1);
    Serial.print(" mV");
    if (phMilliVolts > 3050.0) {
      Serial.print("  <-- ABOVE 3.0 V, CHECK THE WIRING");
    }
    Serial.println();

    phSumMilliVolts = 0;
    phSampleCount = 0;
  }
}

// ---------------------------------------------------------------
// Stage 1 public API
// ---------------------------------------------------------------

bool waterQualityActive() {
  return wqSetupDone;
}

static const char* computeTempState(const char** reason) {
  if (!WQ_TEMP_ENABLED)           { *reason = "disabled";        return "disabled"; }
  if (!wqSetupDone)               { *reason = "disabled";        return "disabled"; }
  if (!tempFound)                 { *reason = "not_found";       return "unavailable"; }
  if (lastTempValidTime == 0)     { *reason = "no_reading_yet";  return "unavailable"; }
  if (isnan(lastTempC))           { *reason = "invalid_reading"; return "unavailable"; }
  unsigned long age = millis() - lastTempValidTime;
  if (age > WQ_MAX_AGE_MS)       { *reason = "stale";           return "stale"; }
  *reason = nullptr;
  return "live";
}

static const char* computePhState(const char** reason) {
  if (!WQ_PH_ENABLED)            { *reason = "disabled";            return "disabled"; }
  if (!wqSetupDone)              { *reason = "disabled";            return "disabled"; }
  if (lastPhValidTime == 0)      { *reason = "no_reading_yet";     return "unavailable"; }
  if (isnan(lastPhMv))           {
    if (lastPhMv != lastPhMv) {
      *reason = "above_valid_range";
    }
    *reason = "invalid_reading";
    return "unavailable";
  }
  if (lastPhMv < WQ_PH_MV_MIN)  { *reason = "below_valid_range";  return "unavailable"; }
  if (lastPhMv > WQ_PH_MV_MAX)  { *reason = "above_valid_range";  return "unavailable"; }
  unsigned long age = millis() - lastPhValidTime;
  if (age > WQ_MAX_AGE_MS)      { *reason = "stale";              return "stale"; }
  *reason = nullptr;
  return "live";
}

static const char* computeTurbState(const char** reason) {
  if (!WQ_TURBIDITY_ENABLED)          { *reason = "disabled";            return "disabled"; }
  if (!wqSetupDone)                   { *reason = "disabled";            return "disabled"; }
  if (lastTurbValidTime == 0)         { *reason = "no_reading_yet";     return "unavailable"; }
  if (isnan(lastTurbVolts))           { *reason = "invalid_reading";    return "unavailable"; }
  if (lastTurbVolts < WQ_TURB_VOLTS_MIN) { *reason = "below_valid_range";  return "unavailable"; }
  if (lastTurbVolts > WQ_TURB_VOLTS_MAX) { *reason = "above_valid_range";  return "unavailable"; }
  unsigned long age = millis() - lastTurbValidTime;
  if (age > WQ_MAX_AGE_MS)           { *reason = "stale";              return "stale"; }
  *reason = nullptr;
  return "live";
}

static void logStateChange(const char* channel, const char* prev, const char* next, const char* reason) {
  if (prev == nullptr || strcmp(prev, next) != 0) {
    Serial.print("[WQ] ");
    Serial.print(channel);
    Serial.print(": ");
    Serial.print(next);
    if (reason) {
      Serial.print(" (");
      Serial.print(reason);
      Serial.print(")");
    }
    Serial.println();
  }
}

static String nullOrFloat(float v, int decimals) {
  if (isnan(v)) return "null";
  return String(v, decimals);
}

static String nullOrUlong(unsigned long v) {
  if (v == 0) return "null";
  unsigned long age = millis() - v;
  return String(age);
}

String waterQualityStatusJson() {
  const char* tempReason = nullptr;
  const char* phReason = nullptr;
  const char* turbReason = nullptr;

  const char* tempState = computeTempState(&tempReason);
  const char* phState   = computePhState(&phReason);
  const char* turbState = computeTurbState(&turbReason);

  logStateChange("temp", prevTempState, tempState, tempReason);
  logStateChange("ph",   prevPhState,   phState,   phReason);
  logStateChange("turb", prevTurbState, turbState, turbReason);
  prevTempState  = tempState;
  prevPhState    = phState;
  prevTurbState  = turbState;

  bool isLiveTemp = (strcmp(tempState, "live") == 0);
  bool isLivePh   = (strcmp(phState,   "live") == 0);
  bool isLiveTurb = (strcmp(turbState, "live") == 0);

  String tempStatus;
  if (strcmp(tempState, "live") == 0) {
    tempStatus = "NOT_SET";
  } else {
    tempStatus = "UNAVAILABLE";
  }

  String json;
  json.reserve(512);

  json += "{\"active\":";
  json += wqSetupDone ? "true" : "false";
  json += ",\"simulated\":false";

  // temperature
  json += ",\"temperature\":{";
  json += "\"enabled\":";
  json += WQ_TEMP_ENABLED ? "true" : "false";
  json += ",\"state\":\"";
  json += tempState;
  json += "\"";
  if (tempReason) {
    json += ",\"reason\":\"";
    json += tempReason;
    json += "\"";
  } else {
    json += ",\"reason\":null";
  }
  json += ",\"valueC\":";
  json += isLiveTemp ? nullOrFloat(lastTempC, 2) : "null";
  json += ",\"ageMs\":";
  json += (lastTempValidTime != 0) ? nullOrUlong(lastTempValidTime) : "null";
  json += ",\"rangeSet\":false";
  json += ",\"status\":\"";
  json += tempStatus;
  json += "\"}";

  // ph
  json += ",\"ph\":{";
  json += "\"enabled\":";
  json += WQ_PH_ENABLED ? "true" : "false";
  json += ",\"state\":\"";
  json += phState;
  json += "\"";
  if (phReason) {
    json += ",\"reason\":\"";
    json += phReason;
    json += "\"";
  } else {
    json += ",\"reason\":null";
  }
  json += ",\"rawMv\":";
  json += isLivePh ? nullOrFloat(lastPhMv, 1) : "null";
  json += ",\"ageMs\":";
  json += (lastPhValidTime != 0) ? nullOrUlong(lastPhValidTime) : "null";
  json += ",\"calibration\":\"unavailable\"";
  json += ",\"value\":null";
  json += ",\"status\":\"UNAVAILABLE\"}";

  // turbidity
  json += ",\"turbidity\":{";
  json += "\"enabled\":";
  json += WQ_TURBIDITY_ENABLED ? "true" : "false";
  json += ",\"state\":\"";
  json += turbState;
  json += "\"";
  if (turbReason) {
    json += ",\"reason\":\"";
    json += turbReason;
    json += "\"";
  } else {
    json += ",\"reason\":null";
  }
  json += ",\"rawVolts\":";
  json += isLiveTurb ? nullOrFloat(lastTurbVolts, 3) : "null";
  json += ",\"ageMs\":";
  json += (lastTurbValidTime != 0) ? nullOrUlong(lastTurbValidTime) : "null";
  json += ",\"baseline\":\"unavailable\"";
  json += ",\"percentBelowBaseline\":null";
  json += ",\"status\":\"UNAVAILABLE\"}";

  json += "}";
  return json;
}
