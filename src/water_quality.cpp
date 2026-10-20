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
// pH and turbidity latest values + timestamps + rejection reasons
// ---------------------------------------------------------------
static float lastPhMv = NAN;
static unsigned long lastPhValidTime = 0;
static const char* lastPhRejectReason = nullptr;

static float lastTurbVolts = NAN;
static unsigned long lastTurbValidTime = 0;
static const char* lastTurbRejectReason = nullptr;

// Track whether any reading (valid or rejected) has been attempted
static bool tempReadingAttempted = false;
static bool phReadingAttempted = false;
static bool turbReadingAttempted = false;

// ---------------------------------------------------------------
// Per-channel state tracking for event-only logging
// ---------------------------------------------------------------
static const char* prevTempState  = nullptr;
static const char* prevTempReason = nullptr;
static const char* prevPhState    = nullptr;
static const char* prevPhReason   = nullptr;
static const char* prevTurbState  = nullptr;
static const char* prevTurbReason = nullptr;

// ---------------------------------------------------------------
// Heap diagnostic
// ---------------------------------------------------------------
static const unsigned long HEAP_DIAG_INTERVAL_MS = 60000;
static unsigned long lastHeapDiagTime = 0;

// ---------------------------------------------------------------
// State computation (shared between update logging and JSON)
// ---------------------------------------------------------------

static const char* computeTempState(const char** reason) {
  if (!WQ_TEMP_ENABLED)           { *reason = "disabled";        return "disabled"; }
  if (!wqSetupDone)               { *reason = "module_inactive"; return "disabled"; }
  if (!tempFound)                 { *reason = "not_found";       return "unavailable"; }
  if (!tempReadingAttempted)      { *reason = "no_reading_yet";  return "unavailable"; }
  if (isnan(lastTempC))           { *reason = "invalid_reading"; return "unavailable"; }
  unsigned long age = millis() - lastTempValidTime;
  if (age > WQ_MAX_AGE_MS)       { *reason = "stale";           return "stale"; }
  *reason = nullptr;
  return "live";
}

static const char* computePhState(const char** reason) {
  if (!WQ_PH_ENABLED)            { *reason = "disabled";            return "disabled"; }
  if (!wqSetupDone)              { *reason = "module_inactive";     return "disabled"; }
  if (!phReadingAttempted)       { *reason = "no_reading_yet";     return "unavailable"; }
  if (isnan(lastPhMv)) {
    *reason = lastPhRejectReason ? lastPhRejectReason : "invalid_reading";
    return "unavailable";
  }
  unsigned long age = millis() - lastPhValidTime;
  if (age > WQ_MAX_AGE_MS)      { *reason = "stale";              return "stale"; }
  *reason = nullptr;
  return "live";
}

static const char* computeTurbState(const char** reason) {
  if (!WQ_TURBIDITY_ENABLED)          { *reason = "disabled";            return "disabled"; }
  if (!wqSetupDone)                   { *reason = "module_inactive";     return "disabled"; }
  if (!turbReadingAttempted)          { *reason = "no_reading_yet";     return "unavailable"; }
  if (isnan(lastTurbVolts)) {
    *reason = lastTurbRejectReason ? lastTurbRejectReason : "invalid_reading";
    return "unavailable";
  }
  unsigned long age = millis() - lastTurbValidTime;
  if (age > WQ_MAX_AGE_MS)           { *reason = "stale";              return "stale"; }
  *reason = nullptr;
  return "live";
}

