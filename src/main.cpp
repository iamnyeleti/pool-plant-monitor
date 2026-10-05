#include <Arduino.h>
#include "vibration.h"
#include "schedule.h"

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("Pool Plant Monitor starting...");

  vibrationSetup();
  scheduleSetup();
}

void loop() {
  vibrationUpdate();
  scheduleUpdate();
  delay(20);
}