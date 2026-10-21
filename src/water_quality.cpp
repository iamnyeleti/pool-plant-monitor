// water_quality.cpp
//
// STEP 3a: turbidity and the DS18B20 temperature sensor (from steps 1 and 2),
// plus the raw pH board voltage.
//
// Water quality stage 1: per-channel state machine (disabled/unavailable/stale/live).
// Water quality stage 2: two-point pH calibration, pH zones with a 30 s hold,
// preferred pH, temperature range with a 30 s hold.
// Water quality stage 3: turbidity baseline and percent below baseline (no NTU).

#include "water_quality.h"
#include <math.h>
#include <string.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#define WQ_STAGE_NAME "stage3"

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
// Compile-time enable constants (PROVISIONAL)
// ---------------------------------------------------------------
static const bool WQ_TEMP_ENABLED     = true;
static const bool WQ_PH_ENABLED       = true;
static const bool WQ_TURBIDITY_ENABLED = true;

// ---------------------------------------------------------------
// Provisional validity ranges (electrical plausibility only)
// ---------------------------------------------------------------
static const float WQ_PH_MV_MIN          = 1.0;
static const float WQ_PH_MV_MAX          = 3050.0;
static const float WQ_TURB_VOLTS_MIN     = 0.05;
static const float WQ_TURB_VOLTS_MAX     = 4.6;
static const unsigned long WQ_MAX_AGE_MS = 3000;

// ---------------------------------------------------------------
// Stage 2 constants (all PROVISIONAL)
// ---------------------------------------------------------------
static const unsigned long WQ_HOLD_MS    = 30000;   // a new zone/status must persist this long
static const int   CAL_SAMPLES           = 30;      // one-second readings per calibration point
static const float PH_CAL_MAX_SPREAD_MV  = 5.0;     // max-min allowed across the 30 samples
static const float PH7_WINDOW_MIN_MV     = 1322.0;  // DFRobot window for the pH 7.0 buffer
static const float PH7_WINDOW_MAX_MV     = 1678.0;
static const float PH4_WINDOW_MIN_MV     = 1854.0;  // DFRobot window for the pH 4.0 buffer
static const float PH4_WINDOW_MAX_MV     = 2210.0;
static const float PH_SLOPE_MIN_MV       = 150.0;   // accepted slope, mV per pH unit
static const float PH_SLOPE_MAX_MV       = 190.0;
static const float PH_LIMIT_LOW          = 7.2;     // fixed limits, inclusive
static const float PH_LIMIT_HIGH         = 7.8;
static const float PH_CAUTION_MARGIN     = 0.2;     // CAUTION within this of a limit
static const float PH_MIDPOINT           = 7.5;     // LOW/HIGH side
static const float PH_EDGE_EPS           = 0.0005;  // float tolerance on inclusive boundaries
static const float PH_PREFERRED_MARGIN   = 0.2;     // ON_TARGET within this of preferred

// Stage 3 constants (all PROVISIONAL)
static const float TURB_CLOUDY_PCT       = 5.0;     // CLOUDIER when percent below baseline >= this
static const float TURB_PCT_EPS          = 0.001;   // float tolerance on the inclusive threshold
static const float TURB_BASELINE_MAX_SPREAD_V = 0.05; // max-min allowed across the 30 baseline samples

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
// Ring of the last CAL_SAMPLES one-second readings
// ---------------------------------------------------------------
struct Ring {
  float v[CAL_SAMPLES];
  bool ok[CAL_SAMPLES];
  uint8_t head;
  uint8_t n;
};

static Ring phRing = {};
static Ring turbRing = {};

static void ringPush(Ring& r, float v, bool ok) {
  r.v[r.head] = v;
  r.ok[r.head] = ok;
  r.head = (r.head + 1) % CAL_SAMPLES;
  if (r.n < CAL_SAMPLES) r.n++;
}

