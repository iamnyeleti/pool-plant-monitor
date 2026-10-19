#include <Arduino.h>
#include "vibration.h"
#include "schedule.h"
#include "radar.h"
#include "radar_server.h"
#include "water_quality.h"
#include "dashboard.h"
#include "network.h"

static const bool VIBRATION_CONNECTED = false;
static const bool RTC_CONNECTED = false;
static const bool RADAR_CONNECTED = true;
static const bool RADAR_SERVER_CONNECTED = true;
static const bool WATER_QUALITY_CONNECTED = false;
static const bool DASHBOARD_CONNECTED = true;

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("Pool Plant Monitor starting...");

  if (RADAR_SERVER_CONNECTED || DASHBOARD_CONNECTED) networkSetup();

  if (VIBRATION_CONNECTED) vibrationSetup();
  if (RTC_CONNECTED) scheduleSetup();
  if (RADAR_CONNECTED) radarSetup();
  if (RADAR_SERVER_CONNECTED) radarServerSetup();
  if (WATER_QUALITY_CONNECTED) waterQualitySetup();
  if (DASHBOARD_CONNECTED) dashboardSetup();

  if (RADAR_SERVER_CONNECTED || DASHBOARD_CONNECTED) networkBegin();
}

void loop() {
  if (VIBRATION_CONNECTED) vibrationUpdate();
  if (RTC_CONNECTED) scheduleUpdate();
  if (RADAR_CONNECTED) radarUpdate();
  if (RADAR_SERVER_CONNECTED) radarServerUpdate();
  if (WATER_QUALITY_CONNECTED) waterQualityUpdate();
  if (DASHBOARD_CONNECTED) dashboardUpdate();
  if (RADAR_SERVER_CONNECTED || DASHBOARD_CONNECTED) networkUpdate();
  delay(20);
}