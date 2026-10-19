#include <Arduino.h>
#include "radar_server.h"
#include "radar.h"
#include "network.h"
#include <ld2410.h>

extern ld2410 radar;

static void handleRadar() {
  bool active = radarIsActive();

  String json = "{";
  json += "\"radarActive\":" + String(active ? "true" : "false");
  json += ",\"presence\":" + String(active ? (radarPresenceDetected() ? "true" : "false") : "false");
  json += ",\"moving\":" + String(active ? (radar.movingTargetDetected() ? "true" : "false") : "false");
  json += ",\"movingDistance\":" + String(active ? radar.movingTargetDistance() : 0);
  json += ",\"movingEnergy\":" + String(active ? radar.movingTargetEnergy() : 0);
  json += ",\"stationary\":" + String(active ? (radar.stationaryTargetDetected() ? "true" : "false") : "false");
  json += ",\"stationaryDistance\":" + String(active ? radar.stationaryTargetDistance() : 0);
  json += ",\"stationaryEnergy\":" + String(active ? radar.stationaryTargetEnergy() : 0);
  json += ",\"distance\":" + String(active ? radar.detectionDistance() : 0);
  json += "}";

  networkServer().send(200, "application/json", json);
}

void radarServerSetup() {
  networkServer().on("/radar", handleRadar);
  Serial.println("Radar server route registered.");
}

void radarServerUpdate() {
}
