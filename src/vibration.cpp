/*
  ============================================================
  vibration.cpp -- the actual vibration module code
  ============================================================
  This is where all the real logic lives. main.cpp doesn't need
  to know any of these details -- it just calls vibrationSetup()
  and vibrationUpdate(), declared in vibration.h.
  ============================================================
*/

#include "vibration.h"
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// Everything below is marked "static" or kept in this file only --
// it's private to the vibration module. main.cpp can't see or
// accidentally interfere with these variables directly, which is
// exactly what we want: each module manages its own internal state.

static Adafruit_MPU6050 mpu;

// PLACEHOLDER -- re-tune once you have real healthy-motor baseline data.
static float VIBRATION_THRESHOLD = 1.5; // m/s^2

static const int SAMPLE_WINDOW = 50;
static float sampleBuffer[SAMPLE_WINDOW];
static int sampleIndex = 0;
static bool bufferFull = false;

// ---- Internal helper functions (not listed in vibration.h, so
// they're only usable inside this file -- main.cpp has no need
// to call these directly) ----

static float computeMagnitude(sensors_event_t &accel) {
  float x = accel.acceleration.x;
  float y = accel.acceleration.y;
  float z = accel.acceleration.z;
  return sqrt(x * x + y * y + z * z);
}

static float computeRMS() {
  float sumSquares = 0;
  int count = bufferFull ? SAMPLE_WINDOW : sampleIndex;
  if (count == 0) return 0;
  for (int i = 0; i < count; i++) {
    sumSquares += sampleBuffer[i] * sampleBuffer[i];
  }
  return sqrt(sumSquares / count);
}

// ---- Public functions (these ARE listed in vibration.h, so
// main.cpp can call them) ----

void vibrationSetup() {
  if (!mpu.begin()) {
    Serial.println("MPU6050 not found. Check wiring (SDA/SCL) and power.");
    while (1) delay(10);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_4_G);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
  Serial.println("Vibration module ready.");
}

void vibrationUpdate() {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  float magnitude = computeMagnitude(accel);
  float vibration = magnitude - 9.8; // removing gravity's constant offset

  sampleBuffer[sampleIndex] = vibration;
  sampleIndex++;
  if (sampleIndex >= SAMPLE_WINDOW) {
    sampleIndex = 0;
    bufferFull = true;
  }

  float rms = computeRMS();
  String status = (rms > VIBRATION_THRESHOLD) ? "WARNING" : "OK";

  Serial.print("[Vibration] Magnitude: ");
  Serial.print(magnitude, 3);
  Serial.print(" | RMS: ");
  Serial.print(rms, 3);
  Serial.print(" | Status: ");
  Serial.println(status);
}
