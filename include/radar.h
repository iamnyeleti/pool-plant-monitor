#ifndef RADAR_H
#define RADAR_H

void radarSetup();
void radarUpdate();
bool radarPresenceDetected();
bool radarIsActive();
bool radarUartReady();
bool radarMoving();
bool radarStationary();
int  radarDistanceCm();
bool radarFrameFresh();      // UART ready and a valid frame arrived within the last 1000 ms
long radarLastFrameAgeMs();  // -1 if none yet

#endif