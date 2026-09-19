/*
  ============================================================
  main.cpp -- the "conductor" of the whole program
  ============================================================
  Notice how short this file is now. Its only job is to set up
  and call each module. As you build the water quality, occupancy,
  and dashboard modules, you'll follow the exact same pattern:
  create a .h/.cpp pair for each, #include the header here, and
  call its setup/update functions from setup() and loop() below.

  This is what makes debugging easier: if something's wrong with
  vibration readings specifically, you know to go look inside
  vibration.cpp -- you're not hunting through one giant file that
  mixes every sensor together.
  ============================================================
*/

#include <Arduino.h>
#include "vibration.h"

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("Pool Plant Monitor starting...");

  vibrationSetup();

  // As other modules are built, add their setup calls here, e.g.:
  // waterQualitySetup();
  // occupancySetup();
  // dashboardSetup();
}

void loop() {
  vibrationUpdate();

  // As other modules are built, add their update calls here, e.g.:
  // waterQualityUpdate();
  // occupancyUpdate();
  // dashboardUpdate();

  delay(20);
}
