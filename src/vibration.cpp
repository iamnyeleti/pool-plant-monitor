
// vibration.cpp
#include "vibration.h"
#include "vib_history.h"
#include "schedule.h"
#include "module_flags.h"
#include <Wire.h>
#include <Preferences.h>

// AD0 is wired to 3.3V, shifting the MPU6050 from 0x68 to 0x69,
// since the DS3231 now occupies 0x68 on the same shared bus.
static const uint8_t MPU_ADDR = 0x69;

// Added so the dashboard can access the most recent RMS value.
static float currentRMS = 0.0f;

static const uint8_t DLPF_REG_VALUE = 0x00;

static bool mpuInit() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C);
  Wire.write(0x08);
  if (Wire.endTransmission() != 0) return false;

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1A);
  Wire.write(DLPF_REG_VALUE); // 0x00 = ~260 Hz bandwidth (effectively unfiltered); 0x04 would be ~21 Hz
  if (Wire.endTransmission() != 0) return false;

  return true;
}

static bool mpuReadAccel(float &x, float &y, float &z) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;

  Wire.requestFrom((int)MPU_ADDR, 6);
  if (Wire.available() < 6) {
    while (Wire.available()) Wire.read();
    return false;
  }

  int16_t rawX = (Wire.read() << 8) | Wire.read();
  int16_t rawY = (Wire.read() << 8) | Wire.read();
  int16_t rawZ = (Wire.read() << 8) | Wire.read();

  // All-zero or all-0xFFFF is what a dead or floating bus returns, not real data.
  if ((rawX == 0 && rawY == 0 && rawZ == 0) ||
      (rawX == -1 && rawY == -1 && rawZ == -1))
    return false;

  const float LSB_PER_G = 8192.0;
  const float G_TO_MS2 = 9.8;

  x = (rawX / LSB_PER_G) * G_TO_MS2;
  y = (rawY / LSB_PER_G) * G_TO_MS2;
  z = (rawZ / LSB_PER_G) * G_TO_MS2;

  return true;
}

#ifndef VIB_CALIBRATION_MODE
#define VIB_CALIBRATION_MODE 0
#endif
static const bool CALIBRATION_MODE = (VIB_CALIBRATION_MODE != 0); // Select with -DVIB_CALIBRATION_MODE=1.
static const unsigned long CALIBRATION_WARMUP_MS = 20000;
static const unsigned long CALIBRATION_DURATION_MS = 90000;

static bool calibrationWarmedUp = false;
static bool calibrationDone = false;
static unsigned long calibrationPhaseStart = 0;
static double calibrationSum = 0;
static unsigned long calibrationCount = 0;
static unsigned long lastCalibrationPrint = 0;

// Real measured baseline from the fan calibration run.
#ifndef VIB_BASELINE
#define VIB_BASELINE 0.7854
#endif
static const float BASELINE_RUNNING_RMS = VIB_BASELINE;

// Threshold multipliers
static const float CAUTION_MULTIPLIER = 1.2;
static const float WARNING_MULTIPLIER = 1.5;

static float cautionThreshold() {
  return BASELINE_RUNNING_RMS * CAUTION_MULTIPLIER;
}

static float warningThreshold() {
  return BASELINE_RUNNING_RMS * WARNING_MULTIPLIER;
}

static const unsigned long WARNING_DURATION_MS =
    2UL * 60UL * 1000UL;

static const float WARNING_FRACTION_THRESHOLD = 0.5;

#ifndef VIB_STOPPED_STD
#define VIB_STOPPED_STD 0.05f
#endif
// PLACEHOLDER: unvalidated, not measured against a real running/stopped pump.
static const float STOPPED_STD_THRESHOLD = VIB_STOPPED_STD;
static const unsigned long STOPPED_HOLD_MS = 60000UL;
static const float STOPPED_EXIT_FACTOR = 2.0f;

static const int SAMPLE_WINDOW = 50;
static float sampleBuffer[SAMPLE_WINDOW];

static int sampleIndex = 0;
static bool bufferFull = false;

static unsigned long lastPrintTime = 0;
static const unsigned long PRINT_INTERVAL_MS = 250;

