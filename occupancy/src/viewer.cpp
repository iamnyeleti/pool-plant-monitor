#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_camera.h"
#include "img_converters.h"
#include "viewer.h"
#include "detection.h"
#include "radar_client.h"
#include "timing.h"

static TimingStats fbGetStats;
static TimingStats jpgStats;

// Now connects to the DOIT board's own network, not a phone hotspot.
// Your phone also connects to this network to view the camera.
static const char* WIFI_SSID = "PoolPlantRadar";
static const char* WIFI_PASSWORD = "poolplant123";

static WebServer server(80);
static bool wifiOk = false;
static unsigned long lastAnnounce = 0;

static void handleRoot() {
  server.send(200, "text/html",
    "<html><body style='margin:0;background:#111'>"
    "<img id='cam' style='width:100%;max-width:640px'>"
    "<script>"
    "var cam=document.getElementById('cam');"
    "function load(){"
    "var img=new Image();"
    "img.onload=function(){cam.src=img.src;setTimeout(load,300);};"
    "img.onerror=function(){setTimeout(load,300);};"
    "img.src='/capture?t='+Date.now();"
    "}"
    "load();"
    "</script></body></html>");
}

static void handleCapture() {
  unsigned long t0 = micros();
  camera_fb_t *fb = esp_camera_fb_get();
  unsigned long t1 = micros();
  if (!fb) {
    server.send(500, "text/plain", "Capture failed");
    return;
  }
  fbGetStats.record(t1 - t0);

  uint8_t *jpg = NULL;
  size_t jpgLen = 0;
  unsigned long t2 = micros();
  bool ok = frame2jpg(fb, 80, &jpg, &jpgLen);
  unsigned long t3 = micros();
  esp_camera_fb_return(fb);

  if (!ok) {
    server.send(500, "text/plain", "JPEG conversion failed");
    return;
  }
  jpgStats.record(t3 - t2);

  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "image/jpeg", (const char*)jpg, jpgLen);
  free(jpg);
}

void viewerSetup() {

  
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connecting to PoolPlantRadar WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi FAILED. Is the DOIT board powered and running?");
    return;
  }

  wifiOk = true;
  server.on("/", handleRoot);
  server.on("/capture", handleCapture);
  server.on("/detection", []() {
    String json = "{";
    json += "\"eventActive\":" + String(detectionEventActive() ? "true" : "false");
    json += ",\"changePercent\":" + String(detectionChangePercent(), 1);
    json += ",\"eventCount\":" + String(detectionEventCount());
    json += ",\"radarPresence\":" + String(radarClientPresence() ? "true" : "false");
    json += ",\"radarDistance\":" + String(radarClientDistance());
    json += "}";
    server.send(200, "application/json", json);
  });
  server.begin();
  Serial.println("\nWiFi connected.");
}

void viewerUpdate() {
  if (wifiOk) server.handleClient();

  if (millis() - lastAnnounce >= 5000) {
    lastAnnounce = millis();
    if (wifiOk) {
      Serial.print("Camera viewer: http://");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("WiFi not connected. Is the DOIT board powered?");
    }
  }
}

TimingStats& viewerFbGetStats() { return fbGetStats; }
TimingStats& viewerJpgStats() { return jpgStats; }