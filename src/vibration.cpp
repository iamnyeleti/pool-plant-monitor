
// vibration.cpp
#include "vibration.h"
#include <Wire.h>

// AD0 is wired to 3.3V, shifting the MPU6050 from 0x68 to 0x69,
// since the DS3231 now occupies 0x68 on the same shared bus.
static const uint8_t MPU_ADDR = 0x69;

// Added so the dashboard can access the most recent RMS value.
static float currentRMS = 0.0f;

static const uint8_t DLPF_REG_VALUE = 0x00;

static bool mpuInit() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C);
  Wire.write(0x08);
  if (Wire.endTransmission() != 0) return false;

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1A);
  Wire.write(DLPF_REG_VALUE); // DLPF_CFG=4, ~21Hz, below the measured ~22.6Hz Nyquist limit
  if (Wire.endTransmission() != 0) return false;

  return true;
}

static bool mpuReadAccel(float &x, float &y, float &z) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;

  Wire.requestFrom((int)MPU_ADDR, 6);
  if (Wire.available() < 6) {
    while (Wire.available()) Wire.read();
    return false;
  }

  int16_t rawX = (Wire.read() << 8) | Wire.read();
  int16_t rawY = (Wire.read() << 8) | Wire.read();
  int16_t rawZ = (Wire.read() << 8) | Wire.read();

  // All-zero or all-0xFFFF is what a dead or floating bus returns, not real data.
  if ((rawX == 0 && rawY == 0 && rawZ == 0) ||
      (rawX == -1 && rawY == -1 && rawZ == -1))
    return false;

  const float LSB_PER_G = 8192.0;
  const float G_TO_MS2 = 9.8;

  x = (rawX / LSB_PER_G) * G_TO_MS2;
  y = (rawY / LSB_PER_G) * G_TO_MS2;
  z = (rawZ / LSB_PER_G) * G_TO_MS2;

  return true;
}

static const bool CALIBRATION_MODE = false; // Set to true to enable calibration mode.
static const unsigned long CALIBRATION_WARMUP_MS = 20000;
static const unsigned long CALIBRATION_DURATION_MS = 90000;

static bool calibrationWarmedUp = false;
static bool calibrationDone = false;
static unsigned long calibrationPhaseStart = 0;
static double calibrationSum = 0;
static unsigned long calibrationCount = 0;
static unsigned long lastCalibrationPrint = 0;

// Real measured baseline from the fan calibration run.
static const float BASELINE_RUNNING_RMS = 0.7854;

// Threshold multipliers
static const float CAUTION_MULTIPLIER = 1.2;
static const float WARNING_MULTIPLIER = 1.5;

static float cautionThreshold() {
  return BASELINE_RUNNING_RMS * CAUTION_MULTIPLIER;
}

static float warningThreshold() {
  return BASELINE_RUNNING_RMS * WARNING_MULTIPLIER;
}

static const unsigned long WARNING_DURATION_MS =
    2UL * 60UL * 1000UL;

static const float WARNING_FRACTION_THRESHOLD = 0.5;

#ifndef VIB_STOPPED_STD
#define VIB_STOPPED_STD 0.05f
#endif
// PLACEHOLDER: unvalidated, not measured against a real running/stopped pump.
static const float STOPPED_STD_THRESHOLD = VIB_STOPPED_STD;
static const unsigned long STOPPED_HOLD_MS = 60000UL;
static const float STOPPED_EXIT_FACTOR = 2.0f;

static const int SAMPLE_WINDOW = 50;
static float sampleBuffer[SAMPLE_WINDOW];

static int sampleIndex = 0;
static bool bufferFull = false;

static unsigned long lastPrintTime = 0;
static const unsigned long PRINT_INTERVAL_MS = 250;

static const unsigned long ASSUMED_LOOP_INTERVAL_MS = 20;

static const int ELEVATED_HISTORY_SIZE =
    WARNING_DURATION_MS / ASSUMED_LOOP_INTERVAL_MS;

static bool elevatedHistory[ELEVATED_HISTORY_SIZE];

static int elevatedIndex = 0;
static int elevatedCount = 0;
static bool elevatedHistoryFull = false;

static String currentStatus = "OK";

struct VibrationEvent {
  unsigned long timestamp;
  unsigned long durationMs;
  float peakRMS;
  bool reachedWarning;
};

static const int EVENT_LOG_SIZE = 10;
static VibrationEvent eventLog[EVENT_LOG_SIZE];

static int eventLogIndex = 0;
static bool loggingActiveEvent = false;

static unsigned long activeEventStart = 0;
static float activeEventPeak = 0;
static bool activeEventReachedWarning = false;

static const unsigned long TREND_INTERVAL_MS =
    60UL * 1000UL;

static const int TREND_BUFFER_SIZE = 1440;

static float trendBuffer[TREND_BUFFER_SIZE];

static int trendIndex = 0;
static bool trendBufferFull = false;

static unsigned long lastTrendTime = 0;

