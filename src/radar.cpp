#include <Arduino.h>
#include <ld2410.h>
#include "radar.h"

static const int RADAR_OUT_PIN = 5;

// UART2 on the ESP32, TX/RX crossed as always:
// radar TX → ESP32 RX (GPIO16), radar RX → ESP32 TX (GPIO17)
ld2410 radar;
static bool radarReady = false;
static bool radarSetupRun = false;

static bool presenceDetected = false;
static unsigned long lastPrintTime = 0;
static const unsigned long PRINT_INTERVAL_MS = 500;

void radarSetup() {
  radarSetupRun = true;
  pinMode(RADAR_OUT_PIN, INPUT);

  Serial2.begin(256000, SERIAL_8N1, 16, 17);
  delay(100);

  if (radar.begin(Serial2)) {
    radarReady = true;
    Serial.println("Radar module ready (UART connected).");
  } else {
    radarReady = false;
    Serial.println("Radar UART failed. OUT pin still works.");
  }
}

void radarUpdate() {
  presenceDetected = (digitalRead(RADAR_OUT_PIN) == HIGH);

  if (radarReady) {
    radar.read();
  }

  if (millis() - lastPrintTime >= PRINT_INTERVAL_MS) {
    lastPrintTime = millis();

    Serial.print("[Radar] OUT: ");
    Serial.print(presenceDetected ? "HIGH" : "LOW");

    if (radarReady && radar.isConnected()) {
      Serial.print(" | Moving: ");
      if (radar.movingTargetDetected()) {
        Serial.print(radar.movingTargetDistance());
        Serial.print("cm energy:");
        Serial.print(radar.movingTargetEnergy());
      } else {
        Serial.print("none");
      }

      Serial.print(" | Still: ");
      if (radar.stationaryTargetDetected()) {
        Serial.print(radar.stationaryTargetDistance());
        Serial.print("cm energy:");
        Serial.print(radar.stationaryTargetEnergy());
      } else {
        Serial.print("none");
      }

      Serial.print(" | Distance: ");
      Serial.print(radar.detectionDistance());
      Serial.print("cm");
    }

    Serial.println();
  }
}

bool radarPresenceDetected() {
  return presenceDetected;
}

bool radarIsActive() {
  return radarSetupRun;
}

bool radarUartReady() {
  return radarReady;
}