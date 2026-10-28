#ifndef RADAR_CLIENT_H
#define RADAR_CLIENT_H

#include "timing.h"

void radarClientSetup();
void radarClientUpdate();
bool radarClientPresence();
int radarClientDistance();
TimingStats& radarClientPollStats();

#endif