static const unsigned long ASSUMED_LOOP_INTERVAL_MS = 20;

static const int ELEVATED_HISTORY_SIZE =
    WARNING_DURATION_MS / ASSUMED_LOOP_INTERVAL_MS;

static bool elevatedHistory[ELEVATED_HISTORY_SIZE];

static int elevatedIndex = 0;
static int elevatedCount = 0;
static bool elevatedHistoryFull = false;

static String currentStatus = "OK";

struct VibrationEvent {
  unsigned long timestamp;
  unsigned long durationMs;
  float peakRMS;
  bool reachedWarning;
  char when[24];
};

static const int EVENT_LOG_SIZE = 10;
static VibrationEvent eventLog[EVENT_LOG_SIZE];

static int eventLogIndex = 0;
static bool loggingActiveEvent = false;

static unsigned long activeEventStart = 0;
static float activeEventPeak = 0;
static bool activeEventReachedWarning = false;
static char activeEventWhen[24] = "";

static void formatEventTime(char* out, size_t n, unsigned long nowMs) {
  if (scheduleIsReady()) {
    String t = scheduleGetTimeString();
    snprintf(out, n, "%s", t.c_str());
  } else {
    unsigned long s = nowMs / 1000UL;
    snprintf(out, n, "T+%02lu:%02lu:%02lu", s / 3600UL, (s / 60UL) % 60UL, s % 60UL);
  }
}

static const unsigned long TREND_INTERVAL_MS =
    60UL * 1000UL;

static const int TREND_BUFFER_SIZE = 1440;

static float trendBuffer[TREND_BUFFER_SIZE];

static int trendIndex = 0;
static bool trendBufferFull = false;

static unsigned long lastTrendTime = 0;

static double trendAccumulator = 0;
static unsigned long trendSampleCount = 0;

static float computeRMS() {
  float sumSquares = 0;

  int count = bufferFull
                  ? SAMPLE_WINDOW
                  : sampleIndex;

  if (count == 0)
    return 0;

  for (int i = 0; i < count; i++) {
    sumSquares += sampleBuffer[i] * sampleBuffer[i];
  }

  return sqrt(sumSquares / count);
}

static void logEventEnd() {

  eventLog[eventLogIndex].timestamp =
      activeEventStart;

  eventLog[eventLogIndex].durationMs =
      millis() - activeEventStart;

  eventLog[eventLogIndex].peakRMS =
      activeEventPeak;

  eventLog[eventLogIndex].reachedWarning =
      activeEventReachedWarning;

  snprintf(eventLog[eventLogIndex].when,
           sizeof(eventLog[eventLogIndex].when), "%s", activeEventWhen);

  Serial.printf(
      "[EVENT] %s elevated_vibration duration_ms=%lu peak_rms=%.3f warning=%d\n",
      eventLog[eventLogIndex].when,
      eventLog[eventLogIndex].durationMs,
      eventLog[eventLogIndex].peakRMS,
      eventLog[eventLogIndex].reachedWarning ? 1 : 0);

  eventLogIndex =
      (eventLogIndex + 1) % EVENT_LOG_SIZE;

  loggingActiveEvent = false;
}

static vh::StoppedDetector stoppedDetector(
    STOPPED_STD_THRESHOLD, STOPPED_HOLD_MS, STOPPED_EXIT_FACTOR);

// ---- Pump run window (separate from any pool access schedule) ----
enum PumpState { PS_UNKNOWN, PS_NO_WINDOW, PS_NO_RTC, PS_OFF, PS_INSIDE };
static PumpState pumpState = PS_UNKNOWN;
static bool pumpSet = false;
static uint16_t pumpStart = 0;  // minute of day
static uint16_t pumpEnd = 0;
static bool pumpDirty = true;
static unsigned long lastPumpEval = 0;

static bool pumpExpectedRunning() {
  return pumpState == PS_INSIDE;
}

static void pumpLoad() {
  Preferences p;
  if (p.begin("pump", false)) {
    pumpSet = p.getBool("set", false);
    pumpStart = p.getUShort("start", 0);
    pumpEnd = p.getUShort("end", 0);
    p.end();
  }
  if (pumpStart >= 1440 || pumpEnd >= 1440 || pumpStart == pumpEnd) pumpSet = false;
}

