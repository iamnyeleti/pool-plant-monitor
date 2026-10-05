// vibration.cpp
#include "vibration.h"
#include <Wire.h>

// AD0 is wired to 3.3V, shifting the MPU6050 from 0x68 to 0x69,
// since the DS3231 now occupies 0x68 on the same shared bus.
static const uint8_t MPU_ADDR = 0x69;

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
  Wire.write(0x00); // widest bandwidth, minimal filtering
  if (Wire.endTransmission() != 0) return false;

  return true;
}

static bool mpuReadAccel(float &x, float &y, float &z) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;

  Wire.requestFrom((int)MPU_ADDR, 6);
  if (Wire.available() < 6) return false;

  int16_t rawX = (Wire.read() << 8) | Wire.read();
  int16_t rawY = (Wire.read() << 8) | Wire.read();
  int16_t rawZ = (Wire.read() << 8) | Wire.read();

  const float LSB_PER_G = 8192.0;
  const float G_TO_MS2 = 9.8;

  x = (rawX / LSB_PER_G) * G_TO_MS2;
  y = (rawY / LSB_PER_G) * G_TO_MS2;
  z = (rawZ / LSB_PER_G) * G_TO_MS2;
  return true;
}

static const bool CALIBRATION_MODE = false;
static const unsigned long CALIBRATION_WARMUP_MS = 20000;
static const unsigned long CALIBRATION_DURATION_MS = 90000;

static bool calibrationWarmedUp = false;
static bool calibrationDone = false;
static unsigned long calibrationPhaseStart = 0;
static double calibrationSum = 0;
static unsigned long calibrationCount = 0;
static unsigned long lastCalibrationPrint = 0;

// Real measured baseline from the fan calibration run.
static const float BASELINE_RUNNING_RMS = 1.0747;

static const float CAUTION_MULTIPLIER = 1.2;
static const float WARNING_MULTIPLIER = 1.5;
static float cautionThreshold() { return BASELINE_RUNNING_RMS * CAUTION_MULTIPLIER; }
static float warningThreshold() { return BASELINE_RUNNING_RMS * WARNING_MULTIPLIER; }

static const unsigned long WARNING_DURATION_MS = 2UL * 60UL * 1000UL;
static const float WARNING_FRACTION_THRESHOLD = 0.5;

static const int SAMPLE_WINDOW = 50;
static float sampleBuffer[SAMPLE_WINDOW];
static int sampleIndex = 0;
static bool bufferFull = false;

static unsigned long lastPrintTime = 0;
static const unsigned long PRINT_INTERVAL_MS = 250;

static const unsigned long ASSUMED_LOOP_INTERVAL_MS = 20;
static const int ELEVATED_HISTORY_SIZE = WARNING_DURATION_MS / ASSUMED_LOOP_INTERVAL_MS;
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

static const unsigned long TREND_INTERVAL_MS = 60UL * 1000UL;
static const int TREND_BUFFER_SIZE = 1440;
static float trendBuffer[TREND_BUFFER_SIZE];
static int trendIndex = 0;
static bool trendBufferFull = false;
static unsigned long lastTrendTime = 0;
static double trendAccumulator = 0;
static unsigned long trendSampleCount = 0;

static float computeRMS() {
  float sumSquares = 0;
  int count = bufferFull ? SAMPLE_WINDOW : sampleIndex;
  if (count == 0) return 0;
  for (int i = 0; i < count; i++) {
    sumSquares += sampleBuffer[i] * sampleBuffer[i];
  }
  return sqrt(sumSquares / count);
}

static void logEventEnd() {
  eventLog[eventLogIndex].timestamp = activeEventStart;
  eventLog[eventLogIndex].durationMs = millis() - activeEventStart;
  eventLog[eventLogIndex].peakRMS = activeEventPeak;
  eventLog[eventLogIndex].reachedWarning = activeEventReachedWarning;
  eventLogIndex = (eventLogIndex + 1) % EVENT_LOG_SIZE;
  loggingActiveEvent = false;
}

void vibrationSetup() {
  Wire.begin();

  if (!mpuInit()) {
    Serial.println("MPU6050 not found. Check wiring (SDA/SCL) and power.");
    while (1) delay(10);
  }
  Serial.println("Vibration module ready.");

  if (CALIBRATION_MODE) {
    Serial.println("=== CALIBRATION MODE ===");
    calibrationPhaseStart = millis();
  }
}

