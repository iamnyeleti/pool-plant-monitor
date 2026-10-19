#include <Arduino.h>
#include "radar_server.h"
#include "radar.h"
#include "network.h"
#include <ld2410.h>

extern ld2410 radar;

static void handleRadar() {
  bool active = radarIsActive();
  bool uart = radarUartReady();

  String json = "{";
  json += "\"radarActive\":" + String(active ? "true" : "false");
  json += ",\"uartReady\":" + String(uart ? "true" : "false");
  json += ",\"presence\":" + String(active ? (radarPresenceDetected() ? "true" : "false") : "false");
  json += ",\"moving\":" + String((active && uart) ? (radar.movingTargetDetected() ? "true" : "false") : "false");
  json += ",\"movingDistance\":" + String((active && uart) ? radar.movingTargetDistance() : 0);
  json += ",\"movingEnergy\":" + String((active && uart) ? radar.movingTargetEnergy() : 0);
  json += ",\"stationary\":" + String((active && uart) ? (radar.stationaryTargetDetected() ? "true" : "false") : "false");
  json += ",\"stationaryDistance\":" + String((active && uart) ? radar.stationaryTargetDistance() : 0);
  json += ",\"stationaryEnergy\":" + String((active && uart) ? radar.stationaryTargetEnergy() : 0);
  json += ",\"distance\":" + String((active && uart) ? radar.detectionDistance() : 0);
  json += "}";

  networkServer().send(200, "application/json", json);
}

void radarServerSetup() {
  networkServer().on("/radar", handleRadar);
  Serial.println("Radar server route registered.");
}

void radarServerUpdate() {
}