static void pumpSave() {
  Preferences p;
  if (p.begin("pump", false)) {
    p.putBool("set", pumpSet);
    p.putUShort("start", pumpStart);
    p.putUShort("end", pumpEnd);
    p.end();
  }
}

static void pumpPrint() {
  if (!pumpSet) {
    Serial.println("[Vibration] PUMP window: not set");
  } else {
    Serial.printf("[Vibration] PUMP window: %02u:%02u-%02u:%02u\n",
                  pumpStart / 60, pumpStart % 60, pumpEnd / 60, pumpEnd % 60);
  }
}

static void pumpHandleCommand(String s) {
  s.trim();
  s.toUpperCase();

  if (s == "PUMP?") {
    pumpPrint();
  } else if (s == "PUMP CLEAR") {
    pumpSet = false;
    pumpSave();
    pumpDirty = true;
    stoppedDetector.reset();
    pumpPrint();
  } else if (s.startsWith("PUMP ")) {
    int h1, m1, h2, m2;
    if (sscanf(s.c_str() + 5, "%d:%d %d:%d", &h1, &m1, &h2, &m2) == 4 &&
        h1 >= 0 && h1 < 24 && h2 >= 0 && h2 < 24 &&
        m1 >= 0 && m1 < 60 && m2 >= 0 && m2 < 60 &&
        (h1 * 60 + m1) != (h2 * 60 + m2)) {
      pumpStart = h1 * 60 + m1;
      pumpEnd = h2 * 60 + m2;
      pumpSet = true;
      pumpSave();
      pumpDirty = true;
      stoppedDetector.reset();
      pumpPrint();
    } else {
      Serial.println("[Vibration] PUMP usage: PUMP HH:MM HH:MM | PUMP CLEAR | PUMP?");
    }
  }
}

// The water-quality module drains the whole serial buffer, so the PUMP commands are
// only compiled in when it is disabled.
static void pumpPollSerial() {
#if !CFG_WATER_QUALITY_CONNECTED
  static String line;
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (line.length() > 0) {
        pumpHandleCommand(line);
        line = "";
      }
    } else if (line.length() < 40) {
      line += c;
    }
  }
#endif
}

static void pumpStateUpdate(unsigned long nowMs) {
  if (!pumpDirty && nowMs - lastPumpEval < 1000) return;
  pumpDirty = false;
  lastPumpEval = nowMs;

  PumpState s;
  int m = 0;
  if (!pumpSet) {
    s = PS_NO_WINDOW;
  } else if (!scheduleGetMinuteOfDay(&m)) {
    s = PS_NO_RTC;
  } else {
    bool inside = (pumpStart < pumpEnd) ? (m >= pumpStart && m < pumpEnd)
                                        : (m >= pumpStart || m < pumpEnd);
    s = inside ? PS_INSIDE : PS_OFF;
  }

  if (s == pumpState) return;
  pumpState = s;

  switch (s) {
    case PS_NO_WINDOW:
      Serial.println("[Vibration] Stopped detection off: no pump schedule");
      break;
    case PS_NO_RTC:
      Serial.println("[Vibration] Stopped detection off: RTC not ready");
      break;
    case PS_OFF:
      Serial.println("[Vibration] OFF SCHEDULE");
      break;
    case PS_INSIDE:
      Serial.println("[Vibration] Inside pump window: STOPPED detection on");
      break;
    default:
      break;
  }
}

static const unsigned long SENSOR_REINIT_MS = 5000;
static const unsigned long SENSOR_TIMEOUT_MS = 2000;
static const unsigned long SENSOR_RECOVER_MS = 1000;
static bool sensorOk = false;
static unsigned long lastInitAttempt = 0;
static unsigned long lastGoodMs = 0;
static unsigned long recoverStart = 0;

// After an outage the RMS window and the 6000-sample WARNING history hold
// pre-outage samples, so both are cleared; WARNING cannot fire until the
// history has refilled with new valid samples.
static void resetSampleHistory() {
  sampleIndex = 0;
  bufferFull = false;
  for (int i = 0; i < ELEVATED_HISTORY_SIZE; i++) elevatedHistory[i] = false;
  elevatedIndex = 0;
  elevatedCount = 0;
  elevatedHistoryFull = false;
}