// True only when the ring holds CAL_SAMPLES readings and all are valid.
static bool ringStats(const Ring& r, float* mean, float* spread) {
  if (r.n < CAL_SAMPLES) return false;
  float sum = 0, lo = r.v[0], hi = r.v[0];
  for (int i = 0; i < CAL_SAMPLES; i++) {
    if (!r.ok[i]) return false;
    sum += r.v[i];
    if (r.v[i] < lo) lo = r.v[i];
    if (r.v[i] > hi) hi = r.v[i];
  }
  *mean = sum / CAL_SAMPLES;
  *spread = hi - lo;
  return true;
}

// ---------------------------------------------------------------
// 30 s hold: the reported status changes only after a new key has persisted
// ---------------------------------------------------------------
struct Hold {
  bool held;
  int heldKey;
  bool pending;
  int pendKey;
  unsigned long pendSince;
};

static void holdReset(Hold& h) { h.held = false; h.pending = false; }

// Returns true when the held key changed (or was first adopted).
static bool holdUpdate(Hold& h, bool live, int key, unsigned long now) {
  if (!live) { h.pending = false; return false; }          // timer resets when not live
  if (!h.held) { h.held = true; h.heldKey = key; h.pending = false; return true; }
  if (key == h.heldKey) { h.pending = false; return false; }
  if (!h.pending || h.pendKey != key) {
    h.pending = true;
    h.pendKey = key;
    h.pendSince = now;
    return false;
  }
  if (now - h.pendSince >= WQ_HOLD_MS) {
    h.heldKey = key;
    h.pending = false;
    return true;
  }
  return false;
}

static Hold phHold = {};
static Hold tempHold = {};
static Hold turbHold = {};

static bool turbBaselineSet = false;
static float turbBaselineV = NAN;        // sensor-side volts

static const char* const TURB_STATUS_NAMES[] = {"CLEAR", "CLOUDIER"};

// ---------------------------------------------------------------
// pH calibration and preferred value
// ---------------------------------------------------------------
static bool ph7Set = false;
static bool ph4Set = false;
static float ph7Mv = NAN;
static float ph4Mv = NAN;
static float phTcal = NAN;               // temperature when the 7.0 point was stored
static unsigned long phCalTime = 0;      // millis() when the latest point was stored
static bool phCalTimeSet = false;

static bool prefPhSet = false;
static float prefPh = NAN;

static bool tempRangeSet = false;
static float tempRangeLow = NAN;
static float tempRangeHigh = NAN;

static const char* const PH_ZONE_NAMES[] = {"OK", "CAUTION", "WARNING"};
static const char* const PH_DIR_NAMES[]  = {"OK", "LOW", "HIGH"};
static const char* const TEMP_STATUS_NAMES[] = {"IN_RANGE", "BELOW_RANGE", "ABOVE_RANGE"};

// ---------------------------------------------------------------
// State computation (shared by update-loop logging and the JSON builder)
// ---------------------------------------------------------------

static bool isLive(const char* state) { return strcmp(state, "live") == 0; }

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

// Live water temperature, or NAN when the temperature channel is not live.
static float tempLiveC() {
  const char* r;
  return isLive(computeTempState(&r)) ? lastTempC : NAN;
}

// ---------------------------------------------------------------
// pH conversion and zones
// ---------------------------------------------------------------

// Two-point conversion with temperature scaling of the slope.
// assumed = true when the live temperature (or the calibration temperature) was not available.
static float phConvert(float mv, float mv7, float mv4, float tcal, float tlive, bool* assumed) {
  float slope = (mv4 - mv7) / 3.0;
  float factor = 1.0;
  if (isnan(tcal)) {
    *assumed = true;
  } else if (isnan(tlive)) {
    *assumed = true;
  } else {
    *assumed = false;
    factor = (273.15 + tlive) / (273.15 + tcal);
  }
  return 7.0 - (mv - mv7) / (slope * factor);
}

