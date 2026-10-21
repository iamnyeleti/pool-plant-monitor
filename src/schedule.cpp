#include <Arduino.h>
#include <Wire.h>
#include "RTClib.h"
#include "schedule.h"

// Set to true since the DS3231 is now physically wired in.
static const bool RTC_INSTALLED = true;

static RTC_DS3231 rtc;
static bool rtcReady = false;

void scheduleSetup() {
  if (!RTC_INSTALLED) {
    Serial.println("RTC not installed yet, schedule module inactive.");
    rtcReady = false;
    return;
  }

  if (!rtc.begin()) {
    Serial.println("DS3231 not found. Check wiring (SDA/SCL) and power.");
    rtcReady = false;
    return;
  }

  rtcReady = true;

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting to compile time...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  } else {
    Serial.println("RTC already keeping time correctly, leaving it alone.");
  }

  Serial.println("RTC module ready.");
}

void scheduleUpdate() {
  if (!RTC_INSTALLED || !rtcReady) return;

  DateTime now = rtc.now();
  static unsigned long lastRtcPrint = 0;

  if (millis() - lastRtcPrint >= 1000) {
    lastRtcPrint = millis();
    Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
  }
}

bool scheduleIsReady() {
  return RTC_INSTALLED && rtcReady;
}

bool scheduleGetMinuteOfDay(int* minuteOfDay) {
  if (!RTC_INSTALLED || !rtcReady) return false;

  DateTime now = rtc.now();
  *minuteOfDay = now.hour() * 60 + now.minute();
  return true;
}

String scheduleGetTimeString() {
  if (!RTC_INSTALLED || !rtcReady) return "RTC unavailable";

  DateTime now = rtc.now();

  char buffer[24];

  snprintf(buffer, sizeof(buffer),
           "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());

  return String(buffer);
}