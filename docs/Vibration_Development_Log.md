# Vibration Module — Complete Development Log

Project: Pool Plant Monitor and Preventative Maintenance Advisor (EEE4022S, SW-3)
Supervisor: Dr Simon Winberg | Co-supervisor: Kai Goodall
Board: ESP32 DOIT DEVKIT V1 (COM5, 115200 baud)

This document records everything that was discussed, built, tested, broken, fixed, and decided for the vibration and RTC modules across the full development conversation. It is written as a raw process record, not a polished report.

---

## 1. Schedule and STOPPED Logic — Design Discussion

### 1.1 Why the RTC was added in the first place

The whole reason for adding a DS3231 RTC to the system was to support schedule-gated monitoring. The idea: a pool operator enters the pump's expected run times via the dashboard. The system only evaluates vibration during that window. Outside the window, it reports "OFF SCHEDULE" and does not flag anything.

This is important because a pump sitting idle overnight correctly reads near-zero RMS. Without a schedule, the system would either need to treat that as "OK" (masking a genuine unexpected shutdown during daytime) or flag it (generating false alerts every night). Neither is acceptable. The schedule resolves this by giving the system context about when the pump is supposed to be running.

### 1.2 How schedule data flows from the dashboard to the device

This was a point of confusion that needed careful clarification. The operator does not edit source code. The data flow is:

1. The operator opens a web page hosted directly by the ESP32 itself (no cloud, no internet).
2. They enter start and end times into a form and submit it.
3. The ESP32's own web server receives the submission.
4. The code calls `scheduleSetRunWindow(startHour, startMinute, endHour, endMinute)`.
5. That function writes the values into flash storage via the `Preferences` library.
6. Every loop iteration, `scheduleShouldBeMonitoring()` reads the stored values, compares them against the RTC's current time, and returns true or false.

The source code files never change. The data lives in persistent storage that the running program reads from.

### 1.3 The STOPPED detection question

The user raised a genuine, correctly-spotted gap: the vibration code only ever looked upward (is RMS above CAUTION? above WARNING?). There was no check for RMS dropping unexpectedly low, which would indicate a pump that stopped running when it shouldn't have.

Two distinct cases were identified:

**Case 1: Pump stops entirely during scheduled run hours.** RMS crashes toward zero while the schedule says it should be running. This should absolutely be flagged — it's more urgent than high vibration, since it means zero filtration. This case genuinely requires the schedule to work, because near-zero RMS is expected outside run hours and a fault during them. You cannot tell these apart without knowing whether the pump is supposed to be on.

**Case 2: Pump runs slower than baseline but doesn't stop.** After discussion, this was determined NOT to be something the system should flag. A quieter-than-usual pump could mean intentional throttling, a variable-speed drive, or a developing fault — and the vibration sensor alone cannot distinguish between these. You'd need motor current or RPM data for that, which this system doesn't have.

**Decision:** Build Case 1 only, as its own distinct `STOPPED` status. Use a threshold at 50% of baseline, sustained for 1 minute, active only during scheduled run hours. Don't build Case 2 at all.

### 1.4 The STOPPED threshold value

The 50% figure was a reasoned starting point based on existing data: resting RMS across all test logs consistently sat at 0.2–0.3, while the running baseline was 0.6926. Half of baseline (0.3463) sits between the two. However, this was explicitly flagged as a value that would need real measurement against the actual deployed pump, same as `BASELINE_RUNNING_RMS` itself.

### 1.5 Schedule and STOPPED code

Full code was written for both `schedule.h`/`schedule.cpp` (with Preferences persistence, midnight-crossing support, and fail-open default behaviour) and the STOPPED detection additions to `vibration.cpp`. The code was given to the user but its pasting, building, and testing on the real board was never confirmed in this conversation. It should be treated as written but unverified.

### 1.6 Fail-open design decision

An important design choice: if the RTC isn't working, or no schedule has ever been set, `scheduleShouldBeMonitoring()` returns true — keep monitoring. The reasoning: a missing schedule should never silently disable fault detection. It's safer to monitor unnecessarily than to miss a real fault because someone forgot to enter a schedule.

---

## 2. Requirements and Traceability

### 2.1 User Requirements

| ID | Requirement | Source |
|---|---|---|
| UR1 | Early alert when pump shows signs of developing mechanical failure | Brief: "early warning indicators of impending mechanical failure" |
| UR2 | Operator never needs to interpret raw sensor numbers, only plain status | Design principle across all modules |
| UR3 | System must be commissionable against the specific pump it's installed on | User's own question about universal thresholds |
| UR4 | Alert if pump appears to have stopped unexpectedly during scheduled hours | Loadshedding/"unexpected OFF" reasoning behind adding the RTC |
| UR5 | No false alerts from brief, non-representative fluctuations | CAUTION-vs-WARNING duration-based design discussion |