static int phCalLevel() {
  if (ph7Set && ph4Set) return 2;
  if (ph7Set || ph4Set) return 1;
  return 0;
}

// Current pH number from the latest valid reading; false when it cannot be given.
static bool phValueNow(float* v, bool* assumed) {
  if (phCalLevel() != 2) return false;
  *v = phConvert(lastPhMv, ph7Mv, ph4Mv, phTcal, tempLiveC(), assumed);
  return true;
}

// zone: 0 OK, 1 CAUTION, 2 WARNING. dir: 0 OK, 1 LOW, 2 HIGH.
static void phClassify(float v, int* zone, int* dir) {
  if (v < PH_LIMIT_LOW - PH_EDGE_EPS || v > PH_LIMIT_HIGH + PH_EDGE_EPS) {
    *zone = 2;
  } else if (v <= PH_LIMIT_LOW + PH_CAUTION_MARGIN + PH_EDGE_EPS ||
             v >= PH_LIMIT_HIGH - PH_CAUTION_MARGIN - PH_EDGE_EPS) {
    *zone = 1;
  } else {
    *zone = 0;
  }
  if (*zone == 0)            *dir = 0;
  else if (v < PH_MIDPOINT)  *dir = 1;
  else                       *dir = 2;
}

// Percent below baseline; negative when clearer than the baseline.
static float turbPercentBelow(float volts) {
  return (turbBaselineV - volts) / turbBaselineV * 100.0;
}

static int turbClassify(float volts) {
  return turbPercentBelow(volts) >= TURB_CLOUDY_PCT - TURB_PCT_EPS ? 1 : 0;
}

static int tempClassify(float t) {
  if (t < tempRangeLow)  return 1;
  if (t > tempRangeHigh) return 2;
  return 0;
}

// ---------------------------------------------------------------
// Calibration keys
// ---------------------------------------------------------------

static void calCapture(bool is7) {
  const char* name = is7 ? "pH7" : "pH4";
  float mean, spread;
  if (!ringStats(phRing, &mean, &spread)) {
    Serial.printf("[CAL] rejected %s reason=need_%d_valid_samples\n", name, CAL_SAMPLES);
    return;
  }
  if (spread > PH_CAL_MAX_SPREAD_MV) {
    Serial.printf("[CAL] rejected %s reason=unstable_spread_%.1f_mV_over_%.1f\n", name, spread, PH_CAL_MAX_SPREAD_MV);
    return;
  }
  float lo = is7 ? PH7_WINDOW_MIN_MV : PH4_WINDOW_MIN_MV;
  float hi = is7 ? PH7_WINDOW_MAX_MV : PH4_WINDOW_MAX_MV;
  if (mean < lo || mean > hi) {
    Serial.printf("[CAL] rejected %s reason=mean_%.1f_mV_outside_%.0f_to_%.0f\n", name, mean, lo, hi);
    return;
  }
  bool haveOther = is7 ? ph4Set : ph7Set;
  float mv7n = is7 ? mean : ph7Mv;
  float mv4n = is7 ? ph4Mv : mean;
  float slope = 0;
  if (haveOther) {
    slope = (mv4n - mv7n) / 3.0;
    if (slope < PH_SLOPE_MIN_MV || slope > PH_SLOPE_MAX_MV) {
      Serial.printf("[CAL] rejected %s reason=slope_%.1f_mV_per_pH_outside_%.0f_to_%.0f\n", name, slope, PH_SLOPE_MIN_MV, PH_SLOPE_MAX_MV);
      return;
    }
  }
  float t = tempLiveC();
  if (is7) {
    ph7Mv = mean;
    ph7Set = true;
    phTcal = t;
  } else {
    ph4Mv = mean;
    ph4Set = true;
  }
  phCalTime = millis();
  phCalTimeSet = true;
  holdReset(phHold);
  Serial.printf("[CAL] stored %s mean=%.1f mV spread=%.1f mV", name, mean, spread);
  if (is7) {
    if (isnan(t)) Serial.print(" Tcal=unavailable");
    else Serial.printf(" Tcal=%.2f C", t);
  }
  Serial.println();
  if (haveOther) Serial.printf("[CAL] two_point slope=%.1f mV/pH\n", slope);
}

