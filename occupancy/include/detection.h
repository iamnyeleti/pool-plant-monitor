#ifndef DETECTION_H
#define DETECTION_H

#include "timing.h"

void detectionSetup();
void detectionUpdate();

// Call this when you know the scene is empty, it saves the current
// frame as the reference to compare against.
void detectionCaptureReference();

bool detectionEventActive();
float detectionChangePercent();
float detectionBrightness();
float detectionThresholdPct();
int detectionEventCount();
TimingStats& detectionCompareStats();

#endif