### 2.2 Functional Requirements

| ID | Requirement | Derives from | Status |
|---|---|---|---|
| FR1 | Sample 3-axis acceleration, compute orientation-independent magnitude | UR1, UR3 | VERIFIED |
| FR2 | Compute RMS over rolling short-term window | UR1 | VERIFIED |
| FR3 | Output only plain status words (OK/CAUTION/WARNING/STOPPED/OFF SCHEDULE) | UR2 | VERIFIED for OK/CAUTION/WARNING; BUILT NOT TESTED for STOPPED/OFF SCHEDULE |
| FR4 | Flag CAUTION immediately on crossing threshold | UR1, UR5 | VERIFIED |
| FR5 | Escalate to WARNING only after sustained elevation (50% of trailing ~132.5s window) | UR1, UR5 | VERIFIED |
| FR6 | Operator-triggered calibration measuring real baseline | UR3 | VERIFIED (procedure works); dashboard-triggered version PLANNED |
| FR7 | Detect sustained low RMS during scheduled hours, report STOPPED | UR4 | BUILT, NOT TESTED |
| FR8 | Suppress fault evaluation outside scheduled window, report OFF SCHEDULE | UR2, UR4 | BUILT, NOT TESTED |
| FR9 | Rolling log of recent CAUTION/WARNING events | UR1 | BUILT, NOT INDEPENDENTLY VERIFIED |
| FR10 | 24-hour rolling trend of average RMS | UR1 | BUILT, NOT VERIFIED |
| FR11 | Expose values via accessor functions for dashboard | UR2 | BUILT, NOT TESTED |

---

## 3. External Engineering Review — What It Found

A comprehensive external review was obtained covering the entire vibration subsystem. This section records the findings that changed how we proceeded, not just repeated what the reviewer said.

### 3.1 Honest claim level

The reviewer distinguished three achievement levels: (1) hardware measurement — verified; (2) anomaly detection under limited fan conditions — demonstrated; (3) reliable prediction of actual pump mechanical failure — not demonstrated. The correct claim is "baseline-relative vibration anomaly detection," not a validated fault-prediction system. Turning a fan from speed 1 to speed 3 is not equivalent to introducing a mechanical defect.

### 3.2 Gravity subtraction is not fully orientation-neutral

`sqrt(ax²+ay²+az²) − 9.8` is mathematically independent of axis labelling but NOT equally sensitive to all physical vibration directions relative to gravity. For vibration perpendicular to gravity, the response is approximately second-order: `v(t) ≈ ax(t)²/(2g)`. Small horizontal vibration is mathematically attenuated far more than small vertical vibration. The current signal should be described as a "gravity-referenced acceleration-magnitude residual," not a fully gravity-compensated vibration measurement.

### 3.3 Sampling rate and aliasing — the most consequential finding

`delay(20)` does not guarantee 50Hz. The actual loop rate was unknown. Even an ideal 50Hz gives a Nyquist limit of only 25Hz. The DLPF at 0x00 passes content up to ~260Hz, creating a real mismatch. This was flagged as the highest-priority technical issue and became the first thing we measured.

### 3.4 Calibration discrepancy cause was never isolated

The two baseline runs (1.0747 vs 0.6926, a 35.6% difference) were explained by battery charge variation, but neither battery voltage nor fan RPM was independently measured. The correct framing: plausible contributing factors, but the cause has not been experimentally isolated.

### 3.5 Threshold multipliers are provisional

1.2× for CAUTION and 1.5× for WARNING are prototype design parameters, not experimentally validated mechanical-fault thresholds. Nothing in this project establishes that these percentages correspond to any known fault severity or statistical significance level.

### 3.6 WARNING timing misconception

The system is not a simple two-minute timer. It's a rolling 6000-sample history where WARNING triggers when ≥50% of that history is above the elevated threshold. Three consequences: (a) WARNING doesn't require two continuous minutes, roughly one minute of intermittent elevation can suffice; (b) the history duration silently changes if loop speed changes; (c) WARNING can remain active after instantaneous vibration has returned to normal.

### 3.7 Literature verification