void vibrationSetup() {

  Wire.begin();

  lastInitAttempt = millis();

  pumpLoad();

  if (mpuInit()) {
    sensorOk = true;
    lastGoodMs = millis();
    Serial.println("Vibration module ready.");
  } else {
    sensorOk = false;
    currentStatus = "SENSOR ERROR";
    Serial.println("[Vibration] SENSOR ERROR: init failed");
  }

  Serial.printf(
      "[CFG] dlpf=0x%02X baseline=%.4f caution_mult=%.2f warning_mult=%.2f "
      "window=%d fraction=%.2f sample_delay_ms=%lu calibration=%d "
      "stopped_std=%.3f stopped_hold_s=%lu build=%s %s\n",
      DLPF_REG_VALUE, BASELINE_RUNNING_RMS, CAUTION_MULTIPLIER,
      WARNING_MULTIPLIER, ELEVATED_HISTORY_SIZE, WARNING_FRACTION_THRESHOLD,
      ASSUMED_LOOP_INTERVAL_MS, CALIBRATION_MODE ? 1 : 0,
      STOPPED_STD_THRESHOLD, STOPPED_HOLD_MS / 1000UL, __DATE__, __TIME__);

  if (CALIBRATION_MODE) {

    Serial.println(
        "=== CALIBRATION MODE ===");

    calibrationPhaseStart =
        millis();
  }
}

static void calibrationUpdate(float rms) {

  unsigned long elapsed =
      millis() - calibrationPhaseStart;

  if (!calibrationWarmedUp) {

    if (elapsed >=
        CALIBRATION_WARMUP_MS) {

      calibrationWarmedUp = true;

      calibrationPhaseStart =
          millis();

      calibrationSum = 0;
      calibrationCount = 0;

      Serial.println(
          "Warm-up done. Recording now...");
    }

    return;
  }

  if (calibrationDone) {

    if (millis() -
            lastCalibrationPrint >=
        2000) {

      lastCalibrationPrint =
          millis();

      Serial.print(
          "===== CALIBRATION COMPLETE ===== "
          "Average RMS: ");

      Serial.println(
          calibrationSum /
              calibrationCount,
          4);
    }

    return;
  }

  calibrationSum += rms;
  calibrationCount++;

  if (millis() -
          lastCalibrationPrint >=
      2000) {

    lastCalibrationPrint =
        millis();

    Serial.print(
        "Calibrating... RMS: ");

    Serial.println(
        rms,
        3);
  }

  if (elapsed >=
      CALIBRATION_DURATION_MS) {

    calibrationDone = true;
  }
}

// Diagnostic fault injection (off unless -DVIB_FAULT_AT_S is set): every read is
// treated as failed from VIB_FAULT_AT_S seconds after boot for VIB_FAULT_MS ms.
#ifdef VIB_FAULT_AT_S
#ifndef VIB_FAULT_MS
#define VIB_FAULT_MS 5000
#endif
static bool faultInjectActive(unsigned long nowMs) {
  static int phase = 0;
  const unsigned long startMs = (unsigned long)(VIB_FAULT_AT_S) * 1000UL;
  if (phase == 0 && nowMs >= startMs) {
    phase = 1;
    Serial.println("[FAULT-INJECT] start");
  }
  if (phase == 1 && nowMs >= startMs + (unsigned long)(VIB_FAULT_MS)) {
    phase = 2;
    Serial.println("[FAULT-INJECT] stop");
  }
  return phase == 1;
}
#else
static bool faultInjectActive(unsigned long) { return false; }
#endif

static const unsigned long TIMING_PRINT_MS = 60000UL;
static unsigned long timingWindowStart = 0;
static unsigned long lastGoodReadUs = 0;
static unsigned long timingN = 0;
static unsigned long timingOver50 = 0;
static unsigned long timingReadErrors = 0;
static double timingSum = 0;
static double timingSumSq = 0;
static float timingMin = 0;
static float timingMax = 0;