static void logStateChange(const char* channel, const char* prevState, const char* prevRsn,
                           const char* nextState, const char* nextRsn) {
  bool stateChanged = (prevState == nullptr || strcmp(prevState, nextState) != 0);
  bool reasonChanged = false;
  if (!stateChanged && prevRsn && nextRsn) {
    reasonChanged = (strcmp(prevRsn, nextRsn) != 0);
  } else if (!stateChanged) {
    reasonChanged = (prevRsn != nextRsn);
  }
  if (stateChanged || reasonChanged) {
    Serial.print("[WQ] ");
    Serial.print(channel);
    Serial.print(": ");
    Serial.print(nextState);
    if (nextRsn) {
      Serial.print(" (");
      Serial.print(nextRsn);
      Serial.print(")");
    }
    Serial.println();
  }
}

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
    tempReadingAttempted = true;

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

  unsigned long now = millis();

  if (now - lastPrintTime >= PRINT_INTERVAL_MS) {
    lastPrintTime = now;

    float pinVolts = (sumMilliVolts / (float)sampleCount) / 1000.0;
    float sensorVolts = pinVolts * DIVIDER_FACTOR;

    // Update turbidity latest value, preserving rejection reason
    turbReadingAttempted = true;
    if (WQ_TURBIDITY_ENABLED) {
      if (sensorVolts < WQ_TURB_VOLTS_MIN) {
        lastTurbVolts = NAN;
        lastTurbRejectReason = "below_valid_range";
      } else if (sensorVolts > WQ_TURB_VOLTS_MAX) {
        lastTurbVolts = NAN;
        lastTurbRejectReason = "above_valid_range";
      } else {
        lastTurbVolts = sensorVolts;
        lastTurbValidTime = now;
        lastTurbRejectReason = nullptr;
      }
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

    // Update pH latest value, preserving rejection reason
    phReadingAttempted = true;
    if (WQ_PH_ENABLED) {
      if (phMilliVolts < WQ_PH_MV_MIN) {
        lastPhMv = NAN;
        lastPhRejectReason = "below_valid_range";
      } else if (phMilliVolts > WQ_PH_MV_MAX) {
        lastPhMv = NAN;
        lastPhRejectReason = "above_valid_range";
      } else {
        lastPhMv = phMilliVolts;
        lastPhValidTime = now;
        lastPhRejectReason = nullptr;
      }
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

    // State-change logging at reporting interval
    const char* tempReason = nullptr;
    const char* phReason = nullptr;
    const char* turbReason = nullptr;
    const char* tempState = computeTempState(&tempReason);
    const char* phState   = computePhState(&phReason);
    const char* turbState = computeTurbState(&turbReason);

    logStateChange("temp", prevTempState, prevTempReason, tempState, tempReason);
    logStateChange("ph",   prevPhState,   prevPhReason,   phState,   phReason);
    logStateChange("turb", prevTurbState, prevTurbReason, turbState, turbReason);
    prevTempState  = tempState;  prevTempReason = tempReason;
    prevPhState    = phState;    prevPhReason   = phReason;
    prevTurbState  = turbState;  prevTurbReason = turbReason;
  }

  // Heap diagnostic approximately every 60 seconds
  if (now - lastHeapDiagTime >= HEAP_DIAG_INTERVAL_MS) {
    lastHeapDiagTime = now;
    Serial.print("[WQ] heap=");
    Serial.print(ESP.getFreeHeap());
    Serial.print(" minheap=");
    Serial.println(ESP.getMinFreeHeap());
  }
}

// ---------------------------------------------------------------
// Stage 1 public API
// ---------------------------------------------------------------

bool waterQualityActive() {
  return wqSetupDone;
}

static String nullOrFloat(float v, int decimals) {
  if (isnan(v)) return "null";
  return String(v, decimals);
}

static String nullOrUlong(unsigned long validTime) {
  if (validTime == 0) return "null";
  unsigned long age = millis() - validTime;
  return String(age);
}

String waterQualityStatusJson() {
  const char* tempReason = nullptr;
  const char* phReason = nullptr;
  const char* turbReason = nullptr;

  const char* tempState = computeTempState(&tempReason);
  const char* phState   = computePhState(&phReason);
  const char* turbState = computeTurbState(&turbReason);

  bool isLiveTemp = (strcmp(tempState, "live") == 0);
  bool isLivePh   = (strcmp(phState,   "live") == 0);
  bool isLiveTurb = (strcmp(turbState, "live") == 0);

  String tempStatus;
  if (isLiveTemp) {
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