static void calReset() {
  ph7Set = false;
  ph4Set = false;
  ph7Mv = NAN;
  ph4Mv = NAN;
  phTcal = NAN;
  phCalTimeSet = false;
  holdReset(phHold);
  Serial.println("[CAL] reset: pH calibration cleared");
}

static void baselineCapture() {
  float mean, spread;
  if (!ringStats(turbRing, &mean, &spread)) {
    Serial.printf("[CAL] rejected turbidity baseline reason=need_%d_valid_samples\n", CAL_SAMPLES);
    return;
  }
  if (spread > TURB_BASELINE_MAX_SPREAD_V) {
    Serial.printf("[CAL] rejected turbidity baseline reason=unstable_spread_%.3f_V_over_%.3f\n", spread, TURB_BASELINE_MAX_SPREAD_V);
    return;
  }
  turbBaselineV = mean;
  turbBaselineSet = true;
  holdReset(turbHold);
  Serial.printf("[CAL] stored turbidity baseline mean=%.3f V spread=%.3f V\n", mean, spread);
}

static void handleKey(int c) {
  switch (c) {
    case '7': calCapture(true);  break;
    case '4': calCapture(false); break;
    case 'r': calReset();        break;
    case 'b': baselineCapture(); break;
    default: break;
  }
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

// ---------------------------------------------------------------
// Classification tick (once per reporting interval)
// ---------------------------------------------------------------
static void classifyTick(unsigned long now) {
  const char* r;

  // pH
  bool phLive = isLive(computePhState(&r));
  float v = NAN;
  bool assumed = false;
  bool have = phLive && phValueNow(&v, &assumed);
  int zone = 0, dir = 0;
  if (have) phClassify(v, &zone, &dir);
  if (holdUpdate(phHold, have, zone * 4 + dir, now)) {
    Serial.printf("[WQ] ph status: %s %s (pH %.2f)\n", PH_ZONE_NAMES[phHold.heldKey / 4], PH_DIR_NAMES[phHold.heldKey % 4], v);
  }

  // Temperature
  bool tLive = isLive(computeTempState(&r)) && tempRangeSet;
  if (holdUpdate(tempHold, tLive, tLive ? tempClassify(lastTempC) : 0, now)) {
    Serial.printf("[WQ] temperature status: %s (%.2f C)\n", TEMP_STATUS_NAMES[tempHold.heldKey], lastTempC);
  }

  // Turbidity
  bool uLive = isLive(computeTurbState(&r)) && turbBaselineSet;
  if (holdUpdate(turbHold, uLive, uLive ? turbClassify(lastTurbVolts) : 0, now)) {
    Serial.printf("[WQ] turbidity status: %s (%.1f%% below baseline)\n", TURB_STATUS_NAMES[turbHold.heldKey], turbPercentBelow(lastTurbVolts));
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

bool waterQualitySetPreferredPh(float value) {
  if (isnan(value) || value < PH_LIMIT_LOW || value > PH_LIMIT_HIGH) return false;
  prefPh = value;
  prefPhSet = true;
  return true;
}

bool waterQualitySetTempRange(float low, float high) {
  if (isnan(low) || isnan(high) || low >= high) return false;
  tempRangeLow = low;
  tempRangeHigh = high;
  tempRangeSet = true;
  holdReset(tempHold);
  return true;
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

  Serial.print("[WQ] " WQ_STAGE_NAME " build ");
  Serial.print(__DATE__);
  Serial.print(" ");
  Serial.print(__TIME__);
  Serial.print(" heap=");
  Serial.println(ESP.getFreeHeap());

  wqSetupDone = true;
}

void waterQualityUpdate() {
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c == '\r' || c == '\n') continue;
    handleKey(c);
  }

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

    ringPush(turbRing, sensorVolts, WQ_TURBIDITY_ENABLED && sensorVolts >= WQ_TURB_VOLTS_MIN && sensorVolts <= WQ_TURB_VOLTS_MAX);

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
    bool phInRange = false;
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
        phInRange = true;
      }
    }
    ringPush(phRing, phMilliVolts, phInRange);

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

    classifyTick(now);
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
// Public API
// ---------------------------------------------------------------