static void timingRecordRead(bool ok) {
  if (!ok) {
    timingReadErrors++;
    return;
  }
  unsigned long nowUs = micros();
  if (lastGoodReadUs != 0) {
    float dtMs = (unsigned long)(nowUs - lastGoodReadUs) / 1000.0f;
    if (timingN == 0 || dtMs < timingMin) timingMin = dtMs;
    if (timingN == 0 || dtMs > timingMax) timingMax = dtMs;
    timingSum += dtMs;
    timingSumSq += (double)dtMs * dtMs;
    if (dtMs > 50.0f) timingOver50++;
    timingN++;
  }
  lastGoodReadUs = nowUs;
}

static void timingPrintIfDue(unsigned long nowMs) {
  if (nowMs - timingWindowStart < TIMING_PRINT_MS) return;
  timingWindowStart = nowMs;

  double mean = timingN ? timingSum / timingN : 0.0;
  double var = timingN ? timingSumSq / timingN - mean * mean : 0.0;
  if (var < 0) var = 0;

  Serial.printf(
      "[TIMING] n=%lu mean_ms=%.3f min_ms=%.3f max_ms=%.3f std_ms=%.3f "
      "over_50ms=%lu read_errors=%lu\n",
      timingN, mean, timingN ? timingMin : 0.0f, timingN ? timingMax : 0.0f,
      sqrt(var), timingOver50, timingReadErrors);

  timingN = 0;
  timingOver50 = 0;
  timingReadErrors = 0;
  timingSum = 0;
  timingSumSq = 0;
  timingMin = 0;
  timingMax = 0;
}

