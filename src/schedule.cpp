#include <Arduino.h>
#include <Wire.h>
#include "RTClib.h"
#include "schedule.h"

// Set to true since the DS3231 is now physically wired in.
static const bool RTC_INSTALLED = true;

static RTC_DS3231 rtc;

void scheduleSetup() {
  if (!RTC_INSTALLED) {
    Serial.println("RTC not installed yet, schedule module inactive.");
    return;
  }

  if (!rtc.begin()) {
    Serial.println("DS3231 not found. Check wiring (SDA/SCL) and power.");
    while (1) delay(10);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting to compile time...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  } else {
    Serial.println("RTC already keeping time correctly, leaving it alone.");
  }

  Serial.println("RTC module ready.");
}

void scheduleUpdate() {
  if (!RTC_INSTALLED) return;

  DateTime now = rtc.now();
  Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
                now.year(), now.month(), now.day(),
                now.hour(), now.minute(), now.second());
  delay(1000);
}