static double trendAccumulator = 0;
static unsigned long trendSampleCount = 0;

static float computeRMS() {
  float sumSquares = 0;

  int count = bufferFull
                  ? SAMPLE_WINDOW
                  : sampleIndex;

  if (count == 0)
    return 0;

  for (int i = 0; i < count; i++) {
    sumSquares += sampleBuffer[i] * sampleBuffer[i];
  }

  return sqrt(sumSquares / count);
}

static void logEventEnd() {

  eventLog[eventLogIndex].timestamp =
      activeEventStart;

  eventLog[eventLogIndex].durationMs =
      millis() - activeEventStart;

  eventLog[eventLogIndex].peakRMS =
      activeEventPeak;

  eventLog[eventLogIndex].reachedWarning =
      activeEventReachedWarning;

  eventLogIndex =
      (eventLogIndex + 1) % EVENT_LOG_SIZE;

  loggingActiveEvent = false;
}

static const unsigned long SENSOR_REINIT_MS = 5000;
static const unsigned long SENSOR_TIMEOUT_MS = 2000;
static const unsigned long SENSOR_RECOVER_MS = 1000;
static bool sensorOk = false;
static unsigned long lastInitAttempt = 0;
static unsigned long lastGoodMs = 0;
static unsigned long recoverStart = 0;

// After an outage the RMS window and the 6000-sample WARNING history hold
// pre-outage samples, so both are cleared; WARNING cannot fire until the
// history has refilled with new valid samples.
static void resetSampleHistory() {
  sampleIndex = 0;
  bufferFull = false;
  for (int i = 0; i < ELEVATED_HISTORY_SIZE; i++) elevatedHistory[i] = false;
  elevatedIndex = 0;
  elevatedCount = 0;
  elevatedHistoryFull = false;
}

void vibrationSetup() {

  Wire.begin();

  lastInitAttempt = millis();

  if (mpuInit()) {
    sensorOk = true;
    lastGoodMs = millis();
    Serial.println("Vibration module ready.");
  } else {
    sensorOk = false;
    currentStatus = "SENSOR ERROR";
    Serial.println("[Vibration] SENSOR ERROR: init failed");
  }

  Serial.printf(
      "[CFG] dlpf=0x%02X baseline=%.4f caution_mult=%.2f warning_mult=%.2f "
      "window=%d fraction=%.2f sample_delay_ms=%lu calibration=%d "
      "stopped_std=%.3f stopped_hold_s=%lu build=%s %s\n",
      DLPF_REG_VALUE, BASELINE_RUNNING_RMS, CAUTION_MULTIPLIER,
      WARNING_MULTIPLIER, ELEVATED_HISTORY_SIZE, WARNING_FRACTION_THRESHOLD,
      ASSUMED_LOOP_INTERVAL_MS, CALIBRATION_MODE ? 1 : 0,
      STOPPED_STD_THRESHOLD, STOPPED_HOLD_MS / 1000UL, __DATE__, __TIME__);

  if (CALIBRATION_MODE) {

    Serial.println(
        "=== CALIBRATION MODE ===");

    calibrationPhaseStart =
        millis();
  }
}

static void calibrationUpdate(float rms) {

  unsigned long elapsed =
      millis() - calibrationPhaseStart;

  if (!calibrationWarmedUp) {

    if (elapsed >=
        CALIBRATION_WARMUP_MS) {

      calibrationWarmedUp = true;

      calibrationPhaseStart =
          millis();

      calibrationSum = 0;
      calibrationCount = 0;

      Serial.println(
          "Warm-up done. Recording now...");
    }

    return;
  }

  if (calibrationDone) {

    if (millis() -
            lastCalibrationPrint >=
        2000) {

      lastCalibrationPrint =
          millis();

      Serial.print(
          "===== CALIBRATION COMPLETE ===== "
          "Average RMS: ");

      Serial.println(
          calibrationSum /
              calibrationCount,
          4);
    }

    return;
  }

  calibrationSum += rms;
  calibrationCount++;

  if (millis() -
          lastCalibrationPrint >=
      2000) {

    lastCalibrationPrint =
        millis();

    Serial.print(
        "Calibrating... RMS: ");

    Serial.println(
        rms,
        3);
  }

  if (elapsed >=
      CALIBRATION_DURATION_MS) {

    calibrationDone = true;
  }
}

