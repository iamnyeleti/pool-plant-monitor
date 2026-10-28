#include <Arduino.h>
#include "esp_camera.h"
#include "detection.h"
#include "radar_client.h"
#include "timing.h"

static TimingStats compareStats;
static unsigned long risingEdgeUs = 0;
static bool pendingLatMeasure = false;

// --- Region of interest (ROI) ---
// These define a rectangle within the 640x480 frame.
// Only pixels inside this box are compared.
// Adjust these to cover just the entrance area in your camera's view.
static const int ROI_X = 100;
static const int ROI_Y = 50;
static const int ROI_W = 440;
static const int ROI_H = 380;

// --- Detection thresholds ---
// A pixel counts as "changed" if its brightness differs from the
// reference by more than this amount (0-255 scale).
static const int PIXEL_CHANGE_THRESHOLD = 30;

// The fraction of changed pixels within the ROI that counts as
// a real scene change, not just noise. This is a starting point,
// you'll tune it from real testing.
static const float SCENE_CHANGE_THRESHOLD_PCT = 15.0f;

// --- Reference frame storage ---
// Stored in PSRAM since it's a full 640x480 grayscale frame.
static uint8_t* referenceFrame = NULL;
static int referenceWidth = 0;
static int referenceHeight = 0;
static bool referenceValid = false;

// --- Detection state ---
static bool eventActive = false;
static float changePercent = 0.0;
static float avgBrightness = 0.0;
static int eventCount = 0;
static bool wasPresent = false;

// --- Timing ---
static unsigned long lastDetectionTime = 0;
static const unsigned long DETECTION_INTERVAL_MS = 1000;
static unsigned long lastPrintTime = 0;
static const unsigned long PRINT_INTERVAL_MS = 1000;

// --- Lighting check ---
// If the average brightness of the ROI is below this, the frame
// is too dark to be useful, skip comparison, report it honestly.
static const int MIN_USEFUL_BRIGHTNESS = 70;

void detectionSetup() {
  // Allocate space for one reference frame in PSRAM.
  if (psramFound()) {
    referenceFrame = (uint8_t*)ps_malloc(640 * 480);
    if (referenceFrame) {
      Serial.println("Detection module ready (PSRAM allocated).");
    } else {
      Serial.println("Detection: PSRAM allocation failed.");
    }
  } else {
    Serial.println("Detection: No PSRAM found, cannot store reference frame.");
  }
}

void detectionCaptureReference() {
  if (!referenceFrame) {
    Serial.println("Detection: No reference buffer available.");
    return;
  }

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Detection: Reference capture failed.");
    return;
  }

  memcpy(referenceFrame, fb->buf, fb->len);
  referenceWidth = fb->width;
  referenceHeight = fb->height;
  referenceValid = true;

  esp_camera_fb_return(fb);

  Serial.printf("Detection: Reference frame captured (%dx%d, %d bytes).\n",
                referenceWidth, referenceHeight, referenceWidth * referenceHeight);
}

static float compareFrameToReference(camera_fb_t* fb) {
  if (!referenceValid) return 0.0;
  if (fb->width != referenceWidth || fb->height != referenceHeight) return 0.0;

  int changedPixels = 0;
  int totalPixels = 0;
  long brightnessSum = 0;

  for (int y = ROI_Y; y < ROI_Y + ROI_H && y < fb->height; y++) {
    for (int x = ROI_X; x < ROI_X + ROI_W && x < fb->width; x++) {
      int idx = y * fb->width + x;
      uint8_t current = fb->buf[idx];
      uint8_t reference = referenceFrame[idx];

      brightnessSum += current;
      totalPixels++;

      int diff = abs((int)current - (int)reference);
      if (diff > PIXEL_CHANGE_THRESHOLD) {
        changedPixels++;
      }
    }
  }

  if (totalPixels == 0) return 0.0;

  float bright = (float)brightnessSum / totalPixels;
  avgBrightness = bright;
  if (bright < MIN_USEFUL_BRIGHTNESS) {
    return -1.0;
  }

  return (float)changedPixels / totalPixels * 100.0;
}

void detectionUpdate() {
  // Only run detection when radar says someone is present.
  bool radarPresent = radarClientPresence();

  bool risingEdge = (radarPresent && !wasPresent);
  wasPresent = radarPresent;

  if (risingEdge) {
    risingEdgeUs = micros();
    pendingLatMeasure = true;
  }

  if (!radarPresent) {
    if (eventActive) {
      Serial.println("[Detection] Radar cleared, event ended.");
      eventActive = false;
    }
    return;
  }

  // Don't compare if we have no reference yet.
  if (!referenceValid) {
    if (risingEdge) {
      Serial.println("[Detection] Radar triggered but no reference frame set.");
      Serial.println("[Detection] Send 'ref' in Serial Monitor to capture one.");
    }
    return;
  }

  // Rate-limit the comparison, one per second is plenty.
  if (millis() - lastDetectionTime < DETECTION_INTERVAL_MS) return;
  lastDetectionTime = millis();

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("[Detection] Frame capture failed.");
    return;
  }

  unsigned long cmpT0 = micros();
  changePercent = compareFrameToReference(fb);
  unsigned long cmpT1 = micros();
  esp_camera_fb_return(fb);
  compareStats.record(cmpT1 - cmpT0);

  if (pendingLatMeasure) {
    Serial.printf("[Lat] %lu ms from rising edge to first compare end\n", (micros() - risingEdgeUs) / 1000);
    pendingLatMeasure = false;
  }

  if (changePercent < 0) {
    if (millis() - lastPrintTime >= PRINT_INTERVAL_MS) {
      lastPrintTime = millis();
      Serial.printf("[Detection] Radar: YES | Camera: TOO DARK | bright=%.1f\n", avgBrightness);
    }
    return;
  }

  bool sceneChanged = (changePercent >= SCENE_CHANGE_THRESHOLD_PCT);

  if (sceneChanged && !eventActive) {
    eventActive = true;
    eventCount++;
    Serial.printf("[Detection] EVENT #%d — Radar: YES | Camera: %.1f%% change (corroborated) | bright=%.1f\n",
                  eventCount, changePercent, avgBrightness);
  } else if (millis() - lastPrintTime >= PRINT_INTERVAL_MS) {
    lastPrintTime = millis();
    Serial.printf("[Detection] Radar: %s | Camera: %.1f%% change | Event: %s | bright=%.1f\n",
                  radarPresent ? "YES" : "NO",
                  changePercent,
                  eventActive ? "ACTIVE" : "none",
                  avgBrightness);
  }
}

bool detectionEventActive() {
  return eventActive;
}

float detectionChangePercent() {
  return changePercent;
}

int detectionEventCount() {
  return eventCount;
}

float detectionBrightness() { return avgBrightness; }
float detectionThresholdPct() { return SCENE_CHANGE_THRESHOLD_PCT; }
TimingStats& detectionCompareStats() { return compareStats; }