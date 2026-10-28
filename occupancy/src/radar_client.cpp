#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "radar_client.h"
#include "detection.h"
#include "timing.h"

static TimingStats pollStats;

static const char* RADAR_URL = "http://192.168.4.1/radar";
static const char* HEARTBEAT_URL = "http://192.168.4.1/api/occupancy/heartbeat";

static bool presence = false;
static int distance = 0;
static bool radarReachable = false;
static unsigned long lastPoll = 0;
static const unsigned long POLL_INTERVAL_MS = 500;
static unsigned long lastHeartbeat = 0;
static const unsigned long HEARTBEAT_INTERVAL_MS = 2000;
static unsigned long lastPrintTime = 0;

void radarClientSetup() {
  Serial.println("Radar client ready, will poll DOIT board.");
}

void radarClientUpdate() {
  if (millis() - lastPoll < POLL_INTERVAL_MS) return;
  lastPoll = millis();

  HTTPClient http;
  http.setConnectTimeout(1000);
  http.setTimeout(1000);
  http.begin(RADAR_URL);

  unsigned long pt0 = micros();
  int code = http.GET();
  unsigned long pt1 = micros();
  pollStats.record(pt1 - pt0);
  radarReachable = (code == 200);
  if (!radarReachable) pollStats.fails++;
  if (radarReachable) {
    String body = http.getString();
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, body);
    if (error == DeserializationError::Ok) {
      presence = doc["presence"] | false;
      distance = doc["distance"] | 0;
    }
  }
  http.end();

  if (radarReachable && millis() - lastHeartbeat >= HEARTBEAT_INTERVAL_MS) {
    lastHeartbeat = millis();

    String url = String(HEARTBEAT_URL)
      + "?eventCount=" + String(detectionEventCount())
      + "&eventActive=" + String(detectionEventActive() ? "1" : "0")
      + "&changePercent=" + String(detectionChangePercent(), 1);

    HTTPClient hb;
    hb.setConnectTimeout(1000);
    hb.setTimeout(1000);
    hb.begin(url);
    hb.GET();
    hb.end();
  }

  if (millis() - lastPrintTime >= 1000) {
    lastPrintTime = millis();
    Serial.print("[RadarClient] Presence: ");
    Serial.print(presence ? "YES" : "NO");
    Serial.print(" | Distance: ");
    Serial.print(distance);
    Serial.println("cm");
  }
}

bool radarClientPresence() {
  return presence;
}

int radarClientDistance() {
  return distance;
}

TimingStats& radarClientPollStats() { return pollStats; }