// water_quality.cpp
//
// STEP 3a: turbidity and the DS18B20 temperature sensor (from steps 1 and 2),
// plus the raw pH board voltage. At this step pH is only a voltage in millivolts.
// Converting it to a pH number (calibration) comes in the next step.

#include "water_quality.h"
#include <math.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ---------------------------------------------------------------
// Pins
// ---------------------------------------------------------------
static const int TURBIDITY_PIN = 35;      // G35 on the board
static const int ONE_WIRE_PIN = 4;        // G4 on the board: DS18B20 data wire
static const int PH_PIN = 34;             // G34 on the board: pH board signal (0 to 3.0 V, so no divider needed)

static const float DIVIDER_FACTOR = 2.0;  // two equal 10k resistors halve the turbidity voltage,
                                          // so multiplying by 2 gives the sensor's real voltage

static const unsigned long PRINT_INTERVAL_MS = 1000;
static unsigned long lastPrintTime = 0;

// ---------------------------------------------------------------
// Turbidity averaging (unchanged from step 1)
// ---------------------------------------------------------------
static unsigned long sumMilliVolts = 0;
static unsigned long sampleCount = 0;

// pH board voltage, averaged the same way
static unsigned long phSumMilliVolts = 0;
static unsigned long phSampleCount = 0;

// ---------------------------------------------------------------
// Temperature (DS18B20)
// ---------------------------------------------------------------
static OneWire oneWire(ONE_WIRE_PIN);
static DallasTemperature tempSensor(&oneWire);

static const uint8_t TEMP_RESOLUTION_BITS = 12;          // 12 bits = steps of 0.0625 degrees C
static const unsigned long TEMP_CONVERSION_MS = 750;     // time the chip needs to measure at 12 bits
static const unsigned long TEMP_MARGIN_MS = 50;          // extra wait so we never read too early
static const unsigned long TEMP_PERIOD_MS = 1000;        // start a new measurement once a second

static bool tempFound = false;
static bool tempConverting = false;
static unsigned long tempRequestTime = 0;
static float lastTempC = NAN;

// Starts a measurement, then comes back later to collect the result.
// It never waits inside the loop, so the other modules keep their timing.
static void temperatureUpdate() {
  if (!tempFound) return;

  unsigned long now = millis();

  if (!tempConverting) {
    if (now - tempRequestTime >= TEMP_PERIOD_MS) {
      tempSensor.requestTemperatures();   // returns straight away (waiting is switched off in setup)
      tempRequestTime = now;
      tempConverting = true;
    }
    return;
  }

  if (now - tempRequestTime >= TEMP_CONVERSION_MS + TEMP_MARGIN_MS) {
    float t = tempSensor.getTempCByIndex(0);
    tempConverting = false;

    // -127 means the chip did not answer. Exactly 85.00 is the value the chip
    // holds at power-up before it has measured anything, so it is not a real reading.
    if (t == DEVICE_DISCONNECTED_C || t == 85.0) {
      lastTempC = NAN;
    } else {
      lastTempC = t;
    }
  }
}

float waterQualityGetTemperatureC() {
  return lastTempC;
  
}

// ---------------------------------------------------------------
// Setup and update
// ---------------------------------------------------------------
void waterQualitySetup() {
  // 11 dB attenuation lets the ADC measure up to roughly 3.1 V.
  analogSetPinAttenuation(TURBIDITY_PIN, ADC_11db);
  analogSetPinAttenuation(PH_PIN, ADC_11db);

  tempSensor.begin();
  tempFound = (tempSensor.getDeviceCount() > 0);

  if (tempFound) {
    tempSensor.setResolution(TEMP_RESOLUTION_BITS);
    tempSensor.setWaitForConversion(false);   // do not wait inside requestTemperatures()
    Serial.println("DS18B20 temperature sensor found.");
  } else {
    Serial.println("DS18B20 NOT found. Check the three wires and the pull-up resistor on G4.");
  }

  Serial.println("Water quality module ready (turbidity, temperature and raw pH voltage).");
}

void waterQualityUpdate() {
  temperatureUpdate();

  sumMilliVolts += analogReadMilliVolts(TURBIDITY_PIN);
  sampleCount++;

  phSumMilliVolts += analogReadMilliVolts(PH_PIN);
  phSampleCount++;

  if (millis() - lastPrintTime >= PRINT_INTERVAL_MS) {
    lastPrintTime = millis();

    float pinVolts = (sumMilliVolts / (float)sampleCount) / 1000.0;
    float sensorVolts = pinVolts * DIVIDER_FACTOR;

    Serial.print("[Turbidity] At G35 pin: ");
    Serial.print(pinVolts, 3);
    Serial.print(" V | Sensor output: ");
    Serial.print(sensorVolts, 3);
    Serial.print(" V");
    if (pinVolts > 2.3) {
      Serial.print("  <-- TOO HIGH, UNPLUG USB AND CHECK THE DIVIDER");
    }
    Serial.println();

    sumMilliVolts = 0;
    sampleCount = 0;

    if (isnan(lastTempC)) {
      Serial.println("[Temperature] no valid reading yet");
    } else {
      Serial.print("[Temperature] ");
      Serial.print(lastTempC, 2);
      Serial.println(" C");
    }

    float phMilliVolts = phSumMilliVolts / (float)phSampleCount;
    Serial.print("[pH] Board output at G34: ");
    Serial.print(phMilliVolts, 1);
    Serial.print(" mV");
    if (phMilliVolts > 3050.0) {
      Serial.print("  <-- ABOVE 3.0 V, CHECK THE WIRING");
    }
    Serial.println();

    phSumMilliVolts = 0;
    phSampleCount = 0;
  }
}