void vibrationUpdate() {

  float ax, ay, az;
  unsigned long nowMs = millis();
  bool readOk = !faultInjectActive(nowMs) && mpuReadAccel(ax, ay, az);

  timingRecordRead(readOk);
  timingPrintIfDue(nowMs);
  pumpPollSerial();
  pumpStateUpdate(nowMs);

  if (!sensorOk) {
    if (nowMs - lastInitAttempt >= SENSOR_REINIT_MS) {
      lastInitAttempt = nowMs;
      mpuInit();
    }
    if (!readOk) {
      recoverStart = 0;
      return;
    }
    if (recoverStart == 0) recoverStart = nowMs;
    if (nowMs - recoverStart < SENSOR_RECOVER_MS) return;

    sensorOk = true;
    lastGoodMs = nowMs;
    recoverStart = 0;
    resetSampleHistory();
    currentStatus = "OK";
    Serial.println("[Vibration] sensor recovered");
  } else if (!readOk) {
    if (nowMs - lastGoodMs >= SENSOR_TIMEOUT_MS) {
      sensorOk = false;
      currentStatus = "SENSOR ERROR";
      lastInitAttempt = nowMs;
      recoverStart = 0;
      lastGoodReadUs = 0;
      stoppedDetector.reset();
      loggingActiveEvent = false;
      Serial.println("[Vibration] SENSOR ERROR: no valid read for 2000 ms");
    }
    return;
  } else {
    lastGoodMs = nowMs;
  }

  float magnitude =
      sqrt(
          ax * ax +
          ay * ay +
          az * az);

  float vibration =
      magnitude - 9.8;

  sampleBuffer[sampleIndex] =
      vibration;

  sampleIndex++;

  if (sampleIndex >=
      SAMPLE_WINDOW) {

    sampleIndex = 0;
    bufferFull = true;
  }

  float rms =
      computeRMS();

  // Save the latest RMS reading for the dashboard.
  currentRMS = rms;

  if (CALIBRATION_MODE) {

    calibrationUpdate(rms);
    return;
  }

  bool isElevatedNow =
      (rms >
       warningThreshold());

  elevatedCount -=
      elevatedHistory[
          elevatedIndex];

  elevatedHistory[
      elevatedIndex] =
      isElevatedNow;

  elevatedCount +=
      isElevatedNow
          ? 1
          : 0;

  elevatedIndex++;

  if (elevatedIndex >=
      ELEVATED_HISTORY_SIZE) {

    elevatedIndex = 0;
    elevatedHistoryFull =
        true;
  }

  bool warningCondition =
      false;

  if (elevatedHistoryFull) {

    float elevatedFraction =
        (float)elevatedCount /
        ELEVATED_HISTORY_SIZE;

    warningCondition =
        (elevatedFraction >=
         WARNING_FRACTION_THRESHOLD);
  }

  bool aboveCautionNow =
      (rms >
       cautionThreshold());

  int windowCount = bufferFull ? SAMPLE_WINDOW : sampleIndex;
  float windowSpread = vh::windowStd(sampleBuffer, windowCount);

  bool stopped = stoppedDetector.update(
      nowMs, windowSpread, pumpExpectedRunning(), bufferFull);

  if (pumpState == PS_OFF) {

    currentStatus =
        "OFF SCHEDULE";

  } else if (stopped) {

    currentStatus =
        "STOPPED";

  } else if (warningCondition) {

    currentStatus =
        "WARNING";

  } else if (
      aboveCautionNow) {

    currentStatus =
        "CAUTION";

  } else {

    currentStatus =
        "OK";
  }

  static unsigned long lastStdPrint = 0;
  if (nowMs - lastStdPrint >= 1000) {
    lastStdPrint = nowMs;
    Serial.printf("[STD] std=%.4f n=%d stopped=%d\n",
                  windowSpread, windowCount, stopped ? 1 : 0);
  }

  if (aboveCautionNow) {

    if (!loggingActiveEvent) {

      loggingActiveEvent =
          true;

      activeEventStart =
          millis();

      formatEventTime(activeEventWhen, sizeof(activeEventWhen), activeEventStart);

      activeEventPeak =
          rms;

      activeEventReachedWarning =
          false;

    } else if (
        rms >
        activeEventPeak) {

      activeEventPeak =
          rms;
    }

    if (warningCondition) {

      activeEventReachedWarning =
          true;
    }

  } else if (
      loggingActiveEvent) {

    logEventEnd();
  }

  trendAccumulator +=
      rms;

  trendSampleCount++;

  if (millis() -
          lastTrendTime >=
      TREND_INTERVAL_MS) {

    lastTrendTime =
        millis();

    if (trendSampleCount > 0) {

      trendBuffer[
          trendIndex] =
          trendAccumulator /
          trendSampleCount;

      trendIndex++;

      if (trendIndex >=
          TREND_BUFFER_SIZE) {

        trendIndex = 0;
        trendBufferFull =
            true;
      }
    }

    trendAccumulator =
        0;

    trendSampleCount =
        0;
  }

  if (millis() -
          lastPrintTime >=
      PRINT_INTERVAL_MS) {

    lastPrintTime =
        millis();

    Serial.print(
        "[Vibration] Status: ");

    Serial.print(
        currentStatus);

    Serial.print(
        " | RMS: ");

    Serial.print(
        rms,
        3);

    Serial.print(
        " | Magnitude: ");

    Serial.print(
        magnitude,
        3);

    Serial.print(
        " | X: ");

    Serial.print(
        ax,
        3);

    Serial.print(
        " Y: ");

    Serial.print(
        ay,
        3);

    Serial.print(
        " Z: ");

    Serial.println(
        az,
        3);
  }
}


// ======================================================
// DASHBOARD ACCESS FUNCTIONS
// ======================================================

String vibrationGetStatus() {
  return currentStatus;
}

float vibrationGetRMS() {
  return currentRMS;
}

float vibrationGetBaseline() {
  return BASELINE_RUNNING_RMS;
}

float vibrationGetCautionThreshold() {
  return cautionThreshold();
}

float vibrationGetWarningThreshold() {
  return warningThreshold();
}


// ======================================================
// TREND ACCESS FUNCTIONS
// ======================================================

int vibrationGetTrendCount() {
  return trendBufferFull
             ? TREND_BUFFER_SIZE
             : trendIndex;
}

float vibrationGetTrendPoint(
    int minutesAgo) {

  int count =
      vibrationGetTrendCount();

  if (minutesAgo < 0 ||
      minutesAgo >= count) {

    return -1;
  }

  int idx =
      trendIndex -
      1 -
      minutesAgo;

  if (idx < 0)
    idx +=
        TREND_BUFFER_SIZE;

  return trendBuffer[idx];
}