void vibrationUpdate() {

  float ax, ay, az;
  bool readOk = mpuReadAccel(ax, ay, az);
  unsigned long nowMs = millis();

  if (!sensorOk) {
    if (nowMs - lastInitAttempt >= SENSOR_REINIT_MS) {
      lastInitAttempt = nowMs;
      mpuInit();
    }
    if (!readOk) {
      recoverStart = 0;
      return;
    }
    if (recoverStart == 0) recoverStart = nowMs;
    if (nowMs - recoverStart < SENSOR_RECOVER_MS) return;

    sensorOk = true;
    lastGoodMs = nowMs;
    recoverStart = 0;
    resetSampleHistory();
    currentStatus = "OK";
    Serial.println("[Vibration] sensor recovered");
  } else if (!readOk) {
    if (nowMs - lastGoodMs >= SENSOR_TIMEOUT_MS) {
      sensorOk = false;
      currentStatus = "SENSOR ERROR";
      lastInitAttempt = nowMs;
      recoverStart = 0;
      loggingActiveEvent = false;
      Serial.println("[Vibration] SENSOR ERROR: no valid read for 2000 ms");
    }
    return;
  } else {
    lastGoodMs = nowMs;
  }

  float magnitude =
      sqrt(
          ax * ax +
          ay * ay +
          az * az);

  float vibration =
      magnitude - 9.8;

  sampleBuffer[sampleIndex] =
      vibration;

  sampleIndex++;

  if (sampleIndex >=
      SAMPLE_WINDOW) {

    sampleIndex = 0;
    bufferFull = true;
  }

  float rms =
      computeRMS();

  // Save the latest RMS reading for the dashboard.
  currentRMS = rms;

  if (CALIBRATION_MODE) {

    calibrationUpdate(rms);
    return;
  }

  bool isElevatedNow =
      (rms >
       warningThreshold());

  elevatedCount -=
      elevatedHistory[
          elevatedIndex];

  elevatedHistory[
      elevatedIndex] =
      isElevatedNow;

  elevatedCount +=
      isElevatedNow
          ? 1
          : 0;

  elevatedIndex++;

  if (elevatedIndex >=
      ELEVATED_HISTORY_SIZE) {

    elevatedIndex = 0;
    elevatedHistoryFull =
        true;
  }

  bool warningCondition =
      false;

  if (elevatedHistoryFull) {

    float elevatedFraction =
        (float)elevatedCount /
        ELEVATED_HISTORY_SIZE;

    warningCondition =
        (elevatedFraction >=
         WARNING_FRACTION_THRESHOLD);
  }

  bool aboveCautionNow =
      (rms >
       cautionThreshold());

  if (warningCondition) {

    currentStatus =
        "WARNING";

  } else if (
      aboveCautionNow) {

    currentStatus =
        "CAUTION";

  } else {

    currentStatus =
        "OK";
  }

  if (aboveCautionNow) {

    if (!loggingActiveEvent) {

      loggingActiveEvent =
          true;

      activeEventStart =
          millis();

      activeEventPeak =
          rms;

      activeEventReachedWarning =
          false;

    } else if (
        rms >
        activeEventPeak) {

      activeEventPeak =
          rms;
    }

    if (currentStatus ==
        "WARNING") {

      activeEventReachedWarning =
          true;
    }

  } else if (
      loggingActiveEvent) {

    logEventEnd();
  }

  trendAccumulator +=
      rms;

  trendSampleCount++;

  if (millis() -
          lastTrendTime >=
      TREND_INTERVAL_MS) {

    lastTrendTime =
        millis();

    if (trendSampleCount > 0) {

      trendBuffer[
          trendIndex] =
          trendAccumulator /
          trendSampleCount;

      trendIndex++;

      if (trendIndex >=
          TREND_BUFFER_SIZE) {

        trendIndex = 0;
        trendBufferFull =
            true;
      }
    }

    trendAccumulator =
        0;

    trendSampleCount =
        0;
  }

  if (millis() -
          lastPrintTime >=
      PRINT_INTERVAL_MS) {

    lastPrintTime =
        millis();

    Serial.print(
        "[Vibration] Status: ");

    Serial.print(
        currentStatus);

    Serial.print(
        " | RMS: ");

    Serial.print(
        rms,
        3);

    Serial.print(
        " | Magnitude: ");

    Serial.print(
        magnitude,
        3);

    Serial.print(
        " | X: ");

    Serial.print(
        ax,
        3);

    Serial.print(
        " Y: ");

    Serial.print(
        ay,
        3);

    Serial.print(
        " Z: ");

    Serial.println(
        az,
        3);
  }
}


// ======================================================
// DASHBOARD ACCESS FUNCTIONS
// ======================================================

String vibrationGetStatus() {
  return currentStatus;
}

float vibrationGetRMS() {
  return currentRMS;
}

float vibrationGetBaseline() {
  return BASELINE_RUNNING_RMS;
}

float vibrationGetCautionThreshold() {
  return cautionThreshold();
}

float vibrationGetWarningThreshold() {
  return warningThreshold();
}


// ======================================================
// TREND ACCESS FUNCTIONS
// ======================================================

int vibrationGetTrendCount() {
  return trendBufferFull
             ? TREND_BUFFER_SIZE
             : trendIndex;
}

float vibrationGetTrendPoint(
    int minutesAgo) {

  int count =
      vibrationGetTrendCount();

  if (minutesAgo < 0 ||
      minutesAgo >= count) {

    return -1;
  }

  int idx =
      trendIndex -
      1 -
      minutesAgo;

  if (idx < 0)
    idx +=
        TREND_BUFFER_SIZE;

  return trendBuffer[idx];
}