bool waterQualityActive() {
  return wqSetupDone;
}

// JSON helpers. Read-only: they never change monitoring state.
static void jf(String& j, float v, int dec) {
  if (isnan(v)) { j += "null"; return; }
  char b[24];
  snprintf(b, sizeof(b), "%.*f", dec, (double)v);
  j += b;
}

static void js(String& j, const char* s) {
  if (!s) { j += "null"; return; }
  j += '"';
  j += s;
  j += '"';
}

static void jb(String& j, bool v) { j += v ? "true" : "false"; }

static void jage(String& j, unsigned long validTime) {
  if (validTime == 0) { j += "null"; return; }
  char b[16];
  snprintf(b, sizeof(b), "%lu", millis() - validTime);
  j += b;
}

static void jstate(String& j, bool enabled, const char* state, const char* reason) {
  j += "\"enabled\":";
  jb(j, enabled);
  j += ",\"state\":";
  js(j, state);
  j += ",\"reason\":";
  js(j, reason);
}

String waterQualityStatusJson() {
  const char* tempReason = nullptr;
  const char* phReason = nullptr;
  const char* turbReason = nullptr;

  const char* tempState = computeTempState(&tempReason);
  const char* phState   = computePhState(&phReason);
  const char* turbState = computeTurbState(&turbReason);

  bool liveTemp = isLive(tempState);
  bool livePh   = isLive(phState);
  bool liveTurb = isLive(turbState);

  String j;
  j.reserve(1280);

  j += "{\"active\":";
  jb(j, wqSetupDone);
  j += ",\"simulated\":false";

  // ---- temperature ----
  j += ",\"temperature\":{";
  jstate(j, WQ_TEMP_ENABLED, tempState, tempReason);
  j += ",\"valueC\":";
  jf(j, liveTemp ? lastTempC : NAN, 2);
  j += ",\"ageMs\":";
  jage(j, lastTempValidTime);
  j += ",\"rangeSet\":";
  jb(j, tempRangeSet);
  j += ",\"low\":";
  jf(j, tempRangeSet ? tempRangeLow : NAN, 1);
  j += ",\"high\":";
  jf(j, tempRangeSet ? tempRangeHigh : NAN, 1);
  const char* tempNow = (liveTemp && tempRangeSet) ? TEMP_STATUS_NAMES[tempClassify(lastTempC)] : nullptr;
  j += ",\"statusNow\":";
  js(j, tempNow);
  const char* tempStatus = "UNAVAILABLE";
  if (liveTemp) {
    if (!tempRangeSet) tempStatus = "NOT_SET";
    else if (tempHold.held) tempStatus = TEMP_STATUS_NAMES[tempHold.heldKey];
  }
  j += ",\"status\":";
  js(j, tempStatus);
  j += "}";

  // ---- pH ----
  float pv = NAN;
  bool assumed = false;
  bool havePh = livePh && phValueNow(&pv, &assumed);
  int zone = 0, dir = 0;
  if (havePh) phClassify(pv, &zone, &dir);

  const char* phStatus = "UNAVAILABLE";
  const char* phStatusReason = phReason;
  const char* phDir = nullptr;
  if (livePh && !havePh) {
    phStatusReason = "calibration_unavailable";
  } else if (havePh && !phHold.held) {
    phStatusReason = "no_status_yet";
  } else if (havePh) {
    phStatus = PH_ZONE_NAMES[phHold.heldKey / 4];
    phDir = PH_DIR_NAMES[phHold.heldKey % 4];
    phStatusReason = nullptr;
  }
  bool possibleBreach = havePh && phHold.held && zone == 2 && (phHold.heldKey / 4) != 2;
  int cal = phCalLevel();

  j += ",\"ph\":{";
  jstate(j, WQ_PH_ENABLED, phState, phReason);
  j += ",\"rawMv\":";
  jf(j, livePh ? lastPhMv : NAN, 1);
  j += ",\"ageMs\":";
  jage(j, lastPhValidTime);
  j += ",\"calibration\":";
  js(j, cal == 2 ? "two_point" : (cal == 1 ? "partial" : "unavailable"));
  j += ",\"calibrationAgeS\":";
  if (phCalTimeSet) {
    char b[16];
    snprintf(b, sizeof(b), "%lu", (millis() - phCalTime) / 1000UL);
    j += b;
  } else {
    j += "null";
  }
  j += ",\"value\":";
  jf(j, havePh ? pv : NAN, 2);
  j += ",\"status\":";
  js(j, phStatus);
  j += ",\"statusReason\":";
  js(j, phStatusReason);
  j += ",\"direction\":";
  js(j, phDir);
  j += ",\"zoneNow\":";
  js(j, havePh ? PH_ZONE_NAMES[zone] : nullptr);
  j += ",\"possibleBreach\":";
  jb(j, possibleBreach);
  j += ",\"tempCompensation\":";
  js(j, havePh ? (assumed ? "assumed" : "live") : nullptr);

  const char* prefStatus = "NOT_SET";
  float prefDev = NAN;
  if (prefPhSet) {
    if (havePh) {
      prefDev = pv - prefPh;
      prefStatus = (fabsf(prefDev) <= PH_PREFERRED_MARGIN + PH_EDGE_EPS) ? "ON_TARGET" : "OFF_TARGET";
    } else {
      prefStatus = "UNAVAILABLE";
    }
  }
  j += ",\"preferred\":{\"set\":";
  jb(j, prefPhSet);
  j += ",\"value\":";
  jf(j, prefPhSet ? prefPh : NAN, 2);
  j += ",\"deviation\":";
  jf(j, prefDev, 2);
  j += ",\"status\":";
  js(j, prefStatus);
  j += "}}";

  // ---- turbidity ----
  j += ",\"turbidity\":{";
  jstate(j, WQ_TURBIDITY_ENABLED, turbState, turbReason);
  j += ",\"rawVolts\":";
  jf(j, liveTurb ? lastTurbVolts : NAN, 3);
  j += ",\"ageMs\":";
  jage(j, lastTurbValidTime);
  j += ",\"baseline\":";
  js(j, turbBaselineSet ? "set" : "unavailable");
  j += ",\"baselineVolts\":";
  jf(j, turbBaselineSet ? turbBaselineV : NAN, 3);
  bool haveTurb = liveTurb && turbBaselineSet;
  j += ",\"percentBelowBaseline\":";
  jf(j, haveTurb ? turbPercentBelow(lastTurbVolts) : NAN, 2);
  j += ",\"statusNow\":";
  js(j, haveTurb ? TURB_STATUS_NAMES[turbClassify(lastTurbVolts)] : nullptr);
  const char* turbStatus = "UNAVAILABLE";
  if (liveTurb) {
    if (!turbBaselineSet) turbStatus = "NO_BASELINE";
    else if (turbHold.held) turbStatus = TURB_STATUS_NAMES[turbHold.heldKey];
  }
  j += ",\"status\":";
  js(j, turbStatus);
  j += "}";

  j += "}";
  return j;
}
