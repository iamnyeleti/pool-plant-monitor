# Pool Plant Monitor — Vibration and RTC/Schedule Modules
## Complete Development Documentation

**Project:** Pool Plant Monitor and Preventative Maintenance Advisor (EEE4022S, SW-3)
**Supervisor:** Dr Simon Winberg | **Co-supervisor:** Kai Goodall
**Board (both modules):** ESP32 DOIT DEVKIT V1, 38-pin
**Document purpose:** A complete, unsummarised record of everything discussed, decided, built, tested, and found across the Vibration and RTC/Schedule modules. This is a working reference document, not report text — nothing has been softened, condensed, or left out because it seemed minor. Every section states clearly whether something is VERIFIED (confirmed by a real measurement or observation), BUILT (exists in code but not fully tested), PLANNED (decided but not yet built), or OPEN (unresolved, needs a decision or missing information from you).

---

# PART 1: VIBRATION MODULE

## 1. Purpose and Brief Alignment

The vibration module exists to monitor pump motor vibration and flag early signs of mechanical failure. This is a direct requirement from the project brief, quoted exactly:

> "extended periods of anomalous motor strain or elevated vibration (lasting beyond a few minutes) serve as early warning indicators of impending mechanical failure."

Two things in that sentence shaped the entire design: the word "extended" (which is why a duration-based escalation logic exists at all, not just a threshold crossing), and "lasting beyond a few minutes" (which is the literal source of the 2-minute WARNING duration constant — more on this under Open Items, since it was never independently confirmed against Winberg's actual intent).

The operator-facing output is deliberately a plain status word — OK, CAUTION, or WARNING — not a raw sensor voltage or acceleration figure, consistent with the general design principle followed across this whole project: a pool operator should never need to interpret a number, only a status.

---

## 2. Hardware — Starting Point and Evolution

### 2.1 Where it started
- **Board:** ESP32 DOIT DEVKIT V1, 38-pin, Micro-USB. Listed in the original components table at **R159.80**.
- **Sensor:** MPU-6050 accelerometer/gyroscope breakout board, **R59.00**.
- **Total project budget constraint:** ~R2000 across all modules.

### 2.2 Final, current wiring (post-DS3231 integration)

| Wire | MPU6050 pin | Goes to ESP32 | Notes |
|---|---|---|---|
| Data | SDA | **GPIO21** | Shared I2C bus with DS3231 |
| Clock | SCL | **GPIO22** | Shared I2C bus with DS3231 |
| Power | VCC | **3.3V** | |
| Ground | GND | **GND** | |
| Address select | AD0 | **3.3V** | Critical — see Section 2.3 below |

### 2.3 Why AD0 is wired to 3.3V — the address collision

The MPU6050 defaults to I2C address **0x68** when AD0 is left disconnected (floating low). The DS3231 RTC module, added later to the same I2C bus (GPIO21/22), also defaults to **0x68**, with no address-select pin of its own to change it. Since two devices cannot coexist at the same I2C address on the same bus, the MPU6050's AD0 pin was deliberately wired to 3.3V, which shifts its address to **0x69**. This freed 0x68 for the DS3231 exclusively.

This matters for the code specifically: because the current `vibration.cpp` talks to the chip by a **hardcoded address constant**, not through a library that auto-detects the device, this constant had to be manually updated the moment the physical wiring changed:

```cpp
// Before DS3231 was added:
static const uint8_t MPU_ADDR = 0x68;

// After DS3231 was added and AD0 was wired to 3.3V:
static const uint8_t MPU_ADDR = 0x69;
```

This is a real, documented instance of a hardware change requiring a corresponding, deliberate software change — worth noting explicitly in a methodology or limitations discussion, since it's exactly the kind of coupling between hardware and firmware that a real commissioning process needs to account for.

### 2.4 Mounting — a real, tested finding

Early testing attempted to mount the MPU6050 by resting it on a wooden board positioned near a test fan, rather than fixing it directly to the vibration source. This was tested across multiple fan speeds and with the fan blades physically blocked by a pen — **zero measurable RMS change occurred in any condition**. This was diagnosed as a mounting/coupling problem, not a sensor or code fault, since the sensor had already been independently proven live via a flip test and a multi-direction movement test (see Section 6).

The fix: the sensor was taped **directly onto the fan's motor casing**, with no intermediary material. This is the only mounting method that has produced a measurable response in this project. This is a genuine, citable finding about sensor coupling, worth stating plainly in a report: rigid, direct contact to the vibration source is required; a resting or loosely-coupled mount effectively isolates the sensor from the vibration it is meant to measure.

---

## 3. Development Environment and Simulation (Wokwi)

### 3.1 Simulation setup
- `wokwi.toml` configured pointing at the compiled firmware binary and ELF file.
- `diagram.json` wired a simulated `wokwi-esp32-devkit-v1` board to a simulated `wokwi-mpu6050` chip: SDA → D21, SCL → D22, plus 3V3 and GND.
- Board target in PlatformIO: `esp32doit-devkit-v1`.

### 3.2 A real tooling problem, and how it was resolved
VS Code's built-in "Wokwi Serial" terminal extension produced **no output at all**, even though the build itself succeeded cleanly. This was not a build failure — the firmware compiled and uploaded into the simulator correctly, only the serial monitor view inside VS Code showed nothing.

An RFC2217 serial-forwarding workaround was attempted (`wokwi.toml` configured with `rfc2217ServerPort = 4000`, paired with the Microsoft "Serial Monitor" VS Code extension). This connected briefly but then closed immediately — also unreliable.

**Root cause:** never conclusively diagnosed. At the time, this was treated as a known, currently open bug in the Wokwi VS Code extension itself, not something fixable from the project side.

**Actual fix used for the rest of the project's Wokwi work:** the VS Code-embedded serial path was abandoned entirely. All further Wokwi testing was done by mirroring the project on the **browser-based wokwi.com simulator** instead, which displayed serial output correctly.

### 3.3 Original simulated logic (before any real-hardware work)
The very first version of the detection logic, tested only in simulation, was a simple **two-state system**: OK or WARNING, triggered by a single fixed threshold of 1.5 (no baseline-relative calculation yet, no CAUTION tier, no duration requirement).

**Verified test results from this original version, in Wokwi:**
- Resting state: Magnitude ≈ 9.807, RMS ≈ 0.007, Status: OK.
- Simulated Z pushed to 1.3–1.5g by hand: Magnitude 14.219, RMS 4.419, Status: WARNING.

This confirmed the basic magnitude/RMS pipeline worked correctly in principle, before any of the later refinements (CAUTION tier, rolling-window duration logic, calibration mode) were added.

---

## 4. Full Chronological Development Narrative

This section tells the story in the order it actually happened, including every significant pivot, not just the end state.

**Stage 1 — Wokwi-only development.** The project began entirely in simulation. The original Adafruit-library-based code was written, built, and the two-state OK/WARNING logic above was verified working in the browser simulator (after working around the VS Code serial bug).

**Stage 2 — Logic refinement, still in simulation.** Through iterative design discussion, the detection logic was progressively rebuilt, each change deliberately reasoned through before being coded:
1. A **duration requirement** was added, directly motivated by the brief's phrase "lasting beyond a few minutes" — a single instantaneous threshold crossing was judged insufficient to represent a genuine fault.
2. A **CAUTION tier** was introduced as a lighter, immediate signal, deliberately given *no* duration requirement, so it could fire the moment something looked elevated, while WARNING remained reserved for sustained severity.
3. The duration check itself was redesigned from a **single continuous timer** (which would reset to zero the instant RMS dipped even briefly below threshold) to a **rolling-fraction window** instead. The reasoning given at the time: a simple reset-on-dip timer would completely miss a real but *intermittent* fault — for example, a problem that spikes above threshold for a few seconds every few minutes, never continuously, would never accumulate enough uninterrupted time to trigger a simple timer, but genuinely represents a developing problem worth flagging.
4. A **10-slot rolling event log** was added, recording timestamp, duration, peak RMS, and whether WARNING was reached, for every excursion above CAUTION.
5. A **24-hour trend buffer** (1440 slots, one averaged point per minute) was added for longer-term pattern visibility.
6. A dedicated **calibration mode** was added, specifically so the baseline threshold value could eventually be measured from a real motor rather than guessed.

**Stage 3 — Literature and standards grounding.** An uploaded academic paper, *"Arduino Uno-Based Vibration Monitoring System Design Using The Sensor Mpu5060"* (Alwin Juhendra and Razali, Applied Bachelor of Production and Maintenance Mechanical Engineering, Bengkalis State Polytechnic, Indonesia), was reviewed directly. Its own validation testing against a reference vibration meter found an average accuracy of only **76.26%**, with a Root Mean Square Error of **0.3330 m/s²**, and one individual measurement with over 90% deviation. This was used as direct, citable support for the decision not to treat raw MPU6050 output as an absolute physical truth, and to lean on baseline-relative thresholds instead.

The same paper's own measured data also showed a clear dominance of vibration on the X and Z axes over Y (their own words: *"the dominant vibration direction occurs on the X axis and the Z axis, while the Y-axis has the lowest vibration level"*), which the paper itself attributed partly to *"the installation position of the MPU6050 sensor."* This became directly relevant later, during this project's own axis-investigation testing (Stage 5 below).

The official **InvenSense MPU-6000/MPU-6050 Product Specification, Revision 3.4** (document PS-MPU-6000A-00) was uploaded in full and cross-checked directly against the code, not taken from memory. Specific findings from this datasheet are detailed throughout this document wherever relevant.

ISO standards research was also done directly against the actual standard text (not a secondary source):
- **ISO 10816-3** was checked and found *not* to apply — its scope is machines above 15kW, and pumps were explicitly removed from its scope in a 2009 revision.
- **ISO 10816-7** was identified as the correct standard for this project's actual target (facility-scale pumps above 1kW), but it is based on **RMS velocity (mm/s)**, not acceleration. This project deliberately measures acceleration (m/s²) instead, a stated limitation (see Section 9), to avoid the drift and complexity of numerical integration on embedded hardware.

**Stage 4 — Real hardware bring-up begins.** The MPU6050 was wired to the physical ESP32 DOIT DEVKIT V1 board for the first time. The very first real-hardware test immediately failed: `MPU6050 not found. Check wiring (SDA/SCL) and power.`

This triggered a full, systematic debugging investigation, documented in detail in Section 8 ("Problems and Fixes"). The short version: an I2C scanner sketch confirmed the physical wiring was correct (a device reliably responded at address 0x68), and a direct WHO_AM_I register read (register 0x75) returned **0x72**, not the genuine InvenSense chip's expected 0x68. This specific unit is a clone/rebrand variant. Adafruit's library performs an internal check against the expected WHO_AM_I value and refuses to proceed if it doesn't match — even though the chip itself functions correctly for real sensor readings. The fix was to abandon the Adafruit library entirely and rewrite the sensor-reading code to communicate directly with the chip over I2C by register address.

**Stage 5 — Axis behaviour investigation.** Once real hardware was confirmed working, per-axis X/Y/Z debug printing was added to the Serial output (previously only the combined magnitude and RMS were printed). Two deliberate physical tests were then run:

1. **A manual orientation flip test.** With the board lying flat, component-side up, Z read consistently around +9.8 to +10. Physically inverting the board, Z read around **−10.3**. This confirmed the sign convention was correct (an accelerometer measures the *support force* opposing gravity, not gravity directly, so the sign depends on which way the chip's own Z axis happens to point), and the magnitude formula's correctness was confirmed by the fact that squaring removes the sign entirely, so `sqrt(x²+y²+z²)` is unaffected by which way the board is oriented.

2. **A manual multi-direction movement test** (up/down, left/right, forward/back, performed as one continuous sequence rather than cleanly separated motions). This showed real, significant movement on X and Y during the test, not just Z — directly contradicting an initial assumption that Z would dominate because it's the "vertical" axis. The actual explanation, worked out from first principles and later confirmed against the InvenSense datasheet's own axis diagram (Section 11.1, "Orientation of Axes of Sensitivity and Polarity of Rotation"): a hand-lift is rarely a perfectly straight vertical motion, it includes small, unavoidable tilts, and because X and Y lie flat in the board's own plane while only Z points straight up, even a small tilt rotates a noticeable portion of gravity's pull onto X and/or Y. This was judged a genuine, correct, physically-grounded finding, not a sensor fault.

**Stage 6 — Fan mounting investigation** (already detailed in Section 2.4 above): the wooden-board mounting failure, diagnosis, and the fix of taping directly to the motor casing.

**Stage 7 — First real calibration run.** With the sensor properly mounted on the running fan, `CALIBRATION_MODE` was set to `true`, and the built-in 20-second warm-up plus 90-second recording procedure was run to completion. Result: **Average RMS: 1.0747**. This value was written into the code as `BASELINE_RUNNING_RMS`, replacing the original placeholder value of `1.0`. `CALIBRATION_MODE` was then set back to `false`.

**Stage 8 — DLPF (Digital Low-Pass Filter) change.** During the same code rewrite that bypassed the Adafruit library, the DLPF register (0x1A) was also changed from its original value of `0x04` (approximately 21Hz bandwidth) to `0x00` (the widest available bandwidth, minimal filtering). This change was prompted by a hobbyist forum post (user "SillyOldDuffer" on model-engineer.co.uk), which claimed the MPU6050 can effectively be designed to filter out vibration internally, since it's primarily intended for drone/quadcopter orientation sensing, where vibration is unwanted noise rather than the signal of interest. **This change was bundled into the same rewrite as the library bypass, so its individual effect was never isolated as its own separate test** — it is not known with certainty whether this filter change was actually necessary, or whether the mounting fix alone would have been sufficient.

**Stage 9 — Phone vibration test (inconclusive).** An attempt was made to test the sensor against a phone's haptic vibration motor, using a vibration-generating app, as an alternative, more controllable vibration source. You reported the status "stayed OK" during this test. However, whether the sensor was genuinely taped to the phone or merely resting against it was never confirmed, and the actual RMS value observed during the test was never provided despite being requested. **This remains an open, unresolved data point** — it's not clear whether this was a genuine null result or a mounting-coupling issue repeating itself.

**Stage 10 — DS3231 integration.** The DS3231 RTC module was added to the same I2C bus. This required: wiring AD0 to 3.3V (Section 2.3), updating `MPU_ADDR` from `0x68` to `0x69` in the code, and restructuring `main.cpp` to call both `vibrationSetup()`/`vibrationUpdate()` and `scheduleSetup()`/`scheduleUpdate()` together. Coexistence of both devices on the shared bus was explicitly tested and confirmed across multiple sessions (see Part 2 for full RTC-side detail).

**Stage 11 — Connected-flag restructuring.** `main.cpp` was restructured to use boolean flags (`VIBRATION_CONNECTED`, `RTC_CONNECTED`) guarding each module's setup/update calls, so that physically disconnecting one sensor (for isolated testing of another) would not cause the whole program to hang — since `vibrationSetup()`'s own `mpuInit()` function enters an infinite `while(1) delay(10);` loop if the chip isn't found, which would otherwise block every other module from ever running. This exact pattern (a boolean "is this hardware actually installed" flag) was also applied in the occupancy module for the not-yet-arrived radar/PIR sensor, so it's a deliberately reused, consistent pattern across the whole project, not a one-off.

**Stage 12 — Dashboard-access functions added.** A set of getter functions was added to `vibration.cpp` specifically so a future dashboard module could read the module's live state: `vibrationGetRMS()`, `vibrationGetBaseline()`, `vibrationGetCautionThreshold()`, `vibrationGetWarningThreshold()`, alongside a new `currentRMS` variable updated every loop. These are **built but not yet tested against an actual dashboard**, since no dashboard module exists yet in this project.

**Stage 13 — platformio.ini review.** The actual, current `platformio.ini` file was reviewed directly (pasted in full). Two findings: first, it still lists `adafruit/Adafruit MPU6050@^2.2.6` and `adafruit/Adafruit Unified Sensor@^1.1.14` in `lib_deps`, even though the current code no longer uses either library — these are now dead, unused dependency declarations, left over from before the library-bypass rewrite. Second, the file has `monitor_filters = default, log2file` active, meaning PlatformIO has very likely been writing real, timestamped log files to disk on every monitoring session — a potential source of genuine historical serial data that was flagged but **never confirmed as actually checked**.

**Stage 14 — Second calibration run.** A second calibration run was performed under current conditions (DS3231 attached, `MPU_ADDR = 0x69`). Result: **Average RMS: 0.6926** — roughly 35% lower than the first run's 1.0747. You directly identified the likely cause yourself: the test fan runs on battery power, so its actual physical vibration strength depends on the battery's current charge level, not solely on the selected speed setting. This means "speed 2" in one session and "speed 2" in a later session are not guaranteed to represent the same physical vibration input. This was accepted as a genuine, reasoned explanation, and is a real, citable limitation of using a battery-powered rig as a vibration surrogate for a mains-powered real pump. The decision made: use the fresh 0.6926 value for the test session being run that day, for internal consistency with the actual physical conditions present during that session's formal testing, rather than reuse the older, no-longer-representative 1.0747 figure.

**Stage 15 — Calibration-trigger design discussion.** A design discussion concluded that **automatic self-calibration at boot was deliberately rejected** as the wrong design, for two stated reasons: first, a system has no way to verify that whatever vibration state exists the instant it powers on is actually "healthy" — if a fault already existed at boot, auto-calibration would lock that faulty state in as the new normal, permanently blinding the system to it. Second, plant rooms lose power regularly in the South African context (loadshedding was cited directly), meaning "recalibrate on every boot" would mean recalibrating far more often than desirable, with zero human verification each time. You then proposed, in your own words, a **two-step trigger-then-confirm design**: a control (physical button, or eventually a dashboard control) that first captures and *displays* a calibration result, with a genuinely separate, deliberate second action required to actually *apply* it as the new active baseline. This was confirmed as the correct design, directly analogous to the existing `CALIBRATION_MODE` flag's manual-trigger behaviour, just with the "apply" step made explicit and separate rather than automatic. It was also noted that this would require the resulting value to be written to the ESP32's flash storage (via the `Preferences` library) to survive a reboot, rather than only existing as an in-memory variable. **This entire trigger-then-confirm mechanism is PLANNED, not built.**

**Stage 16 — Formal test protocol design and execution.** A structured test protocol was designed, consisting of four conditions: resting baseline (Test A), CAUTION-band response (Test B), sustained WARNING plus response-latency measurement (Test C), and recovery timing (Test D), originally planned at 3 repeats each.

Before the formal trials, a known-issue was re-surfaced and corrected: an earlier monitor session had shown the boot line `=== CALIBRATION MODE ===`, despite the source file on disk correctly reading `CALIBRATION_MODE = false`. This was diagnosed as a stale build — the board was running an older compiled firmware than what the current source file actually contained, because a rebuild/reupload had not been performed after the last edit. The fix was simply to rebuild and reupload, after which the boot sequence was confirmed clean.

**Trial 1** was then run and logged in full (a single, clean on/off cycle). **Trial 2** was run and logged, and during this trial, a real transient hardware event occurred: six consecutive I2C communication errors (`[E][Wire.cpp:499] requestFrom(): i2cWriteReadNonStop returned Error -1`) appeared in the log around the 17:25:57 timestamp, lasting under a second, after which readings resumed normally with no reboot and no lockup. This is treated as a genuine, observed reliability event — a transient I2C dropout, most plausibly caused by electrical noise from the running motor — not a fault in the logic itself, since the system recovered gracefully on its own.

After Trial 2 was initially analysed as a single on/off cycle, you clarified that it had actually been run as a **four-stage step test**: off (baseline) → first fan speed for 1 minute → second fan speed for 1 minute → a third (final) speed → off again. This changes how the log should be read, and the exact boundary between the first and second speed stages could not be confidently identified from the RMS data alone, since that section of the log shows sustained oscillation between OK and CAUTION rather than a single clean jump. **This remains an open item**, pending your actual recorded dial-change times, needed to properly segment that log into its true four stages rather than the provisional single-cycle reading given initially.

---

## 5. Physics and Mathematical Foundations

**Core derivation (Newton's Second Law + Hooke's Law):** for a system undergoing vibration, `a(t) = −ω²x(t)` — acceleration is proportional to displacement, with the angular frequency squared as the constant of proportionality and a sign flip (restoring force opposes displacement).

**Magnitude calculation:**
```
magnitude = sqrt(x² + y² + z²)
```
This is the Euclidean norm of the three-axis acceleration vector. It was deliberately chosen for being **orientation-independent** — the combined magnitude does not change based on how the sensor happens to be mounted, only the *split* across the three individual axes changes with orientation. This was later empirically confirmed during the flip test and the axis-movement test (Section 4, Stage 5): even though individual X/Y/Z readings changed substantially with orientation and motion, the combined magnitude during the "held at a new, tilted position" phase of the lift test remained close to the original resting value (~9.8–10), correctly reporting OK rather than falsely flagging an orientation change as ongoing vibration.

**Gravity subtraction:**
```
vibration = magnitude − 9.8
```
This removes the constant ~1g (9.8 m/s²) offset that gravity contributes even when the sensor is perfectly still, so that the "vibration" value represents deviation from rest, not the raw gravitational reading. This was derived from first principles as a small-vibration approximation (effectively a first-order Taylor/binomial approximation), and was noted at the time to be more sensitive to vertical-axis vibration than horizontal, given the specific way the subtraction interacts with the vector magnitude.

**A measured discrepancy worth noting:** the flip test found the "inverted" reading was approximately **−10.3**, not the expected clean **−9.8**. This was not treated as an error, but cross-checked directly against the InvenSense datasheet's own stated tolerances: **Initial Calibration Tolerance: ±3%**, and **Zero-G Output (Z-axis specifically): ±80mg** (approximately ±0.78 m/s²). Combined, these two documented tolerances comfortably account for the observed ~0.5 m/s² discrepancy, confirming it as expected sensor variation rather than a fault.

**Cross-axis sensitivity:** the datasheet's Section 11.4.5 states a documented **±2% cross-axis sensitivity** figure, meaning some amount of a signal on one axis "leaking" into another is expected, normal sensor behaviour, separate from (and smaller than) the physical-tilt explanation that dominates the axis-movement test results.

---

## 6. Sensitivity, Scaling, and Register Configuration

**Accelerometer range selected:** ±4g, set via `AFS_SEL = 1`, written as `0x08` to register `0x1C` (ACCEL_CONFIG).

**Sensitivity Scale Factor used in code:** `LSB_PER_G = 8192.0`. This was directly cross-checked against the InvenSense datasheet's Table 6.2 (Accelerometer Specifications), which states exactly **8,192 LSB/g for AFS_SEL=1** — confirming the conversion constant in the code is correct against the manufacturer's own published specification, not an assumption.

**Conversion applied per axis:**
```cpp
x = (rawX / LSB_PER_G) * G_TO_MS2;  // where G_TO_MS2 = 9.8
```

**DLPF (Digital Low-Pass Filter) register:** `0x1A` (CONFIG), currently set to `0x00` (widest bandwidth, minimal filtering), changed from the original `0x04` (~21Hz) — see Stage 8 above for the reasoning and the caveat that this change's individual necessity was never isolated from the mounting fix.

**Chip wake-up:** register `0x6B` (PWR_MGMT_1) is written with `0x00` on setup, to bring the chip out of its default sleep state.

---

## 7. Full Current Code

### 7.1 `include/vibration.h`
```cpp
#ifndef VIBRATION_H
#define VIBRATION_H

#include <Arduino.h>

void vibrationSetup();
void vibrationUpdate();
String vibrationGetStatus();

int vibrationGetTrendCount();
float vibrationGetTrendPoint(int minutesAgo);
float vibrationGetRMS();
float vibrationGetBaseline();
float vibrationGetCautionThreshold();
float vibrationGetWarningThreshold();

#endif
```

### 7.2 `src/vibration.cpp` (current, as of the DS3231 integration and second calibration)
```cpp
// vibration.cpp
#include "vibration.h"
#include <Wire.h>

// AD0 is wired to 3.3V, shifting the MPU6050 from 0x68 to 0x69,
// since the DS3231 now occupies 0x68 on the same shared bus.
static const uint8_t MPU_ADDR = 0x69;

// Added so the dashboard can access the most recent RMS value.
static float currentRMS = 0.0f;

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
  Wire.write(0x00); // widest bandwidth, minimal filtering
  if (Wire.endTransmission() != 0) return false;

  return true;
}

static bool mpuReadAccel(float &x, float &y, float &z) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) return false;

  Wire.requestFrom((int)MPU_ADDR, 6);
  if (Wire.available() < 6) return false;

  int16_t rawX = (Wire.read() << 8) | Wire.read();
  int16_t rawY = (Wire.read() << 8) | Wire.read();
  int16_t rawZ = (Wire.read() << 8) | Wire.read();

  const float LSB_PER_G = 8192.0;
  const float G_TO_MS2 = 9.8;

  x = (rawX / LSB_PER_G) * G_TO_MS2;
  y = (rawY / LSB_PER_G) * G_TO_MS2;
  z = (rawZ / LSB_PER_G) * G_TO_MS2;

  return true;
}

static const bool CALIBRATION_MODE = false; // Set to true to enable calibration mode
static const unsigned long CALIBRATION_WARMUP_MS = 20000;
static const unsigned long CALIBRATION_DURATION_MS = 90000;

static bool calibrationWarmedUp = false;
static bool calibrationDone = false;
static unsigned long calibrationPhaseStart = 0;
static double calibrationSum = 0;
static unsigned long calibrationCount = 0;
static unsigned long lastCalibrationPrint = 0;

// Real measured baseline, from the second (battery-condition-matched)
// fan calibration run performed on today's test session.
static const float BASELINE_RUNNING_RMS = 0.6926;

// Threshold multipliers
static const float CAUTION_MULTIPLIER = 1.2;
static const float WARNING_MULTIPLIER = 1.5;

static float cautionThreshold() {
  return BASELINE_RUNNING_RMS * CAUTION_MULTIPLIER;
}

static float warningThreshold() {
  return BASELINE_RUNNING_RMS * WARNING_MULTIPLIER;
}

static const unsigned long WARNING_DURATION_MS = 2UL * 60UL * 1000UL;
static const float WARNING_FRACTION_THRESHOLD = 0.5;

static const int SAMPLE_WINDOW = 50;
static float sampleBuffer[SAMPLE_WINDOW];

static int sampleIndex = 0;
static bool bufferFull = false;

static unsigned long lastPrintTime = 0;
static const unsigned long PRINT_INTERVAL_MS = 250;

static const unsigned long ASSUMED_LOOP_INTERVAL_MS = 20;

static const int ELEVATED_HISTORY_SIZE = WARNING_DURATION_MS / ASSUMED_LOOP_INTERVAL_MS;

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
};

static const int EVENT_LOG_SIZE = 10;
static VibrationEvent eventLog[EVENT_LOG_SIZE];

static int eventLogIndex = 0;
static bool loggingActiveEvent = false;

static unsigned long activeEventStart = 0;
static float activeEventPeak = 0;
static bool activeEventReachedWarning = false;

static const unsigned long TREND_INTERVAL_MS = 60UL * 1000UL;
static const int TREND_BUFFER_SIZE = 1440;

static float trendBuffer[TREND_BUFFER_SIZE];

static int trendIndex = 0;
static bool trendBufferFull = false;

static unsigned long lastTrendTime = 0;

static double trendAccumulator = 0;
static unsigned long trendSampleCount = 0;

static float computeRMS() {
  float sumSquares = 0;
  int count = bufferFull ? SAMPLE_WINDOW : sampleIndex;
  if (count == 0) return 0;
  for (int i = 0; i < count; i++) {
    sumSquares += sampleBuffer[i] * sampleBuffer[i];
  }
  return sqrt(sumSquares / count);
}

static void logEventEnd() {
  eventLog[eventLogIndex].timestamp = activeEventStart;
  eventLog[eventLogIndex].durationMs = millis() - activeEventStart;
  eventLog[eventLogIndex].peakRMS = activeEventPeak;
  eventLog[eventLogIndex].reachedWarning = activeEventReachedWarning;
  eventLogIndex = (eventLogIndex + 1) % EVENT_LOG_SIZE;
  loggingActiveEvent = false;
}

void vibrationSetup() {
  Wire.begin();

  if (!mpuInit()) {
    Serial.println("MPU6050 not found. Check wiring (SDA/SCL) and power.");
    while (1) delay(10);
  }
  Serial.println("Vibration module ready.");

  if (CALIBRATION_MODE) {
    Serial.println("=== CALIBRATION MODE ===");
    calibrationPhaseStart = millis();
  }
}

static void calibrationUpdate(float rms) {
  unsigned long elapsed = millis() - calibrationPhaseStart;
  if (!calibrationWarmedUp) {
    if (elapsed >= CALIBRATION_WARMUP_MS) {
      calibrationWarmedUp = true;
      calibrationPhaseStart = millis();
      calibrationSum = 0;
      calibrationCount = 0;
      Serial.println("Warm-up done. Recording now...");
    }
    return;
  }
  if (calibrationDone) {
    if (millis() - lastCalibrationPrint >= 2000) {
      lastCalibrationPrint = millis();
      Serial.print("===== CALIBRATION COMPLETE ===== Average RMS: ");
      Serial.println(calibrationSum / calibrationCount, 4);
    }
    return;
  }
  calibrationSum += rms;
  calibrationCount++;
  if (millis() - lastCalibrationPrint >= 2000) {
    lastCalibrationPrint = millis();
    Serial.print("Calibrating... RMS: ");
    Serial.println(rms, 3);
  }
  if (elapsed >= CALIBRATION_DURATION_MS) calibrationDone = true;
}

void vibrationUpdate() {
  float ax, ay, az;
  if (!mpuReadAccel(ax, ay, az)) {
    return;
  }

  float magnitude = sqrt(ax * ax + ay * ay + az * az);
  float vibration = magnitude - 9.8;

  sampleBuffer[sampleIndex] = vibration;
  sampleIndex++;
  if (sampleIndex >= SAMPLE_WINDOW) {
    sampleIndex = 0;
    bufferFull = true;
  }

  float rms = computeRMS();

  // Save the latest RMS reading for the dashboard.
  currentRMS = rms;

  if (CALIBRATION_MODE) {
    calibrationUpdate(rms);
    return;
  }

  bool isElevatedNow = (rms > warningThreshold());

  elevatedCount -= elevatedHistory[elevatedIndex];
  elevatedHistory[elevatedIndex] = isElevatedNow;
  elevatedCount += isElevatedNow ? 1 : 0;

  elevatedIndex++;
  if (elevatedIndex >= ELEVATED_HISTORY_SIZE) {
    elevatedIndex = 0;
    elevatedHistoryFull = true;
  }

  bool warningCondition = false;
  if (elevatedHistoryFull) {
    float elevatedFraction = (float)elevatedCount / ELEVATED_HISTORY_SIZE;
    warningCondition = (elevatedFraction >= WARNING_FRACTION_THRESHOLD);
  }

  bool aboveCautionNow = (rms > cautionThreshold());

  if (warningCondition) {
    currentStatus = "WARNING";
  } else if (aboveCautionNow) {
    currentStatus = "CAUTION";
  } else {
    currentStatus = "OK";
  }

  if (aboveCautionNow) {
    if (!loggingActiveEvent) {
      loggingActiveEvent = true;
      activeEventStart = millis();
      activeEventPeak = rms;
      activeEventReachedWarning = false;
    } else if (rms > activeEventPeak) {
      activeEventPeak = rms;
    }
    if (currentStatus == "WARNING") {
      activeEventReachedWarning = true;
    }
  } else if (loggingActiveEvent) {
    logEventEnd();
  }

  trendAccumulator += rms;
  trendSampleCount++;

  if (millis() - lastTrendTime >= TREND_INTERVAL_MS) {
    lastTrendTime = millis();
    if (trendSampleCount > 0) {
      trendBuffer[trendIndex] = trendAccumulator / trendSampleCount;
      trendIndex++;
      if (trendIndex >= TREND_BUFFER_SIZE) {
        trendIndex = 0;
        trendBufferFull = true;
      }
    }
    trendAccumulator = 0;
    trendSampleCount = 0;
  }

  if (millis() - lastPrintTime >= PRINT_INTERVAL_MS) {
    lastPrintTime = millis();
    Serial.print("[Vibration] Status: ");
    Serial.print(currentStatus);
    Serial.print(" | RMS: ");
    Serial.print(rms, 3);
    Serial.print(" | Magnitude: ");
    Serial.print(magnitude, 3);
    Serial.print(" | X: ");
    Serial.print(ax, 3);
    Serial.print(" Y: ");
    Serial.print(ay, 3);
    Serial.print(" Z: ");
    Serial.println(az, 3);
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
  return trendBufferFull ? TREND_BUFFER_SIZE : trendIndex;
}

float vibrationGetTrendPoint(int minutesAgo) {
  int count = vibrationGetTrendCount();
  if (minutesAgo < 0 || minutesAgo >= count) {
    return -1;
  }
  int idx = trendIndex - 1 - minutesAgo;
  if (idx < 0) idx += TREND_BUFFER_SIZE;
  return trendBuffer[idx];
}
```

### 7.3 Earlier version, for reference (before DS3231/dashboard integration)
The version of `vibration.cpp` that existed immediately after the library-bypass rewrite, before the DS3231 was added, used `MPU_ADDR = 0x68` and `BASELINE_RUNNING_RMS = 1.0747`, and did not yet include the `currentRMS` variable or any of the dashboard-access getter functions. This earlier version is not reproduced in full here since it is functionally superseded, but its existence and exact difference from the current version (address and baseline value) is recorded in Section 4 (Stages 7, 10, 12, 14) above.

### 7.4 The `main.cpp` integration point (current)
```cpp
#include <Arduino.h>
#include "vibration.h"
#include "schedule.h"
#include "dashboard.h"
#include "water_quality.h"

// Set a flag to false when that hardware is unplugged, so its setup
// does not stop the whole program. Set it back to true once it is
// wired in again.
static const bool VIBRATION_CONNECTED = true;  // MPU6050
static const bool RTC_CONNECTED = true;         // DS3231 clock

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("Pool Plant Monitor starting...");

  if (VIBRATION_CONNECTED) vibrationSetup();
  if (RTC_CONNECTED) scheduleSetup();
  waterQualitySetup();
  dashboardSetup();
}

void loop() {
  if (VIBRATION_CONNECTED) vibrationUpdate();
  if (RTC_CONNECTED) scheduleUpdate();
  waterQualityUpdate();
  dashboardUpdate();
  delay(20);
}
```
**Note:** `waterQualitySetup()`/`waterQualityUpdate()` and `dashboardSetup()`/`dashboardUpdate()` are called unconditionally in this file, with no connected-flag guarding them the way vibration and RTC have. This was flagged as a real risk during the formal test session (since `water_quality.cpp` and `dashboard.cpp` are being developed in a separate chat session, their boot-time behaviour when hardware is disconnected is unverified here), and for the actual formal vibration/RTC test trials, these two calls were temporarily commented out to isolate the test from unverified code.

---

## 8. Problems and Fixes — Full Debugging Log

**Problem 1: VS Code Wokwi serial terminal showed no output.**
Build succeeded; only the embedded serial view failed. RFC2217 forwarding was attempted as a workaround and also failed (connected then immediately closed). Root cause never conclusively diagnosed — treated as a known open bug in the Wokwi VS Code extension. **Fix:** abandoned the VS Code-embedded path, used the browser-based wokwi.com simulator for all further Wokwi work.

**Problem 2: `MPU6050 not found` on first real-hardware test.**
Wiring was suspected first. An I2C scanner sketch was written and run, which found a device responding reliably and consistently at address **0x68** — ruling out a wiring fault. A direct WHO_AM_I register read (register `0x75`) was then performed, returning **0x72** — not the genuine InvenSense chip's documented ID of `0x68`. This specific MPU6050 unit is a clone or rebrand variant. The Adafruit library's `begin()` function performs an internal check against the expected WHO_AM_I value and refuses to proceed (`begin()` returns `false`) if it doesn't match, even though the chip itself responds correctly and would function normally for actual sensor readings. **Fix:** `vibration.cpp` was rewritten entirely to bypass the Adafruit library and communicate with the chip directly via raw I2C register reads/writes (`Wire.h` only).

**Problem 3: Zero measurable response during the first fan vibration test.**
Already detailed in Section 2.4. Sensor/code were already proven functionally correct via the flip test and movement test, ruling those out. **Root cause:** the sensor was resting on an intermediate wooden board rather than being rigidly coupled to the actual vibration source. **Fix:** taped the sensor directly onto the fan's motor casing.

**Problem 4: A stale-build mismatch during formal testing.**
A monitor session showed the line `=== CALIBRATION MODE ===` printing at boot, despite the source file on disk correctly reading `CALIBRATION_MODE = false`. **Root cause:** the board was running an older compiled firmware image than what the current source file actually contained — a rebuild and reupload had not been performed since the last source edit. **Fix:** rebuilt and reuploaded; the next boot sequence was confirmed clean, with no calibration-mode text present.

**Problem 5: A transient I2C communication dropout during Trial 2.**
At approximately 17:25:57 during the formal Trial 2 test session, six consecutive error lines appeared in the serial log:
```
[320020][E][Wire.cpp:499] requestFrom(): i2cWriteReadNonStop returned Error -1
[320049][E][Wire.cpp:499] requestFrom(): i2cWriteReadNonStop returned Error -1
[320078][E][Wire.cpp:499] requestFrom(): i2cWriteReadNonStop returned Error -1
[320107][E][Wire.cpp:499] requestFrom(): i2cWriteReadNonStop returned Error -1
[320136][E][Wire.cpp:499] requestFrom(): i2cWriteReadNonStop returned Error -1
[320165][E][Wire.cpp:499] requestFrom(): i2cWriteReadNonStop returned Error -1
```
This spans roughly 145 milliseconds based on the internal millisecond counters shown in the error messages themselves (320020 to 320165). Normal `[Vibration] Status: WARNING` lines resume immediately in the very next log entries, with no reboot message, no `MPU6050 not found` lockup, and no corrupted or nonsensical readings afterward. **No fix was applied or deemed necessary** — this is recorded as a genuine, observed, self-recovering reliability event, most plausibly caused by electrical noise from the running motor given the timing (mid-WARNING, i.e., during active, strong vibration), rather than a fault in the firmware logic.

---

## 9. Testing — Calibration Runs

### 9.1 First calibration run
- **Conditions:** sensor taped directly to the fan motor casing; fan running steadily.
- **Procedure:** `CALIBRATION_MODE = true`, 20-second warm-up period, followed by a 90-second recording period, result averaged and printed automatically by the firmware.
- **Result:** `===== CALIBRATION COMPLETE ===== Average RMS: 1.0747` (you reported this number directly; the exact verbatim console line including this result was not itself pasted into the conversation, only the number).
- Set as `BASELINE_RUNNING_RMS = 1.0747` at the time.

### 9.2 Second calibration run
- **Conditions:** same fan, same mounting method, but on a different day/session, with the battery at a different charge level.
- **Full raw log captured**, including the countdown through the warm-up period, the "Warm-up done. Recording now..." transition, and 40+ individual "Calibrating... RMS: X.XXX" lines during the 90-second recording window, ranging roughly between 0.526 and 0.901, averaging to:
- **Result:** `===== CALIBRATION COMPLETE ===== Average RMS: 0.6926`.
- **Reasoning for the discrepancy (your own observation, accepted as correct):** the fan is battery-powered, so its actual vibration output depends on current battery charge, not purely the selected speed setting — meaning the two calibration sessions are not guaranteed to represent the same physical input, even at a nominally identical speed setting.
- **Decision:** `BASELINE_RUNNING_RMS` updated to `0.6926` for consistency with the conditions present during the same day's formal test trials.

### 9.3 Derived thresholds under the current (second) calibration
```
CAUTION threshold = 0.6926 × 1.2 = 0.8311 m/s²
WARNING threshold = 0.6926 × 1.5 = 1.0389 m/s²
```

---

## 10. Testing — Formal Protocol and Trial Results

### 10.1 Protocol as designed
Four conditions, originally planned at 3 repeats each:
- **Test A — Resting baseline:** fan off, 3 × 30-second recordings.
- **Test B — CAUTION response:** fan at a speed producing RMS between the caution and warning thresholds, held ~20 seconds, 3 repeats.
- **Test C — Sustained WARNING + latency:** fan at a speed above the warning threshold, held continuously, real clock time noted at onset and at first WARNING line, 3 repeats.
- **Test D — Recovery timing:** fan switched off after each Test C trial, real clock time noted at switch-off and at first OK line, 3 repeats (piggybacking on Test C's cooldown).

### 10.2 Trial 1 — results
A single, clean on/off cycle (later confirmed by you as not containing repeat trials — subsequent oscillations in the same session were manual fan adjustments made while reviewing earlier results, not separate formal repeats).

| Transition | Timestamp |
|---|---|
| Last clean OK before sustained climb | 15:27:58 |
| Sustained CAUTION begins | 15:27:59 |
| First WARNING | 15:29:24 |
| First OK after recovery | 15:30:39 |

**Onset-to-WARNING latency:** 15:29:24 − 15:27:59 = **85 seconds**.
**WARNING episode duration:** 15:30:39 − 15:29:24 = **75 seconds**.

**Notable finding:** the transition from WARNING directly to OK, observed at 15:30:38 → 15:30:39 in the raw log, **skipped CAUTION entirely** — the line at 15:30:38 reads `Status: WARNING | RMS: 0.609`, and the very next line at 15:30:39 reads `Status: OK | RMS: 0.664`, with no intervening CAUTION line. This is a real, observed instance of a scenario discussed hypothetically earlier in this project: because WARNING is governed by a 2-minute rolling fraction (not the instantaneous RMS value), it's possible for the instantaneous RMS to already be below the CAUTION threshold by the moment the rolling fraction itself finally drops below 50%, causing a direct WARNING→OK jump in a single loop cycle, visually skipping CAUTION.

**Repeat count achieved:** n = 1 (not the originally planned n = 3). Decision made, with your agreement, to treat this as a deliberate scope decision (citing this project's proof-of-concept framing) rather than attempt further repeats in that session.

### 10.3 Trial 2 — results (provisional, pending clarification)
Initially analysed as a single on/off cycle, later clarified by you as actually a **four-stage step test**: off (baseline) → first speed, held for 1 minute → second speed, held for 1 minute → a third, final speed → off.

**Provisional boundaries** (from RMS pattern inference only, not confirmed timestamps):

| Phase | Best-estimate window | Observed pattern |
|---|---|---|
| Off (baseline) | 17:20:38 → ~17:21:51 (~73s) | Clean, steady OK, RMS ~0.25 |
| Speed 1 | ~17:21:52 → ~17:22:56 (~64s) | Oscillating between OK and CAUTION, never cleanly settling |
| Speed 2 | ~17:22:56 → 17:23:53 (~57s) | Same oscillating pattern continues, slightly more CAUTION-heavy |
| Speed 3 (final/sustained) | 17:23:54 → 17:26:58 (~3 min) | Climbs into sustained CAUTION, then clean WARNING, then recovers |
| Off | 17:26:58 onward | Settles back to clean OK |

**Under the provisional single-sustained-climb reading** (treating 17:23:54 onward as one continuous escalation, which may not be fully accurate given the true step structure):

| Transition | Timestamp |
|---|---|
| Last OK before final unbroken run | 17:23:53 |
| Start of sustained elevation | 17:23:54 |
| First WARNING | 17:25:02 |
| First OK after recovery | 17:26:58 |

**Onset-to-WARNING latency (provisional):** 17:25:02 − 17:23:54 = **68 seconds**.
**WARNING episode duration:** 17:26:58 − 17:25:02 = **1 minute 56 seconds (116 seconds)**.

**Important caveat, stated explicitly and not to be lost:** the boundary between "Speed 1" and "Speed 2" above is a rough, even-split guess over a ~2-minute stretch of genuinely ambiguous, oscillating data — it is not a real, identifiable feature in the log, unlike the other three boundaries, which are each clean, unambiguous transitions. **This is an OPEN item**, pending your actual recorded dial-change times for the three speed changes (off→1, 1→2, 2→3), without which this log cannot be properly segmented into its true four stages.

### 10.4 Combined trial comparison

| | Trial 1 | Trial 2 (provisional) |
|---|---|---|
| Structure | Single on/off cycle | 4-stage step test (speed boundaries unconfirmed) |
| Onset→WARNING latency | 85s | 68s (provisional) |
| WARNING duration | 75s | 116s |
| Notable event | WARNING→OK skipped CAUTION entirely | 6-line I2C dropout mid-WARNING, self-recovered within ~1 second |

**Total repeats achieved against the original 3-per-condition design: n = 2 distinct sessions**, not 3, with Trial 2's internal structure still requiring clarification before its numbers can be considered final.

---

## 11. Design Decisions — Full Reasoning Log

This section expands on every deliberate design choice made in this module, with the full reasoning behind each, not condensed to a table.

**RMS over raw peak values.** RMS was used throughout as the vibration severity metric rather than tracking raw peak acceleration values, on the basis that RMS is the metric actually used by the ISO 10816 standards family, and that it naturally smooths out momentary noise spikes that a raw peak-tracking approach would be far more sensitive to.

**Magnitude as `sqrt(x²+y²+z²)` rather than per-axis tracking.** This was chosen specifically so the system's core detection logic would not depend on exactly how the sensor happened to be mounted or oriented — a vibration along any axis, or any combination of axes, contributes to the same single combined number. The cost of this choice, identified later through the axis-investigation testing and the Juhendra/Razali paper's own findings, is that genuine directional information (which axis the vibration is actually occurring on) is discarded by this approach. Per-axis RMS tracking was discussed as a valuable future addition specifically because of this finding, but has not been implemented.

**Gravity subtraction via a fixed 9.8 constant.** Chosen as a simple, direct way to remove the constant resting offset, derived from first principles rather than taken from an external source. The known limitation, confirmed by the flip test, is that it assumes an idealised 9.8 m/s², while the actual chip reads closer to 9.8–10.3 depending on orientation and the chip's own documented tolerance band — meaning the "vibration" value the system actually computes carries a small amount of built-in offset error, bounded by the datasheet's own stated ±3% calibration tolerance and ±80mg zero-g tolerance.

**Baseline-relative percentage thresholds, not a fixed absolute number or a statistical mean±standard-deviation approach.** The stated reasoning: the project brief limits how long a baseline-recording procedure can reasonably run, so a sample large enough to produce a statistically reliable standard deviation was judged impractical within that constraint. A percentage-above-measured-baseline approach was recommended by an uploaded reference document at the time (its exact full citation was not recorded and remains an open item), and was adopted directly.

**A rolling-fraction window for WARNING escalation, replacing an original single reset-on-dip timer.** The original, simpler design, a timer that counts up while RMS is elevated and resets instantly to zero the moment RMS dips even briefly, was identified as having a real, serious gap: it would completely fail to catch a genuinely concerning but *intermittent* fault, one that spikes above threshold repeatedly but briefly, never sustaining a single, uninterrupted stretch long enough to trigger a simple timer. The rolling-fraction approach instead asks "what fraction of the last 2 minutes was spent above the WARNING threshold," which correctly accumulates credit across multiple separate short episodes, not just one continuous one.

**A separate CAUTION tier, explicitly with no duration requirement at all.** This was a deliberate design split: CAUTION exists to give an *immediate*, lighter-weight signal the instant RMS crosses above the caution threshold, with zero sustained-duration requirement, while WARNING is reserved specifically for the brief's own "lasting beyond a few minutes" language. One direct, important consequence of this design, discovered and discussed at length: a value that sits permanently in the CAUTION band (for example, steady at 1.5 against a 1.61 WARNING threshold) will **never** escalate to WARNING under the current logic, no matter how long it persists, because the rolling-fraction window specifically and only tracks time spent above the *WARNING* threshold, not the CAUTION threshold. This was confirmed as intentional, correct behaviour matching the CAUTION/WARNING split's actual design intent (CAUTION = elevated but not fault-level; WARNING = fault-level and sustained), not a gap — though it was also explicitly flagged as a real design choice worth being aware of and open to reconsideration if you decide sustained-but-moderate elevation should eventually also escalate.

**Direct I2C register access instead of any Arduino sensor library.** Forced by the discovery that this specific MPU6050 unit's WHO_AM_I value (0x72) does not match what the Adafruit library's internal safety check expects (0x68), causing `begin()` to always fail even though the chip itself communicates and measures correctly. The alternative of patching or forking the Adafruit library, or searching for a different, less strict library, was not pursued; direct register access was chosen instead as the more controllable, transparent solution, at the cost of needing to maintain register-level code directly (register addresses, bit values, and the raw-to-physical-units conversion math) rather than relying on a library's abstraction.

**Manual, human-triggered calibration rather than any form of automatic self-calibration.** Explicitly rejected automatic calibration-at-boot for two concrete reasons: a system booting up has no way to independently verify that whatever vibration state exists at that exact moment is genuinely healthy, so an existing, real fault present at boot time would be permanently baked in as the new "normal" baseline; and because plant-room power loss (loadshedding was specifically cited) would make "recalibrate on every boot" trigger far more often than is sensible, with no human check involved each time. The chosen alternative — a human explicitly triggers calibration, confirms the equipment is in a known-good running state first, then the captured value is applied — mirrors how real industrial condition-monitoring systems are actually commissioned in practice. A further refinement, proposed directly by you, splits this into two genuinely separate steps: trigger-and-display a calibration result first, with a separate, deliberate confirm action required before that result is actually applied as the live baseline — specifically to prevent one mistimed calibration run (equipment not actually running yet, or briefly disturbed) from silently corrupting the active thresholds with no human review at all.

**Using the freshly-measured 0.6926 baseline instead of the earlier 1.0747 value for the current session's formal testing.** Reasoned directly from the battery-powered-fan limitation: since the two calibration runs may not represent the same actual physical vibration strength (due to battery charge variability), using whichever value was measured most recently, under the same conditions as the day's actual formal trials, was judged the internally consistent choice, even though it means the two calibration sessions are not directly comparable to one another as if they measured the same thing.

**Connected-flag guards in `main.cpp`, rather than letting a missing sensor halt the whole program.** This follows directly from the fact that `mpuInit()` failing causes an infinite blocking loop (`while(1) delay(10);`), which, if left unguarded, would prevent every other module in the program (including ones with no connection to vibration at all) from ever running, the moment the MPU6050 happened to be disconnected for any reason (including deliberate, temporary disconnection for isolated testing of another module). The same pattern was deliberately reused for the RTC (`RTC_CONNECTED`) and for the occupancy module's not-yet-arrived radar/PIR sensor, making it a consistent, repeated pattern across the whole project rather than a one-off fix.

---

## 12. Current Known Limitations

- `WARNING_DURATION_MS = 2 minutes` remains an interpretation of the brief's "a few minutes" phrasing, never independently confirmed with Winberg, and never derived from measuring a real motor's actual startup transient behaviour.
- No real pump has ever been available for testing; both calibration runs and both formal trials were conducted against a battery-powered hand fan, which differs from a real deployment both in vibration *signature* (frequency/amplitude character) and in power-source *stability* (battery charge drift vs. stable mains power).
- The DLPF bandwidth change (0x04 → 0x00) was bundled into the same code rewrite as the library-bypass fix, so its individual necessity, separate from the mounting fix, has never been isolated as its own controlled test.
- The sensor's own raw measurement accuracy (the ~10.3 vs. expected ~9.8 offset found in the flip test) has never been corrected in code — only the system-level baseline threshold has been calibrated, not the underlying sensor reading itself.
- `platformio.ini` still lists the now-unused `adafruit/Adafruit MPU6050` and `adafruit/Adafruit Unified Sensor` library dependencies, left over from before the direct-register rewrite.
- The dashboard-access getter functions (`vibrationGetRMS()` etc.) are implemented but have never been tested against an actual running dashboard, since no dashboard module currently exists in this project.
- The manual calibration trigger-and-confirm design (a physical button or dashboard control, with persistent flash storage via `Preferences`) is fully specified in discussion but entirely unbuilt.
- Per-axis RMS tracking, and hourly CAUTION/WARNING event counters, were both discussed as valuable, well-justified additions but neither has been implemented.
- The formal test protocol's originally planned repeat count (3 per condition) was not achieved; actual testing produced n=1 (Trial 1) plus one step-test session (Trial 2) whose internal phase boundaries remain only provisionally identified.

---

## 13. Open Questions Requiring Your Input

1. Was the sensor genuinely taped or merely resting during the phone-vibration test, and what actual RMS value did it show? (Stage 9, Section 4.)
2. Did you ever check whether `log2file`-generated session logs already exist on disk, under `.pio\build\esp32doit-devkit-v1\`? (Stage 13, Section 4.)
3. Your actual recorded dial-change timestamps for Trial 2's three fan-speed changes (off→1, 1→2, 2→3), needed to properly segment that log. (Section 10.3.)
4. Do you want the unused Adafruit MPU6050/Unified Sensor library lines removed from `platformio.ini`, or left in place and explicitly noted as historical in your documentation?
5. Is `WARNING_DURATION_MS = 2 minutes` being finalised as a documented, justified assumption for your report, or is it still pending actual confirmation from Winberg?
6. Do you want to pursue a third, cleanly isolated repeat trial to reach the originally planned n=3, or formally accept n=2 (with Trial 2's structure clarified) as the final, reported test count?

---

# PART 2: RTC / SCHEDULE MODULE

## 1. Purpose

The DS3231 real-time clock module provides the system with awareness of actual wall-clock date and time, independent of how long the ESP32 itself has been powered on. Its purpose within the broader project is twofold, though only the first half is currently built:

1. **Immediate, standalone purpose (built):** maintain accurate, genuinely persistent timekeeping, surviving power loss, so that other modules (vibration's event log and trend buffer, eventually a schedule-gating function for the occupancy camera) can be given real timestamps rather than only a "time since this boot" value from `millis()`.
2. **Intended future purpose (not yet built):** support schedule-comparison logic — specifically, determining whether the pool is currently inside an "open" or "closed" operating window, which the occupancy module's camera-activation logic was designed around but has not yet been connected to. This half of the module's purpose is explicitly on hold, pending two things: occupancy reaching basic detection capability, and Winberg's ethics confirmation regarding image retention during the camera's closed-hours operation.

This module does not map to a single, explicitly named line in the brief the way vibration does; its justification is reasoning worked through directly in this project (ties to the brief's "track daily pool occupancy patterns" language, and an "unexpected OFF" fault-detection concept discussed in relation to loadshedding).

---

## 2. Hardware

**Module:** DS3231 real-time clock breakout board.

**Wiring — shares the exact same I2C bus as the MPU6050:**

| Wire | DS3231 pin | Goes to ESP32 |
|---|---|---|
| Data | SDA | **GPIO21** (same physical wire as the MPU6050's SDA) |
| Clock | SCL | **GPIO22** (same physical wire as the MPU6050's SCL) |
| Power | VCC | **3.3V** |
| Ground | GND | **GND** |

The DS3231 has no AD0-equivalent address-select pin, so it always sits at its default I2C address of **0x68**. This is precisely why the MPU6050's own address had to be moved away from 0x68 (to 0x69, via its AD0 pin) once this module was added — see Part 1, Section 2.3, for the full reasoning.

**Library used:** `adafruit/RTClib@^2.1.4`, declared in `platformio.ini`'s `lib_deps`.

---

## 3. Software Evolution

### 3.1 Original, minimal version (checkpoint 1 — proving the clock works at all)
The very first version of `schedule.cpp` did nothing but confirm the RTC itself was alive and correctly keeping time:
```cpp
#include <Arduino.h>
#include <Wire.h>
#include "RTClib.h"
#include "schedule.h"

static RTC_DS3231 rtc;

void scheduleSetup() {
  if (!rtc.begin()) {
    Serial.println("DS3231 not found. Check wiring (SDA/SCL) and power.");
    while (1) delay(10);
  }

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting to compile time...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  } else {
    Serial.println("RTC already keeping time correctly, leaving it alone.");
  }

  Serial.println("RTC module ready.");
}

void scheduleUpdate() {
  DateTime now = rtc.now();
  Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
                now.year(), now.month(), now.day(),
                now.hour(), now.minute(), now.second());
  delay(1000);
}
```

**A deliberate design choice embedded in this very first version, worth explaining precisely:** the code only sets the RTC's time (`rtc.adjust(...)`, using the compiler's own build timestamp via `F(__DATE__)`/`F(__TIME__)`) if `rtc.lostPower()` returns true — meaning the chip's own backup battery genuinely failed or was never installed. If the RTC still has valid, continuously-kept time (battery intact, never lost power), the code deliberately leaves it alone and trusts the chip's own internally-tracked time. This is the entire point of using a battery-backed RTC instead of simply tracking elapsed time with `millis()` from each boot — if every firmware reupload or power cycle reset the clock back to "whenever this code was compiled," the RTC would provide no real advantage over just using `millis()`.

### 3.2 Restructured with a "not installed yet" flag
Before the physical jumper wires needed to actually wire the DS3231 were available, the module was restructured with a boolean guard, following the same pattern used elsewhere in this project (see Part 1, Section 4, Stage 11, and the identical pattern used for the occupancy module's radar/PIR):
```cpp
// Flip this to true once the DS3231 is physically wired in with jumper wires.
static const bool RTC_INSTALLED = false;
```
With `scheduleSetup()` and `scheduleUpdate()` both checking this flag first and returning immediately (doing nothing) if it's `false`. This allowed the rest of the program (specifically, vibration) to continue being developed and tested without being blocked by RTC hardware that wasn't physically available yet. Once the physical wiring was actually completed, this flag was flipped to `true`.

### 3.3 Current version (post-wiring, integrated with vibration)
```cpp
#include <Arduino.h>
#include <Wire.h>
#include "RTClib.h"
#include "schedule.h"

// Set to true since the DS3231 is now physically wired in.
static const bool RTC_INSTALLED = true;

static RTC_DS3231 rtc;
static bool rtcReady = false;

void scheduleSetup() {
  if (!RTC_INSTALLED) {
    Serial.println("RTC not installed yet, schedule module inactive.");
    rtcReady = false;
    return;
  }

  if (!rtc.begin()) {
    Serial.println("DS3231 not found. Check wiring (SDA/SCL) and power.");
    rtcReady = false;
    while (1) delay(10);
  }

  rtcReady = true;

  if (rtc.lostPower()) {
    Serial.println("RTC lost power, setting to compile time...");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  } else {
    Serial.println("RTC already keeping time correctly, leaving it alone.");
  }

  Serial.println("RTC module ready.");
}

void scheduleUpdate() {
  if (!RTC_INSTALLED || !rtcReady) return;

  DateTime now = rtc.now();
  static unsigned long lastRtcPrint = 0;

  if (millis() - lastRtcPrint >= 1000) {
    lastRtcPrint = millis();
    Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n",
                  now.year(), now.month(), now.day(),
                  now.hour(), now.minute(), now.second());
  }
}

bool scheduleIsReady() {
  return RTC_INSTALLED && rtcReady;
}

String scheduleGetTimeString() {
  if (!RTC_INSTALLED || !rtcReady) return "RTC unavailable";

  DateTime now = rtc.now();
  char buffer[24];
  snprintf(buffer, sizeof(buffer),
           "%04d-%02d-%02d %02d:%02d:%02d",
           now.year(), now.month(), now.day(),
           now.hour(), now.minute(), now.second());
  return String(buffer);
}
```

**Two refinements visible in this version, beyond the original checkpoint-1 code:**
1. A second guard variable, `rtcReady`, was added alongside the compile-time `RTC_INSTALLED` flag, specifically so a *runtime* hardware failure (the chip physically not responding when `rtc.begin()` is called, even though the code expected it to be installed) is also tracked and checked, not just the build-time intent.
2. `scheduleUpdate()`'s time-printing was changed from a blocking `delay(1000)` to a non-blocking `millis()`-based interval check (`if (millis() - lastRtcPrint >= 1000)`), since a blocking delay inside `scheduleUpdate()` would stall every other module's `update()` call in the shared `loop()`, which only became a problem once RTC was running alongside vibration (and eventually other modules) in the same loop rather than being tested in isolation.

### `include/schedule.h`
```cpp
#ifndef SCHEDULE_H
#define SCHEDULE_H

void scheduleSetup();
void scheduleUpdate();

#endif
```
(Note: the current `schedule.cpp` also exposes `scheduleIsReady()` and `scheduleGetTimeString()`, which are not yet declared in this header file — a minor inconsistency worth tidying up, since any other `.cpp` file wanting to call those two functions would need the header updated first.)

---

## 4. Testing — The Power-Cycle Persistence Proof

This is the single most important test performed on this module, since it's the one thing that actually distinguishes "a genuine battery-backed real-time clock" from "a clock that merely happened to display the correct time once."

**Procedure:** with the RTC running and printing correct time, the ESP32's USB cable was physically unplugged, left disconnected for roughly 30 seconds, then reconnected.

**First boot (before the unplug):** log showed `RTC already keeping time correctly, leaving it alone.`, with time correctly advancing second by second from approximately 00:59:45 onward.

**A monitor disconnect/reconnect event occurred** (`Disconnected (ClearCommError failed...)` / `Reconnecting to COM5     Connected!`) — this was identified as just the monitor software's own connection hiccup, not a genuine power cycle, and is distinct from the actual deliberate unplug test described next.

**Second boot (the actual, deliberate unplug/replug test):** the full ESP32 boot sequence printed again (`rst:0x1 (POWERON_RESET)...`), confirming a genuine hardware reset had occurred, and the very next relevant lines read: `RTC already keeping time correctly, leaving it alone.` — **not** the "lost power, setting to compile time" branch — followed by time correctly continuing to advance from approximately 01:00:08 onward, i.e., genuinely picking up from roughly where real elapsed time would place it, not resetting back to an earlier point or to the firmware's compile time.

**This is treated as a definitive, VERIFIED pass:** the DS3231's own onboard battery genuinely kept time running independently of the ESP32's own power state, which is the entire functional point of using this chip over a `millis()`-based software clock.

---

## 5. Testing — Coexistence with the Vibration Module on the Shared I2C Bus

Across multiple separate test sessions (the two formal vibration trials documented in Part 1, Section 10, plus the dedicated RTC power-cycle test above), vibration status lines and RTC timestamp lines were observed printing correctly interleaved in the same serial log, with no corruption, no I2C address confusion, and no blocking of one module by the other. This stands as ongoing, repeated confirmation that the address-conflict fix (MPU6050 moved to 0x69, DS3231 remaining at 0x68) genuinely works in practice, not just in theory — including during the one session where a real transient I2C error did occur (Part 1, Section 8, Problem 5), which affected only the MPU6050 read calls and did not disrupt the RTC's own readings in the same log.

---

## 6. Design Decisions

**The "not installed yet" boolean flag pattern (`RTC_INSTALLED`).** Adopted specifically to avoid blocking the rest of the program's development while waiting for physical jumper wires to become available. This is the same pattern applied to vibration's own `VIBRATION_CONNECTED` flag and to the occupancy module's radar/PIR sensors — a deliberately consistent, reused approach across this entire project for handling "hardware that will exist eventually, but doesn't exist in this exact moment."

**Only setting the RTC's time when `lostPower()` is true, never unconditionally.** This is the core design decision that makes the RTC meaningfully different from a software-only clock. Setting the time unconditionally on every boot (for example, always running `rtc.adjust()` using the compile timestamp) would mean the clock's value is really just "whenever this firmware was last compiled," which defeats the entire purpose of having a battery-backed chip at all. Checking `lostPower()` first ensures the chip's own continuously-running internal clock is trusted and preserved across ordinary power cycles and reflashes, and is only ever reset in the one case where that trust is actually broken (a dead or missing backup battery, or a genuine first-ever power-up).

**A `rtcReady` runtime flag added alongside the compile-time `RTC_INSTALLED` flag.** Reasoned as necessary because `RTC_INSTALLED` only reflects what the programmer *expects* to be true at compile time, not what's *actually* true at the moment the code runs — a genuine hardware fault (loose wire, dead chip) occurring despite `RTC_INSTALLED` being set to `true` needs its own, separate runtime check, which `rtcReady` provides.

**Non-blocking `millis()`-based print interval, replacing an earlier blocking `delay(1000)`.** This became necessary specifically once the RTC module started running inside the same shared `loop()` as the vibration module (and, prospectively, other modules). A `delay(1000)` call inside `scheduleUpdate()` would stall the entire `loop()` function for a full second on every single call, preventing `vibrationUpdate()` (and any other module's update function) from running during that stall — a serious problem for a module like vibration, whose entire RMS/threshold logic depends on being called at a consistent, short interval.

---

## 7. Known Limitations and Open Items

- `schedule.h` has not been updated to declare `scheduleIsReady()` or `scheduleGetTimeString()`, even though `schedule.cpp` now defines and exposes both — any other file wanting to call them currently cannot do so cleanly without the header being updated first.
- The module's actual intended future purpose — schedule-comparison logic to gate the occupancy camera's active hours — remains entirely unbuilt. This is explicitly, deliberately on hold, not forgotten: it is waiting on (a) the occupancy module reaching basic working presence-detection, and (b) Winberg's ethics confirmation regarding what happens to captured images during the camera's closed-hours operation.
- No experiment or test has been designed yet for whatever the eventual schedule-comparison logic will need to verify (for example: does the system correctly recognise "closed hours just started" at the right moment, accounting for a settling window to let stragglers leave, as was discussed in principle during the occupancy module's planning but never connected to actual RTC code).
- The DS3231's own documented hardware flaw (a charging-circuit issue that can damage a standard CR2032 coin cell if the board is powered continuously via USB for long periods) was identified very early in this project's component-sourcing phase, with a suggested mitigation of swapping to an LIR2032 rechargeable cell or removing the charging diode — **this mitigation has not been confirmed as actually applied to the physical board currently in use.**

---

*End of document. This file covers the Vibration and RTC/Schedule modules only, as requested. Water quality, occupancy, dashboard, and git/repository documentation were not included here per your instruction to focus on these two modules.*

---

# PART 3: EXTERNAL ENGINEERING REVIEW — Full Findings Log

An external, independent critical review was obtained covering the entire vibration subsystem, the RTC integration, and the proposed schedule/STOPPED additions. This section records every distinct finding from that review, each with the reviewer's reasoning and how it was responded to, not condensed into a summary.

**Finding 1 — Honest claim level.** The subsystem demonstrates three distinct levels of achievement, not one: (1) hardware measurement — VERIFIED; (2) anomaly detection under limited fan-test conditions — DEMONSTRATED, limited scope; (3) reliable prediction of actual pump mechanical failure — NOT DEMONSTRATED. The correct claim for this project is "baseline-relative vibration anomaly detection that may support preventative maintenance," not a validated fault-prediction system. Turning a fan from speed 1 to speed 3 is not equivalent to introducing a mechanical defect; a healthy motor running faster is expected to vibrate more, for entirely benign reasons.

**Finding 2 — Gravity subtraction is not fully orientation-neutral.** `sqrt(ax²+ay²+az²) − 9.8` is mathematically independent of how the axes are labelled, but not equally sensitive to all physical vibration directions relative to gravity. For vibration mostly aligned with gravity (vertical), the response is close to first-order (direct). For vibration perpendicular to gravity (horizontal), the response is approximately second-order: `v(t) ≈ ax(t)²/(2g)` for small horizontal ax, meaning small horizontal vibration is mathematically attenuated far more than small vertical vibration. A more conventional approach would remove the slow-moving gravity/DC component from each axis separately before combining the dynamic components, rather than subtracting a single fixed 9.8 from the combined magnitude. **Response/status:** confirmed as a real limitation, not previously stated this precisely. The current signal should be described as a gravity-referenced acceleration-magnitude residual, not implied to be a fully gravity-compensated vibration measurement, unless the per-axis approach is implemented and compared.

**Finding 3 — Sampling rate and aliasing risk.** `delay(20)` in `main.cpp` does not guarantee a 50Hz sample rate, only a minimum 20ms wait, before I2C reads, RTC calls, and printing add further, uncounted time. Even an idealised 50Hz gives a Nyquist limit of only 25Hz. The MPU6050's DLPF is currently set to `0x00` (~260Hz accelerometer bandwidth, confirmed directly against the InvenSense datasheet's Section 6.2 table, which states "Programmable Range: 5 ... 260 Hz"), creating a real mismatch, the filter now passes far more high-frequency content than the sampling rate can faithfully represent. The actual sampling interval has never been measured. **Status: HIGH PRIORITY, unresolved**, flagged as the most consequential open technical question. A diagnostic measurement has since been written (see Part 5) but not yet run.

**Finding 4 — Calibration trigger-confirm design is sound, but still only planned.** The proposed trigger→display→separate-confirm→persist workflow is a defensible, correct architecture, directly analogous to how real industrial condition-monitoring systems are commissioned. However, it remains documented as planned, not demonstrated working firmware. `CALIBRATION_MODE` still requires a manual code edit and reflash, there is no live operator-facing trigger yet.

**Finding 5 — The calibration discrepancy's cause was never isolated.** The two baseline runs (1.0747 vs 0.6926, a 35.6% difference) were explained by the battery-powered fan's charge-dependent speed, a plausible hypothesis, but neither battery voltage nor fan RPM was independently measured in either session. The correct, precise framing: the baseline changed substantially between sessions; battery state and resulting motor-speed variation are plausible contributing factors, but the cause has not been experimentally isolated. Other possible contributors (small mounting differences, fan condition, measurement timing, sensor variation) were not ruled out either.

**Finding 6 — Threshold multipliers are provisional, not validated.** `CAUTION_MULTIPLIER = 1.2` and `WARNING_MULTIPLIER = 1.5` are correct arithmetic against the baseline, but nothing in this project establishes that 20% and 50% above baseline correspond to a statistically unusual signal, a known fault severity, or a published pump condition-monitoring limit. They are prototype design parameters, not experimentally validated mechanical-fault thresholds.

**Finding 7 — The "90 seconds is too short for statistics" justification was weak.** The reviewer disagrees with the documented reasoning that statistical (mean ± standard deviation) thresholds were impractical purely due to short calibration time. 90 seconds at the existing sample rate is enough samples to estimate preliminary baseline mean, standard deviation, and percentiles, even accounting for correlated samples. The percentage-multiplier approach is not inherently wrong, it's simpler to commission and interpret, which is a legitimate reason for a prototype, but that should be the stated justification, not an implied inability to compute statistics from the available data.

**Finding 8 — WARNING timing is not what the documentation implied.** The system is not a simple two-minute continuous timer. It is a rolling 6000-sample history (built from `ASSUMED_LOOP_INTERVAL_MS = 20`), and WARNING triggers once at least 50% of that filled history is above the elevated threshold. Three concrete consequences: **(a)** WARNING does not require two continuous minutes of abnormal vibration, roughly one minute of intermittent elevated samples within the window can be sufficient once the history is full. **(b)** The history is defined by loop iterations, not actual elapsed time, if the loop slows down once other modules (dashboard, water quality) are integrated, the real-world duration represented by "6000 samples" silently changes without any code change. **(c)** WARNING can remain active after instantaneous vibration has already returned to normal, since old elevated samples remain in the history until they age out one at a time. This third behaviour is exactly what Trial 1's direct WARNING→OK transition (skipping CAUTION) demonstrated, confirmed as consistent with the algorithm's actual design, not a bug, but something that needs to be explained precisely in any write-up, not left implicit.

**Finding 9 — The Aldrini and Chihi paper, now verified.** Confirmed as a real, existing paper (Aldrini and Chihi, "Adaptive multi-scale threshold with time-gated evaluation," published via Springer/Journal of Intelligent Manufacturing, DOI 10.1007/s10845-026-02809-3). It genuinely supports the *principle* of time-gated evaluation to reduce false alarms and identify sustained deviations. It uses a more sophisticated adaptive approach with statistical thresholds and variable observation windows, and does **not** establish that two minutes specifically is correct for this project's pump-monitoring case. Usable to cite the principle of temporal confirmation; not usable as experimental justification for the specific 2-minute constant, which remains an engineering design parameter motivated by the brief's own wording.

**Finding 10 — The two trials are not equivalent repeats.** Trial 1 (single on/off cycle) and Trial 2 (multi-stage fan-speed test) have different structures, starting conditions, and waveform histories, and should not be presented as two repetitions of the same experiment. The 85-second and 68-second onset-to-WARNING figures were measured relative to *inferred* onset points in the log, not necessarily the precise instant RMS first crossed the elevated threshold, and should not be treated as precise, clean latency measurements.

**Finding 11 — The wrong RTC date, independently confirmed as not invalidating durations.** The reviewer independently reached the same conclusion already given in this document: an incorrect absolute RTC date does not automatically invalidate elapsed-duration calculations taken from the same continuously running clock, provided its rate stayed correct and it did not jump mid-test. The timing *method* still needs to be stated precisely in any report, not just the numbers.

**Finding 12 — An explicit list of what testing has not yet established.** False-alarm rate during long periods of healthy operation; repeatability under identical, controlled conditions; sensitivity to a known, repeatable physical disturbance; the actual relationship between sample rate and detection timing; how the firmware reports a sensor fault or disconnection; and, importantly, **whether a change in healthy operating speed is incorrectly interpreted as a fault** — increased motor speed alone is not evidence of unhealthy operation, and the current system has no way to distinguish "running faster because that's the scheduled/intended operating point" from "running faster because of a developing fault." For a realistic deployment, either the operating regime needs to be stable, or the threshold needs to account for multiple legitimate operating points. For this proof-of-concept's bounded scope, a fixed, commissioned operating regime is reasonable, but this limitation must be stated explicitly, not left implicit.

**Finding 13 — The I2C dropout's cause is unverified, and the bigger issue is what happens afterward.** The six-line, ~145ms I2C error burst during Trial 2 was attributed to motor-generated electrical noise, a plausible but *unverified* explanation, loose wiring, vibration-induced contact issues, and I2C bus timing are equally plausible alternatives that were never ruled out. More importantly: `if (!mpuReadAccel(ax, ay, az)) { return; }` means a failed read simply skips that loop iteration, with `currentStatus` left sitting at whatever it last was. A sensor that is actually disconnected or failing indefinitely would continue reporting its last real status, OK included, with no explicit fault state ever surfacing. **Needed:** an explicit `SENSOR ERROR` / `DATA UNAVAILABLE` status, driven by a timeout since the last successful read, not inferred from silence. Separately, the infinite blocking `while(1) delay(10);` loops in both `vibrationSetup()`'s and `scheduleSetup()`'s failure paths should be replaced with non-blocking error reporting, so one unexpectedly failed sensor doesn't halt every other, unrelated module. The `VIBRATION_CONNECTED`/`RTC_CONNECTED` flags only guard against a *known*, compile-time absence of hardware, they do nothing to protect against hardware that was expected to be present and fails at runtime.

**Finding 14 — Pool access schedule and pump operating schedule were conflated.** This is treated as the most significant architectural issue raised. Pool opening hours (when the public may swim) and the pump's actual required run schedule are not the same thing, a filtration pump may legitimately need to run outside public hours. The current single stored run-window, and the gating logic built around it (`scheduleShouldBeMonitoring()` / the `OFF SCHEDULE` status), is only valid if that window genuinely represents the **pump's own** operating schedule, not pool access hours. These must be two independently settable values, even though they can share the same RTC and the same underlying `Preferences` persistence mechanism. Four distinct concepts were named explicitly as needing to stay separate: facility access schedule, expected pump operating schedule, observed pump operating state (sensor evidence), and vibration health status. **Response:** accepted directly. A rename was proposed to make the current function's actual scope explicit, `scheduleShouldBeMonitoring()` → `pumpScheduleShouldBeRunning()`, with the occupancy module's eventual schedule treated as a second, separate stored window in the same file, never a reuse of this one. **This rename has been proposed but NOT YET applied in code** (see Part 5, Current State).

**Finding 15 — Low vibration alone does not prove the pump has stopped.** RMS below the proposed 50%-of-baseline STOPPED threshold could mean a stopped motor, a motor running at a lower (but still legitimate) speed, a poorly-attached or shifted sensor, or a sensor/data problem. There is no independent motor-run signal (no current sensor, no tachometer) to confirm against. Correct framing: "possible unexpected pump stoppage inferred from low vibration," not a confirmed stoppage. The logic should distinguish a *confirmed expected-off* state (correctly scheduled off) from an *unexpectedly low* reading occurring during a period the pump is actually supposed to be running, these are not the same situation and shouldn't be reported identically.

**Finding 16 — A correction to this chat's own prior reasoning.** This chat had claimed STOPPED and WARNING "can never clash" based solely on comparing the two conditions' instantaneous threshold *values* (0.3463 vs 0.8311, no numeric overlap). The reviewer correctly identified this as incomplete: WARNING depends on *historical* data (the 2-minute rolling fraction), not just the instantaneous RMS, so both conditions can genuinely be true underneath at the same moment, for example, a pump running hot, then suddenly stopping: the rolling WARNING fraction can still read true from recent history, even while current RMS has already crashed below the STOPPED threshold. The code's existing if/else ordering (STOPPED checked first) happens to resolve this correctly in terms of which status is actually *displayed*, but the stated reasoning for why no conflict could occur was wrong, and has been corrected here.

**Finding 17 — ISO 10816-7, independently re-verified.** Confirmed ISO 10816-7:2009 remains the current, published standard, with ISO 20816-7 as its in-development replacement. Scope: industrial rotodynamic pumps above 1kW, consistent with what this project had already established. New detail added: the standard specifies a measurement frequency range extending at least from 10 to 1000Hz for its vibration-velocity instrumentation, along with specific measurement-location requirements. The core distinction already documented stands, confirmed independently: the standard uses RMS *velocity* (mm/s); this project computes an *acceleration*-derived indicator (m/s²). Current CAUTION/WARNING thresholds must not be presented as ISO severity categories.

**Finding 18 — The Juhendra/Razali paper, internal inconsistency independently confirmed by this chat.** The reviewer flagged internal contradictions in the paper (ANOVA table showing Sig. = 0.096 while the discussion text claims significance below 0.05; Levene's test table showing Sig. < 0.001 while the discussion claims homogeneous variance was established). **This was independently re-checked directly against the actual uploaded paper text in this conversation and confirmed accurate**: Table 11 does read "Sig. ... 0,096", while the paper's own discussion text states "a significance value (Sig.) less than 0.05." Same contradiction pattern confirmed in the Levene's test table versus its surrounding discussion. The paper's reported figures (76.26% average accuracy, 0.3330 m/s² RMSE) are real, genuine numbers appearing in the document, but given these internal inconsistencies, the paper is a weak foundation for quantitative accuracy claims. Usable as an example of a low-cost MPU6050 implementation and the general need for calibration; not usable as a generalisable accuracy figure for MPU6050 sensors, and not predictive of this project's own hardware's accuracy.

**Finding 19 — Manufacturer datasheet figures are reference specs, not guarantees for this exact chip.** The confirmed WHO_AM_I = 0x72 clone-chip finding matters beyond the Adafruit library rejection: successful raw register communication does not prove this unidentified variant shares identical calibration characteristics (scale-factor precision, noise floor, etc.) with a genuine InvenSense part. Datasheet figures (8192 LSB/g, tolerance bands, etc.) should be understood as reference specifications for the general chip family, not confirmed, guaranteed specs for this specific physical unit.

**Finding 20 — "Time retention" is not the same claim as "correct time."** `rtc.lostPower()` only checks the oscillator-stop flag, whether the battery-backed clock's oscillator ever stopped running. It does not independently verify that whatever calendar date and time is currently stored is actually correct. This directly explains the wrong-date issue encountered during testing: the flag can correctly report "never lost power" while the underlying date value is still wrong, if it was set incorrectly at some point and never corrected afterward.

**Finding 21 — Vibration events are not yet RTC-timestamped.** The event log (`eventLog[]`) currently stores `millis()` values, elapsed time since boot, not real calendar timestamps. Simply having a DS3231 present on the shared bus does not automatically convert these records into absolute timestamps, that would require deliberately calling into the schedule module's time functions at the exact moment each event is logged, which the current code does not do.

**Finding 22 — History does not survive a reboot.** The 10-slot event log and the 1440-slot trend buffer are both held only in RAM. Both are lost on every ESP32 restart unless deliberately persisted elsewhere (flash, SD card), which is not currently implemented.

**Finding 23 — "Power resilience" needs precise language.** A battery-backed RTC retaining correct time through a power cycle is not the same claim as the whole monitoring system continuing to operate during an outage. True uninterrupted *monitoring* (not just clock-keeping) would require separate backup power for the ESP32, sensors, and dashboard electronics themselves, a substantially larger, separate scope item, not something implied by simply having an RTC installed.

**Finding 24 — The DS3231 breakout's coin-cell charging circuit should be physically checked, not assumed.** This was already flagged as a known open risk very early in this project's component-sourcing phase (some common DS3231 breakout designs include charging circuitry unsuitable for a non-rechargeable CR2032 cell if powered continuously for extended periods). The reviewer re-raised this independently. **Status: still not confirmed as checked or mitigated on the physical board currently in use.**

**Finding 25 — Reviewer's own overall assessment table**, reproduced in full:

| Capability | Assessment |
|---|---|
| Hardware communication and integration | Demonstrated |
| Basic vibration sensitivity | Demonstrated |
| Baseline-relative alert generation | Demonstrated, limited validation |
| Signal-processing suitability | Needs further investigation |
| Threshold selection | Provisional |
| Mechanical fault detection | Not validated |
| RTC power-cycle time retention | Demonstrated |
| Autonomous fault-tolerant operation | Not yet demonstrated |

---

# PART 4: PRIORITISED REMEDIATION CHECKLIST (47 items, 5 phases)

This checklist was produced directly from the review above plus this document's own outstanding items, deliberately sequenced so that no phase's work needs to be redone because an earlier phase's output changed underneath it (most importantly: Phase 1's sampling-rate finding will very likely invalidate the current calibration baseline, so calibration/thresholds are correctly sequenced *after* it, not before).

## Phase 1 — Verify the measurement system (do first, 0/7)
1. Measure the actual sampling rate (`micros()`-based interval logging, average/min/max/variation, with RTC and other modules active).
2. Check the sampling-rate vs. filter-bandwidth mismatch (measured rate vs. the ~260Hz DLPF setting).
3. Decide on an appropriate sampling strategy (target frequency range, filtering, a controlled sample interval rather than relying on `delay(20)` alone).
4. Review the gravity-removal method (Finding 2 above).
5. Compare an improved acceleration-processing method (per-axis gravity/DC removal, then combined dynamic RMS) against the current single-subtraction approach.
6. Verify the RMS measurement window's real duration in seconds, once the actual sample rate is known.
7. Document the physical sensor installation (mounting, cable strain relief, orientation, coupling consistency).

## Phase 2 — Correct the detection algorithm (high priority, 0/8)
8. Add a `SENSOR ERROR` status (Finding 13).
9. Replace infinite blocking error loops with non-blocking failure handling (Finding 13).
10. Make WARNING timing independent of loop execution speed (Finding 8b).
11. Validate CAUTION/WARNING multipliers against measured normal variability and controlled elevated conditions (Finding 6).
12. Confirm exactly what the 2-minute/50% duration rule represents; keep as a stated prototype parameter unless independently validated (Finding 8, Finding 9).
13. Review alert recovery and state priority (WARNING→CAUTION→OK, direct WARNING→OK, interaction with historical data) (Finding 8c, Finding 16).
14. Review STOPPED detection: test the proposed threshold/duration, distinguish possible from confirmed stoppage (Finding 15).
15. Evaluate whether a motor-startup settling period is needed, without suppressing genuine faults.

## Phase 3 — Fix calibration, RTC and scheduling (high priority, 0/8)
16. Recalibrate after any signal-processing change (Phase 1 changes invalidate the existing 0.6926 baseline directly).
17. Check calibration repeatability under controlled, logged fan conditions.
18. Implement operator-approved calibration as actual working firmware, not just the design (Finding 4).
19. Save approved calibration values to flash via `Preferences`; test survival across power cycles.
20. Separate pump operating schedule from pool occupancy/access schedule, as two distinct stored values (Finding 14).
21. Implement and test pump operating-state logic distinguishing expected-OFF, possible-unexpected-STOPPED, normal running, and abnormal vibration (Finding 15).
22. Verify and correct DS3231 date/time; check its accuracy after a real power cycle; inspect the coin-cell charging circuit (Finding 20, Finding 24).
23. Integrate real RTC timestamps into event records; verify which history survives a reboot and which doesn't (Finding 21, Finding 22).

## Phase 4 — Formal validation testing (required before final results, 0/10)
24. Test A — Motor-off baseline, repeated trials.
25. Test B — Healthy running baseline, repeated trials, checking for false CAUTION/WARNING.
26. Test C — CAUTION response under a controlled, moderate, repeatable condition.
27. Test D — Sustained WARNING response, threshold-crossing time, latency.
28. Test E — Short/intermittent disturbances, confirming no false sustained escalation.
29. Test F — Recovery behaviour, including lingering historical WARNING effects.
30. Test G — Sensor/communication failure, explicit error reporting and recovery.
31. Test H — STOPPED and schedule behaviour, including transitions.
32. Repeat and properly annotate tests (aiming for 3 controlled repeats per main condition, with actual recorded speed-change times and the specific firmware version used).
33. Save raw measurements, serial logs, RMS time histories, and threshold plots for the report.

## Phase 5 — Documentation and methodology (after technical validation, 0/9)
34. Remove the unused Adafruit MPU6050/Unified Sensor dependencies from `platformio.ini`. **(Already decided in this project, see Part 1 Section "platformio.ini, firm answer"; this checklist item is execution-only, not an open decision.)**
35. Check existing PlatformIO `log2file` logs. **(Already confirmed: the log files genuinely exist, and all pasted trial data in this document was sourced from them.)**
36. Update the vibration architecture/flowchart to show acquisition, filtering, RMS, commissioning, threshold comparison, time-gating, operating states, and dashboard output.
37. Explain the mathematical measurement method in full, including filtering, actual sample timing, and limitations.
38. Present calibration and threshold justification, stating plainly whether each was statistically or empirically validated.
39. Update the literature review: ISO 10816-7 as contextual guidance only; the Juhendra/Razali paper cited cautiously; Aldrini/Chihi cited only for the time-gating principle.
40. Document the full experimental procedure and results: rig photos, conditions, tables, graphs, latency measurements, repeats, and reliability issues encountered.
41. State the proof-of-concept limitations explicitly: a battery-powered fan is not an industrial pump; measured anomalies are not validated diagnoses of any specific fault type.
42. Separate completed, planned, and unverified features clearly in the final report, matching this document's own VERIFIED/BUILT/PLANNED/OPEN tagging convention throughout.

**Optional, not essential to the core system (0/5):** per-axis RMS trend plots; hourly alert counters/long-term summaries; validating the dashboard getter functions against a live dashboard; an independent speed/vibration reference (tachometer or second accelerometer); improved physical power resilience (backup power for the monitoring electronics itself).

**Recommended starting point, per both the review and this checklist:** Phase 1, Item 1, measuring the real sampling interval, since every later phase's validity depends on knowing this first.

---

# PART 5: CURRENT STATE AS OF THIS ENTRY

- A temporary diagnostic function, `measureLoopInterval()`, has been **written and given**, intended to be called as the first line of `vibrationUpdate()`, printing average/min/max loop interval and effective Hz every 5 seconds. **This has been given as code only; it has not yet been pasted into the project, built, uploaded, or run.** This directly answers Phase 1, Item 1 once executed.
- The rename of `scheduleShouldBeMonitoring()` to `pumpScheduleShouldBeRunning()` (Finding 14) has been **proposed and agreed in discussion, but not yet applied to the actual code**. `schedule.h`/`schedule.cpp` as documented in Part 2 of this file still use the original name.
- The schedule/STOPPED code given earlier in this document (Part 1, Section "The schedule and STOPPED logic, now actually built") has been **written and given, but its actual pasting, building, and testing on the real board has not been confirmed** in this conversation. Per Finding 14 and Finding 16 above, this code should not be treated as tested or final, it requires the pump/pool schedule separation fix and should be retested, not assumed correct as originally given.
- `BASELINE_RUNNING_RMS = 0.6926` is very likely to require remeasurement once Phase 1's sampling/filtering decisions are made (Checklist Item 16), since any change to the signal-processing pipeline invalidates a baseline measured under the old pipeline.

*End of Part 5 and this update. Next addition to this document should follow once the loop-timing measurement has actually been run and real numbers are available.*

---

# PART 6: CORRECTIONS FROM DIRECT REPOSITORY VERIFICATION

The entries below were found by directly inspecting the actual repository (not from pasted excerpts), and correct or extend several things stated earlier in this document. Kept as a separate, clearly dated addition rather than silently edited into the sections above, so the correction itself stays visible.

## 6.1 Real log files, resolving a previously OPEN item

The `logs/` folder (untracked in git at time of writing) contains six real serial logs from 12–13 October, confirming every trial pasted into this document came from genuine `log2file` output, not reconstructed from memory:

| File | Content |
|---|---|
| `device-monitor-261012-151453.log`, `…151940.log` | Water-quality output (temperature, pH), not vibration |
| `device-monitor-261012-232316.log` | Vibration, Status OK, RMS ≈ 0.56 |
| `device-monitor-261012-232802.log` | A calibration run (likely the 0.6926 result) |
| `device-monitor-261012-233647.log` | **Trial 1**, RTC time 15:26:55 to 15:31:36 |
| `device-monitor-261013-013030.log` | **Trial 2**, RTC time 17:20:38 to 17:27:12, including all six `Error -1` lines at 17:25:57 |

**Action:** these files should be committed to the repository (or at minimum backed up), as they are the primary citable evidence for both trials, not the pasted excerpts reproduced in Part 1.

## 6.2 The RTC offset, now precisely quantified

Not just "the date is wrong": the DS3231 was found to be running approximately **8 hours 10 minutes behind** the laptop's own clock, consistently across two independent logs (the calibration log: laptop time 23:28 vs. RTC time 15:20; Trial 2: laptop time 01:30:30 vs. RTC time 17:20:38). This is a real, measured, consistent offset, not random drift.

**What this changes:** the latency and duration figures already calculated in Part 1 (85s, 75s, 68s, 116s) remain valid, since they're derived from differences within the same continuously-running RTC. But the RTC's absolute timestamps **cannot** be directly compared against anything recorded on the laptop or a phone, for example, your own noted dial-change times for Trial 2's speed stages, without first applying this +8h10m correction. This directly affects how Trial 2's four-stage boundaries (still an OPEN item) should eventually be resolved.

## 6.3 Correction to the `platformio.ini` cleanup recommendation

The earlier recommendation to "remove the unused Adafruit lines" was based on an incomplete view of the file. The actual current `platformio.ini` also lists `RTClib`, `OneWire`, and `DallasTemperature`, none of which are dead code:
- **RTClib** is genuinely used, by `schedule.cpp`, confirmed throughout Part 2 of this document.
- **OneWire** and **DallasTemperature** are almost certainly used by the water-quality module's DS18B20 temperature sensor (a 1-Wire digital sensor, these are its standard libraries), developed in a separate session and outside this document's own direct visibility, but a reasonable, near-certain inference given the sensor involved.

**Corrected, narrower action:** only `adafruit/Adafruit MPU6050` and `adafruit/Adafruit Unified Sensor` remain confirmed, genuinely unused, and safe to remove. The other three stay.

## 6.4 Correction to Section 7.4 (`main.cpp`)

The version of `main.cpp` actually used for the formal Trial 1 and Trial 2 testing has `waterQualitySetup()`/`waterQualityUpdate()` and `dashboardSetup()`/`dashboardUpdate()` **commented out**, not called unconditionally behind the connected-flags as Part 1, Section 7.4 describes. This matches the test-isolation guidance given earlier in this project specifically for the formal test session (isolate vibration/RTC from unverified modules), and should be treated as the accurate, current state. Section 7.4's listing reflects an earlier or different version of the file, not what was actually running during the trials documented in Part 1, Section 10.

## 6.5 Confirmed matching, no correction needed

`MPU_ADDR = 0x69`, `BASELINE_RUNNING_RMS = 0.6926`, `CALIBRATION_MODE = false`, and the DLPF register value of `0x00` were all independently checked directly against the repository and match what's recorded in Part 1 exactly.

*End of Part 6.*

---

# PART 7: LOOP TIMING MEASUREMENT RESULTS (Phase 1, Items 1-3)

**Measured** (150-second run, board untouched, no I2C errors, 29 reports):
- Average loop interval: 22.09ms, effective rate 45.27Hz (not the assumed 20ms/50Hz)
- Range: 21.992ms to 34.000ms
- A ~34ms spike recurs reliably once every 60 seconds, consistent with the once-per-minute trend-buffer write, confirmed as expected overhead, not a fault

**Consequences for existing constants:**
- Actual WARNING rolling window: 6000 × 22.09ms ≈ 132.5s, not the intended 120s
- Actual short-term RMS window: 50 × 22.09ms ≈ 1.10s, not 1.0s
- Both Trial 1 (85s) and Trial 2 (68s, provisional) latency figures were measured against this ~132.5s window, not a true 120s one

**Aliasing finding:** Nyquist limit at 45.27Hz ≈ 22.6Hz. The DLPF setting at the time of this measurement (0x00, ~260Hz bandwidth) does not anti-alias at this rate, any real vibration above ~22.6Hz would be aliased into a false, lower-frequency reading. A typical 1500-3000 RPM motor vibrates at 25-50Hz, already above this limit.

**Decision (answering Phase 1, Item 3):** rather than restructuring sampling around a dedicated timer or interrupt, a larger change this project's scope doesn't justify, the DLPF is narrowed to match the honestly achievable sample rate. DLPF_CFG=4 (register value 0x04, ~21Hz) is the largest available filter option still below the 22.6Hz Nyquist limit. This is the same register value used before it was changed to 0x00 earlier in this project (based on a hobbyist forum claim about drone use), now arrived at for a properly justified reason: matching this board's own measured sampling capability, not a borrowed general claim.

**Status: APPLIED.** DLPF changed from 0x00 to 0x04. The temporary `measureLoopInterval()` diagnostic has been removed, its purpose fulfilled.

**Consequence, per Checklist Item 16:** `BASELINE_RUNNING_RMS = 0.6926`, calibrated under the old filter setting, is now invalid. A fresh calibration run under the new 0x04 setting is required before any further formal testing. **This is the next task, not yet done.**

---

# PART 8: RECALIBRATION, RTC CORRECTION AND BACKUP BATTERY TEST (13 October 2026)

**New active baseline: `BASELINE_RUNNING_RMS = 0.3476`**, replacing 0.6926. Measured under the corrected DLPF setting of 0x04 (~21Hz). Derived thresholds: CAUTION = 0.3476 × 1.2 = 0.4171, WARNING = 0.3476 × 1.5 = 0.5214. `CALIBRATION_MODE` set back to `false`; boot confirmed clean ("Vibration module ready.", real `Status: OK` lines, no calibration text).

**RTC absolute time force-corrected once, on 13 October 2026.** Before correction the DS3231 read 06:22 against laptop time 14:32, the same ~8h10m offset recorded in Part 6.2. A temporary `rtc.adjust(DateTime(F(__DATE__), F(__TIME__)))` line was added after the `lostPower()` block, uploaded once, then removed and re-uploaded so it does not run on future flashes. The final boot printed the "RTC already keeping time correctly, leaving it alone." branch, as intended.

Residual offset after correction: the RTC runs a steady **~39s behind** the laptop clock (measured line-by-line against laptop time: −39.0 to −39.1s). This is the gap between compile time (when `__TIME__` is fixed) and the moment the board actually started running, not drift. Date and hour are now correct; the clock is not second-accurate. **OPEN (minor):** a serial-based or offset-compensated set would remove this if second-level accuracy is ever needed.

**Backup battery now in use: Energizer CR2016, 3V**, physically thinner (1.6mm) than the CR2032 (3.2mm) these DS3231 module holders are typically designed for, so contact pressure in the holder was a genuine concern.

**Power-cycle test result: VERIFIED.** USB was fully unplugged and reconnected (unplugged for well over 30s; the port was confirmed absent from the system during the gap). On reboot:
- The firmware printed "RTC already keeping time correctly, leaving it alone." (not "lost power"), meaning the DS3231's oscillator-stop flag was never set: the CR2016 kept the oscillator running throughout.
- RTC-to-laptop offset after the power cycle: −38.5s, against −39.1s before it, so the time carried on through the unplugged gap rather than resetting to an earlier point. The 0.5s difference is within the 1-second resolution of the comparison.

One successful power cycle is not proof of long-term contact reliability in an oversized holder; a repeat after the board has been physically handled or moved would strengthen this result.

**Note on current firmware state:** immediately after this entry, the DLPF was temporarily reverted to 0x00 as a diagnostic, to test whether the 0x04 filter removed real vibration signal as well as noise. The 0.3476 baseline above belongs to the 0x04 setting and stays valid only if 0x04 is kept.
