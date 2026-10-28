#ifndef VIEWER_H
#define VIEWER_H

#include "timing.h"

void viewerSetup();
void viewerUpdate();
TimingStats& viewerFbGetStats();
TimingStats& viewerJpgStats();

#endif