- Aldrini and Chihi paper: confirmed real, supports the principle of time-gated evaluation, but does NOT justify the specific 2-minute constant.
- Juhendra/Razali paper: internal statistical inconsistencies independently confirmed (ANOVA Sig. 0.096 claimed as below 0.05; Levene's test contradiction). Weak foundation for quantitative accuracy claims.
- ISO 10816-7: confirmed current standard, but this system does not implement its measurements (it uses acceleration-derived RMS, not velocity RMS in mm/s).

### 3.8 Dead sensor looks healthy

`mpuReadAccel()` failing just does `return;`, leaving `currentStatus` at its last value. A disconnected sensor would keep reporting its last status indefinitely. Needs an explicit `SENSOR ERROR` state with a staleness timeout.

### 3.9 Pool schedule vs pump schedule conflation

Pool opening hours and pump operating hours are not the same thing. A filtration pump may legitimately run outside public hours. The current single stored run window is only valid if it represents the pump's own operating schedule. These must be two independently settable values. Rename proposed: `scheduleShouldBeMonitoring()` → `pumpScheduleShouldBeRunning()`.

### 3.10 The STOPPED/WARNING conflict correction

This chat had claimed STOPPED and WARNING "can never clash" based on non-overlapping instantaneous thresholds. The reviewer correctly identified this as incomplete: WARNING depends on historical data, so both conditions can genuinely be true at the same moment. The code's if/else ordering (STOPPED checked first) happens to resolve the displayed status correctly, but the reasoning was wrong.

---

## 4. The 47-Item Remediation Checklist

A five-phase checklist was produced from the review findings. The key insight in its sequencing: Phase 1 (measurement verification) must come before Phase 3 (calibration), because any change to the signal-processing pipeline invalidates the existing baseline. Items completed during this session are marked.

**Phase 1 — Verify the measurement system (7 items)**
- Item 1: Measure actual sampling rate — COMPLETED (see Section 5)
- Item 2: Check sampling-rate vs filter-bandwidth mismatch — COMPLETED
- Item 3: Decide on sampling strategy — COMPLETED (see Section 6)
- Items 4-7: Gravity review, improved processing comparison, RMS window verification, sensor installation documentation — NOT STARTED

**Phase 2 — Correct the detection algorithm (8 items)** — NOT STARTED
**Phase 3 — Fix calibration, RTC and scheduling (8 items)** — PARTIALLY STARTED (calibration run done, RTC corrected)
**Phase 4 — Formal validation tests (10 items)** — NOT STARTED
**Phase 5 — Documentation and methodology (9 items)** — PARTIALLY STARTED

---

## 5. Loop Timing Measurement

### 5.1 What was measured

A temporary diagnostic function (`measureLoopInterval()`) was added as the first line of `vibrationUpdate()`, recording `micros()` between consecutive calls. It ran for 150 seconds with the board untouched, producing 29 reports.

### 5.2 Results

- Average loop interval: 22.09ms (effective rate: 45.27Hz)
- Range: 21.992ms to 34.000ms
- A ~34ms spike recurred reliably once every 60 seconds, most likely the once-per-minute trend-buffer write (not directly confirmed)
- The assumed 20ms/50Hz was wrong

### 5.3 Consequences for existing constants

- The WARNING rolling window is actually 6000 × 22.09ms ≈ 132.5 seconds, not the intended 120 seconds
- The short-term RMS window is actually 50 × 22.09ms ≈ 1.10 seconds, not 1.0 seconds
- Both Trial 1 (85s) and Trial 2 (68s) latency figures were measured against this ~132.5s window

### 5.4 Aliasing finding

Nyquist limit at 45.27Hz ≈ 22.6Hz. The DLPF at 0x00 (~260Hz bandwidth) does not anti-alias at this rate. A typical 1500-3000 RPM motor vibrates at 25-50Hz, already above the Nyquist limit. This confirmed the aliasing concern was real, not theoretical.

The diagnostic code was removed after the measurement was complete, its purpose fulfilled. The log was saved as `logs/loop-timing-261013.log`.

---

## 6. The DLPF Filter Decision — A Genuine, Difficult Tradeoff

This was the most technically interesting decision in the vibration module's development, and it went through multiple reversals before landing on a final, justified position.

### 6.1 Chronology

1. **Initial setting: 0x00 (~260Hz bandwidth).** Chosen early on based on a hobbyist forum post about drone vibration filtering. No technical justification for this specific project.

2. **Changed to 0x04 (~21Hz) after the loop timing measurement.** 21Hz is the largest MPU6050 filter option still below the measured 22.6Hz Nyquist limit. This was the mathematically "correct" anti-aliasing choice.

3. **Calibration under 0x04 produced 0.3476**, roughly half the previous 0.6926 under 0x00. This confirmed the filter change had a real, substantial effect on the signal.

4. **Testing under 0x04 revealed the system could barely distinguish fan speeds.** The user observed that increasing fan speed produced almost no change in RMS, with values staying flat around 0.3 regardless. CAUTION and WARNING almost never triggered from actual fan operation, only from physically bumping the sensor.

5. **Reverted to 0x00 to test whether the filter was the cause.** Even under 0x00, the system initially showed flat RMS. This temporarily suggested the filter wasn't the problem.

6. **The flat-RMS mystery was eventually traced to the code still having 0x04 on the board**, not 0x00 as assumed. The revert hadn't actually been applied. Once properly running under 0x00, the system showed clear, responsive behaviour with RMS climbing well above 1.0 during fan operation and CAUTION triggering correctly.

7. **Fan battery depletion was ruled out** as the cause: the fan was fully charged and the behaviour persisted under 0x04.

8. **Tape mount was also ruled out** as a sole explanation: the same physical setup worked under 0x00.

### 6.2 The core tradeoff

- **0x04 (21Hz, "correct"):** Respects the Nyquist limit. But empirically suppresses the amplitude differences between fan speeds so severely that the system can barely distinguish them. A system that can't tell operating conditions apart defeats its own purpose.

- **0x00 (260Hz, "working"):** Produces a system that clearly responds to different conditions, with well-separated RMS values for different speeds. But carries a real aliasing risk — high-frequency vibration content can fold into lower-frequency readings, misrepresenting what's actually happening.

### 6.3 Final decision and justification

**Use 0x00.** The justification is not "it looks better" but a specific technical argument: the system's detection method uses only RMS amplitude, not frequency content. Aliasing corrupts which frequency a vibration is attributed to, but does not necessarily eliminate the energy from the RMS calculation. A system that correctly respects theory but can't distinguish operating conditions is worse than one with a known, documented frequency-domain risk.

### 6.4 How to honestly describe this in a report

"The wider filter bandwidth was retained because the Nyquist-matched 21Hz filter, while theoretically correct for the measured sample rate, empirically suppressed the amplitude differences between operating conditions that the severity classification depends on. This is a deliberate tradeoff: amplitude sensitivity was prioritised over frequency-domain correctness, since the system's detection method uses only RMS amplitude, not frequency content. This introduces a known, unquantified aliasing risk that was not independently verified through spectral analysis, and is noted as a limitation."

### 6.5 What this means for the calibration

The baseline 0.3476, measured under 0x04, is invalid under 0x00. A fresh calibration under 0x00 is required before any further formal testing. This calibration has NOT been done yet — the user ran out of tape to mount the sensor.

---

## 7. Calibration History

| Run | Filter | Baseline | Notes |
|---|---|---|---|
| 1 | 0x00 | 1.0747 | Earlier session, different fan charge state |
| 2 | 0x00 | 0.6926 | Different session, battery charge likely different |
| 3 | 0x04 | 0.3476 | Under the narrower filter, confirmed the filter dramatically reduces measured amplitude |

Run 3 is now invalid because the decision was made to revert to 0x00. A new Run 4 under 0x00 is needed and pending (blocked by lack of tape for sensor mounting).

---

## 8. RTC Module

### 8.1 Hardware

- DS3231 on shared I2C bus at address 0x68
- Battery: Energizer CR2016 3V lithium (physically thinner than the CR2032 the holder was designed for)
- The DS3231 breakout's charging circuit is a known concern: it can damage non-rechargeable cells if powered continuously. An LIR2032 rechargeable cell was recommended but not purchased.

### 8.2 Time correction

The RTC was found to be running approximately 8 hours 10 minutes behind the laptop clock, consistently across multiple logs. This was corrected with a one-time force `rtc.adjust(DateTime(F(__DATE__), F(__TIME__)))` line, which was added temporarily and then removed after one upload cycle.

After correction, the RTC was 39 seconds behind the laptop clock. This is the normal compile-to-boot delay and is acceptable for the system's hour/minute schedule resolution. No offset correction feature was built — the system doesn't need second-level precision.

### 8.3 Battery backup verification

The CR2016 was tested by unplugging USB entirely for over 30 seconds, then reconnecting. The boot printed "RTC already keeping time correctly, leaving it alone" (not "lost power"), and the time had correctly continued advancing through the gap. VERIFIED for one pass, but long-term reliability of the thinner cell in a CR2032 holder is unconfirmed.

### 8.4 Important distinctions documented

- Time retention ≠ correct time (`lostPower()` only checks the oscillator-stop flag)
- Vibration events currently store `millis()`, not RTC timestamps
- History (event log, trend buffer) lives in RAM, lost on reboot
- Power resilience of the RTC ≠ power resilience of the whole monitoring system

---

## 9. Fan Speed Testing Issue

### 9.1 The problem

During testing, increasing the fan speed produced almost no change in RMS. The system reported OK regardless of speed, with RMS sitting flat around 0.3–0.37. Only physical contact (pressing the speed dial button, touching the fan) produced CAUTION or WARNING.

### 9.2 Investigation and resolution

Multiple possible causes were investigated:

1. **Fan battery depletion:** Ruled out — fan was fully charged and the problem persisted.
2. **Tape mount loosening:** Could not be tested — user ran out of tape.
3. **DLPF filter suppressing real signal:** Confirmed as the primary cause (see Section 6).
4. **Code had wrong filter setting:** Discovered that the code still had 0x04 even when we thought it had been changed to 0x00. Once properly running under 0x00, the system responded correctly.

### 9.3 The deeper question: is fan speed even a valid test?

This was raised honestly during the investigation. Fan speed changes are NOT equivalent to a developing mechanical fault. A healthy motor running faster is expected to produce more vibration — that's normal, not a defect. The fan test validates that the detection pipeline's logical behaviour works correctly when the input signal changes, not that the system can diagnose specific mechanical faults. This distinction must be stated explicitly in the report.

---

## 10. platformio.ini Cleanup

The `platformio.ini` still listed `adafruit/Adafruit MPU6050` and `adafruit/Adafruit Unified Sensor`, neither of which is used by the current code (the Adafruit library was abandoned due to the WHO_AM_I=0x72 clone rejection). `RTClib`, `OneWire`, and `DallasTemperature` are genuinely in use and must stay. Only the two Adafruit lines should be removed. This cleanup was decided but the execution was confirmed done by the Claude Code session that verified the repo.

---

## 11. Wokwi Simulation Status

The original Wokwi simulation was used early in development to prove detection logic before real hardware existed. It no longer works with the current code for several reasons:

1. The current `main.cpp` pulls in modules Wokwi can't simulate (water quality sensors).
2. The vibration code now uses raw register access, not the Adafruit library the simulation was built against.
3. The code expects the MPU6050 at address 0x69, not the default 0x68.
4. The DS3231 was added entirely after Wokwi testing stopped.

This is documented as a natural consequence of the project's trajectory, not a regression. Wokwi served its purpose during initial logic verification and was superseded by real hardware testing.

---

## 12. Repo Verification Corrections

A Claude Code session directly inspected the actual repository and corrected several items:

1. **Log files confirmed to exist** in `logs/` folder — six real serial logs from 12–13 October, confirming all pasted trial data came from genuine `log2file` output.
2. **The RTC offset was precisely quantified** at 8h10m, consistent across two independent logs.
3. **`main.cpp` had water quality and dashboard calls commented out**, not called unconditionally as previously documented. This matched the test-isolation approach used for formal trials.
4. **All key constants confirmed matching:** MPU_ADDR=0x69, BASELINE_RUNNING_RMS=0.6926, CALIBRATION_MODE=false, DLPF=0x00.

---

## 13. Current State — What's Done, What's Pending

### Confirmed done and verified
- Hardware communication (MPU6050 at 0x69, DS3231 at 0x68, shared I2C bus)
- Basic vibration sensitivity (RMS responds to real disturbance)
- Baseline-relative alert generation (CAUTION and WARNING trigger correctly)
- Loop timing measurement (45.27Hz, all consequences documented)
- DLPF tradeoff investigated, decided, and justified
- RTC power-cycle time retention
- CR2016 battery backup (one-pass verification)
- Comprehensive external review received and fully documented

### Built but not tested on hardware
- Schedule/STOPPED logic code
- Dashboard getter functions
- Schedule rename (pumpScheduleShouldBeRunning)
- SENSOR ERROR status

### Blocked
- Fresh calibration under 0x00 — needs tape to remount sensor
- Formal retesting (Phase 4 of checklist) — needs calibration first
- Vibration formal trials with proper protocol — needs everything above

### Outstanding decisions
- Whether to implement per-axis gravity/DC removal (Phase 1, Items 4-5) — significant additional work, may be out of scope
- Whether to make WARNING timing truly time-based rather than sample-count-based (Phase 2, Item 10)
- Whether to implement runtime calibration triggerable from dashboard (Phase 3, Item 18)