static void calibrationUpdate(float rms) {
  unsigned long elapsed = millis() - calibrationPhaseStart;
  if (!calibrationWarmedUp) {
    if (elapsed >= CALIBRATION_WARMUP_MS) {
      calibrationWarmedUp = true;
      calibrationPhaseStart = millis();
      calibrationSum = 0;
      calibrationCount = 0;
      Serial.println("Warm-up done. Recording now...");
    }
    return;
  }
  if (calibrationDone) {
    if (millis() - lastCalibrationPrint >= 2000) {
      lastCalibrationPrint = millis();
      Serial.print("===== CALIBRATION COMPLETE ===== Average RMS: ");
      Serial.println(calibrationSum / calibrationCount, 4);
    }
    return;
  }
  calibrationSum += rms;
  calibrationCount++;
  if (millis() - lastCalibrationPrint >= 2000) {
    lastCalibrationPrint = millis();
    Serial.print("Calibrating... RMS: ");
    Serial.println(rms, 3);
  }
  if (elapsed >= CALIBRATION_DURATION_MS) calibrationDone = true;
}

void vibrationUpdate() {
  float ax, ay, az;
  if (!mpuReadAccel(ax, ay, az)) {
    return;
  }

  float magnitude = sqrt(ax * ax + ay * ay + az * az);
  float vibration = magnitude - 9.8;

  sampleBuffer[sampleIndex] = vibration;
  sampleIndex++;
  if (sampleIndex >= SAMPLE_WINDOW) {
    sampleIndex = 0;
    bufferFull = true;
  }

  float rms = computeRMS();

  if (CALIBRATION_MODE) {
    calibrationUpdate(rms);
    return;
  }

  bool isElevatedNow = (rms > warningThreshold());
  elevatedCount -= elevatedHistory[elevatedIndex];
  elevatedHistory[elevatedIndex] = isElevatedNow;
  elevatedCount += isElevatedNow ? 1 : 0;
  elevatedIndex++;
  if (elevatedIndex >= ELEVATED_HISTORY_SIZE) {
    elevatedIndex = 0;
    elevatedHistoryFull = true;
  }

  bool warningCondition = false;
  if (elevatedHistoryFull) {
    float elevatedFraction = (float)elevatedCount / ELEVATED_HISTORY_SIZE;
    warningCondition = (elevatedFraction >= WARNING_FRACTION_THRESHOLD);
  }

  bool aboveCautionNow = (rms > cautionThreshold());

  if (warningCondition) {
    currentStatus = "WARNING";
  } else if (aboveCautionNow) {
    currentStatus = "CAUTION";
  } else {
    currentStatus = "OK";
  }

  if (aboveCautionNow) {
    if (!loggingActiveEvent) {
      loggingActiveEvent = true;
      activeEventStart = millis();
      activeEventPeak = rms;
      activeEventReachedWarning = false;
    } else if (rms > activeEventPeak) {
      activeEventPeak = rms;
    }
    if (currentStatus == "WARNING") activeEventReachedWarning = true;
  } else if (loggingActiveEvent) {
    logEventEnd();
  }

  trendAccumulator += rms;
  trendSampleCount++;
  if (millis() - lastTrendTime >= TREND_INTERVAL_MS) {
    lastTrendTime = millis();
    trendBuffer[trendIndex] = trendAccumulator / trendSampleCount;
    trendIndex++;
    if (trendIndex >= TREND_BUFFER_SIZE) {
      trendIndex = 0;
      trendBufferFull = true;
    }
    trendAccumulator = 0;
    trendSampleCount = 0;
  }

  if (millis() - lastPrintTime >= PRINT_INTERVAL_MS) {
    lastPrintTime = millis();
    Serial.print("[Vibration] Status: ");
    Serial.print(currentStatus);
    Serial.print(" | RMS: ");
    Serial.print(rms, 3);
    Serial.print(" | Magnitude: ");
    Serial.print(magnitude, 3);
    Serial.print(" | X: ");
    Serial.print(ax, 3);
    Serial.print(" Y: ");
    Serial.print(ay, 3);
    Serial.print(" Z: ");
    Serial.println(az, 3);
  }
}

String vibrationGetStatus() {
  return currentStatus;
}

int vibrationGetTrendCount() {
  return trendBufferFull ? TREND_BUFFER_SIZE : trendIndex;
}

float vibrationGetTrendPoint(int minutesAgo) {
  int count = vibrationGetTrendCount();
  if (minutesAgo < 0 || minutesAgo >= count) return -1;
  int idx = trendIndex - 1 - minutesAgo;
  if (idx < 0) idx += TREND_BUFFER_SIZE;
  return trendBuffer[idx];
}