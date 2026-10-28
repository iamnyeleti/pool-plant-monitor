#include <Arduino.h>
#include "occupancy.h"
#include "viewer.h"
#include "radar_client.h"
#include "detection.h"
#include "timing.h"

static TimingStats loopStats;
static unsigned long lastCamPrint = 0;

void setup() {
  occupancySetup();
  delay(500);
  viewerSetup();
  radarClientSetup();
  detectionSetup();

  // Capture reference frame after a 15-second countdown.
  // Step out of the camera's view before it reaches zero.
  Serial.println("Reference frame will be captured in 15 seconds.");
  Serial.println("Step out of the camera's view NOW.");

  for (int i = 15; i > 0; i--) {
    Serial.printf("Capturing in %d...\n", i);
    delay(1000);
  }

  detectionCaptureReference();
  Serial.println("Reference captured. Detection is now active.");
}

void loop() {
  unsigned long ls = micros();

  viewerUpdate();
  radarClientUpdate();
  detectionUpdate();

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "ref") {
      detectionCaptureReference();
    }
  }

  delay(1);

  loopStats.record(micros() - ls);

  if (millis() - lastCamPrint >= 5000) {
    lastCamPrint = millis();
    TimingStats& fb = viewerFbGetStats();
    TimingStats& jpg = viewerJpgStats();
    TimingStats& poll = radarClientPollStats();
    TimingStats& cmp = detectionCompareStats();
    Serial.printf("[Cam] loop %.0f/%.0f ms; fb_get %.0f/%.0f ms; frame2jpg %.0f/%.0f ms; "
                  "/radar %.0f/%.0f ms (fail %lu); detect_cmp %.0f/%.0f ms; "
                  "thresh=%.0f%%; heap %lu PSRAM %lu\n",
                  loopStats.avgMs(), loopStats.maxMs(),
                  fb.avgMs(), fb.maxMs(),
                  jpg.avgMs(), jpg.maxMs(),
                  poll.avgMs(), poll.maxMs(), poll.fails,
                  cmp.avgMs(), cmp.maxMs(),
                  detectionThresholdPct(),
                  (unsigned long)ESP.getFreeHeap(),
                  (unsigned long)ESP.getFreePsram());
    loopStats.reset();
    fb.reset();
    jpg.reset();
    poll.reset();
    cmp.reset();
  }
}