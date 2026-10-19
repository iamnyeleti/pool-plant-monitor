#include <Arduino.h>
#include <WiFi.h>
#include "network.h"

static const char* AP_SSID = "PoolPlantRadar";
static const char* AP_PASS = "poolplant123";

static WebServer server(80);
static bool setupDone = false;
static bool beginDone = false;

void networkSetup() {
  if (setupDone) return;
  setupDone = true;

  WiFi.mode(WIFI_AP);
  if (!WiFi.softAP(AP_SSID, AP_PASS)) {
    Serial.println("Failed to create access point.");
    return;
  }

  Serial.print("WiFi AP started: ");
  Serial.println(AP_SSID);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
}

WebServer& networkServer() {
  return server;
}

void networkBegin() {
  if (beginDone) return;
  beginDone = true;
  server.begin();
  Serial.println("Web server started on port 80.");
}

void networkUpdate() {
  server.handleClient();
}
