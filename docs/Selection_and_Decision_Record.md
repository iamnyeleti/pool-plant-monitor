# Selection and Decision Record

Project: Pool Plant Monitor and Preventative Maintenance Advisor (EEE4022S, SW-3)
Built from: this conversation only
Compiled: 16 October 2026

---

## Recollection check (against the record in this conversation)

| Recollection | Supported? | Detail |
|---|---|---|
| Considered a Raspberry Pi and an STM32, chose ESP32 for Wi-Fi/BT and capability | **NOT IN THIS CONVERSATION.** Both ESP32 boards were already in use when this chat started. No discussion of RPi or STM32 alternatives occurred here. The recollection may be from an earlier session but cannot be verified from this record. | Tag: UNKNOWN |
| For turbidity, first considered a tube-and-camera method before a dedicated sensor | **PARTIALLY SUPPORTED.** The pasted water quality document (from a separate chat session) records decision D-3: "Dedicated turbidity sensor (camera-based turbidity is future work) [LIT]", indicating the camera approach was mentioned in the literature review. No direct discussion of the tube-and-camera method as a considered alternative occurred in this conversation itself. | Tag: from pasted document, not this chat's own discussion |
| Chose the ESP32-S3 camera board because it has a camera and talks over Wi-Fi | **PARTIALLY SUPPORTED.** The camera board was already chosen and working before this conversation began. The WiFi capability was used and confirmed here (connecting to PoolPlantRadar network). The "because" reasoning was not stated or discussed in this chat. | Tag: STUDENT (recollection), board selection predates this conversation |
| Originally planned to use a cable that was not available locally | **SUPPORTED.** The JST-type UART connector on the camera board was identified, confirmed unusable, and the workaround (WiFi bridge) was designed in this conversation. See decision A-5. | Tag: RECORD |

---

## Category 1: Boards, Sensors and Components

### S-1: MPU6050 accelerometer (vibration sensing)

- **Decision:** which sensor to use for pump vibration monitoring
- **Chosen:** MPU6050 clone (WHO_AM_I = 0x72), wired at I2C address 0x69 [RECORD]
- **Alternatives considered:** none discussed in this conversation; the sensor was already in use when this chat began [RECORD]
- **Comparison:** n/a
- **Reason:** predates this conversation [UNKNOWN from this chat's record]
- **Constraint:** the clone chip returns WHO_AM_I = 0x72 instead of the genuine 0x68, which caused the Adafruit library to reject it [RECORD, from transcript summary]. The I2C address was shifted to 0x69 (AD0 tied to 3.3V) to avoid collision with the DS3231 RTC at 0x68 [RECORD]
- **What it costs/limits:** datasheet specifications are reference values for the general chip family, not guaranteed specs for this exact unit [RECORD, from external review Finding 19]. The Adafruit library was abandoned and replaced with direct register access [RECORD]
- **Evidence:** WHO_AM_I register read returning 0x72 [RECORD, transcript summary]. Flip test confirming sign reversal on Z axis [RECORD, transcript summary]
- **Status:** FINAL

### S-2: DS3231 RTC module

- **Decision:** how to give the system a real-time clock for schedule-gating
- **Chosen:** DS3231 breakout module, I2C address 0x68 [RECORD]
- **Alternatives considered:** none discussed in this conversation [RECORD]
- **Comparison:** n/a
- **Reason:** the system needs to know what time it is to decide whether the pump should be running, without depending on internet access [RECORD, discussed when explaining the schedule data flow]
- **Constraint:** shares the I2C bus with the MPU6050, hence the MPU6050's address was shifted to 0x69 [RECORD]
- **What it costs/limits:** the breakout board includes a charging circuit that can damage non-rechargeable cells (CR2032 or CR2016) if powered continuously via USB [RECORD, flagged multiple times]. An LIR2032 rechargeable cell was recommended but not purchased [RECORD]
- **Evidence:** power-cycle test verified the clock continued running through a USB disconnect (printed "RTC already keeping time correctly, leaving it alone" on reconnection) [RECORD]. Time was approximately 39 seconds behind the laptop clock after correction, acceptable for hour/minute schedule resolution [RECORD]
- **Status:** FINAL. Battery is an Energizer CR2016 (physically thinner than the CR2032 the holder was designed for) [RECORD, STUDENT stated]. Long-term contact reliability unconfirmed [RECORD]

### S-3: CR2016 battery for the DS3231

- **Decision:** which battery to install in the RTC backup holder
- **Chosen:** Energizer CR2016, 3V lithium [STUDENT]
- **Alternatives considered:** LIR2032 rechargeable lithium coin cell [AI-SUGGESTED, recommended from the start of the project]
- **Comparison:** CR2016 is what the student had available. LIR2032 is designed to be safely charged by the DS3231 breakout's trickle-charging circuit. CR2016 is non-rechargeable and physically thinner (1.6mm vs 3.2mm) than the CR2032 holder [RECORD]
- **Reason:** "I put in a battery" — the student used what was available [STUDENT]
- **Constraint:** local availability; the student did not have an LIR2032 [RECORD]
- **What it costs/limits:** risk of damage from the charging circuit over extended powered-on periods; uncertain long-term contact in a holder designed for a thicker cell [RECORD]
- **Evidence:** one-pass power-cycle test showed the clock survived a 30+ second USB disconnect [RECORD]
- **Status:** FINAL for now. LIR2032 remains the recommended replacement [RECORD]

### S-4: HLK-LD2410C radar sensor

- **Decision:** which sensor to use for presence detection in the occupancy module
- **Chosen:** HLK-LD2410C, 24GHz FMCW human presence radar [RECORD]
- **Alternatives considered:** PIR sensor (HC-SR501) was identified as the fallback if the radar didn't arrive in time [RECORD, transcript summary]. The radar arrived, so PIR was not purchased [RECORD]
- **Comparison:** the LD2410C detects stationary humans from breathing alone, independent of lighting. A PIR only detects movement. For after-hours monitoring where someone might stand still, the radar is the stronger choice [RECORD, discussed in the architecture section]
- **Reason:** can detect moving and stationary targets, works in darkness, configurable range [RECORD]
- **Constraint:** none stated beyond delivery timing [RECORD]
- **What it costs/limits:** the manufacturer explicitly warns about false triggers from fans, curtains, animals, and reflective surfaces [RECORD, from pasted external review]. It is tuned for human-like signatures but cannot definitively classify a detected target as human [RECORD]. Primarily specified for indoor use [RECORD, from pasted review]. Runs warm during operation (normal for an active radar, confirmed not a fault) [RECORD]
- **Evidence:** Bluetooth connection via HLKRadarTool confirmed moving and stationary target readings [RECORD, from pasted radar summary]. UART integration confirmed real distance and energy data [RECORD, serial log pasted]. OUT pin HIGH/LOW transitions confirmed after range reduction [RECORD]
- **Status:** FINAL

### S-5: Waveshare ESP32-S3-CAM with GC0308 sensor

- **Decision:** which board/camera for visual scene-change detection
- **Chosen:** Waveshare ESP32-S3-CAM with GC0308 grayscale sensor [RECORD]
- **Alternatives considered:** none discussed in this conversation; already in use [RECORD]
- **Comparison:** n/a in this conversation
- **Reason:** not stated in this conversation [UNKNOWN from this chat]. Student's recollection: "it has a camera and talks to the other ESP over Wi-Fi" [STUDENT, unchecked]
- **Constraint:** GC0308 is grayscale only, no native JPEG support (confirmed by hardware debugging in earlier sessions) [RECORD, transcript summary]. JPEG conversion done in software, limiting frame rate to approximately 1fps [RECORD]
- **What it costs/limits:** genuinely needs light to produce useful frames (confirmed by the student's own lighting experiment in an earlier session) [RECORD, transcript summary]. Grayscale does not equal night vision [RECORD]. The UART header uses JST-type connectors, not standard 2.54mm pins, which could not be sourced locally [RECORD, confirmed from photos in this conversation]
- **Evidence:** frame capture at 640x480 verified [RECORD]. Browser viewer confirmed working [RECORD]. PSRAM 8MB confirmed in build output [RECORD]. SD card available but not yet used [RECORD]
- **Status:** FINAL

### S-6: Removal of unused Adafruit libraries from platformio.ini

- **Decision:** whether to remove `adafruit/Adafruit MPU6050` and `adafruit/Adafruit Unified Sensor` from the dependency list
- **Chosen:** remove both [RECORD]
- **Alternatives considered:** leave them (no one argued for this) [RECORD]
- **Comparison:** the Adafruit library was abandoned because it rejects the clone chip's WHO_AM_I value. The code now uses direct register access via Wire.h. Neither library is imported anywhere [RECORD]. RTClib, OneWire, and DallasTemperature remain because they are genuinely used [RECORD, confirmed by Claude Code repo verification]
- **Reason:** dead code that creates a misleading impression the build depends on them [RECORD]
- **Constraint:** none
- **What it costs/limits:** nothing; no code references them [RECORD]
- **Evidence:** Claude Code session verified against the actual repository that no source file imports either library [RECORD]
- **Status:** FINAL. Confirmed executed by the Claude Code session [RECORD]

---

## Category 2: Sensing and Measurement Methods

### M-1: DLPF filter setting (0x00 vs 0x04)

- **Decision:** which MPU6050 digital low-pass filter bandwidth to use
- **Chosen:** 0x00 (~260Hz bandwidth) [RECORD]
- **Alternatives considered:** 0x04 (~21Hz bandwidth) [RECORD]
- **Comparison:** the actual loop rate was measured at 45.27Hz, giving a Nyquist limit of ~22.6Hz. The 21Hz filter (0x04) is the largest MPU6050 filter option below that Nyquist limit, making it the mathematically correct anti-aliasing choice [RECORD]. However, testing under 0x04 showed the system could barely distinguish fan speed changes — RMS stayed flat around 0.3 regardless of speed [RECORD]. Under 0x00, the system clearly responded with RMS climbing well above 1.0 and CAUTION triggering correctly [RECORD]. Calibration under 0x04 produced 0.3476, roughly half the 0.6926 measured under 0x00, confirming the filter substantially affects the signal [RECORD]
- **Reason:** "amplitude sensitivity was prioritised over frequency-domain correctness, since the system's detection method uses only RMS amplitude, not frequency content" [RECORD]. A system that can't distinguish operating conditions defeats its own purpose [RECORD]. Aliasing corrupts frequency attribution but doesn't necessarily eliminate energy from the RMS calculation [AI-SUGGESTED reasoning, accepted by the student]
- **Constraint:** the measured sample rate (45.27Hz) is fixed by the loop architecture; restructuring around a dedicated timer or interrupt was judged too large a change for the project's scope [RECORD]
- **What it costs/limits:** introduces a known, unquantified aliasing risk — high-frequency vibration content can fold into lower-frequency readings [RECORD]. The 21Hz filter's roll-off is gradual, not a hard cutoff, so some residual aliasing risk remains even under 0x04 [RECORD, flagged by Claude Code session]. This must be stated as a limitation, not presented as solved [RECORD]
- **Evidence:** loop timing measurement (22.09ms average, 45.27Hz) [RECORD, serial log]. Calibration under 0x04: 0.3476 [RECORD, serial log]. Fan speed test under 0x04: flat RMS ~0.3 [RECORD, student observation]. Fan speed test under 0x00: responsive RMS, CAUTION triggered [RECORD, serial logs pasted]
- **Status:** CHANGED. First version: 0x00 (from a hobbyist forum post, no project-specific justification). Changed to 0x04 after the loop timing measurement (justified by Nyquist). Changed back to 0x00 after empirical testing showed 0x04 suppressed the usable signal. Final version: 0x00, with the tradeoff documented as a known limitation.

### M-2: Gravity subtraction method (sqrt(ax²+ay²+az²) − 9.8)

- **Decision:** how to extract a vibration-representative signal from the accelerometer
- **Chosen:** compute the total acceleration magnitude and subtract 9.8 [RECORD, code in vibration.cpp]
- **Alternatives considered:** per-axis gravity/DC removal followed by combined dynamic-acceleration RMS [RECORD, raised by the external review, Finding 2]
- **Comparison:** the current method is mathematically independent of how the sensor's axes are labelled but is NOT equally sensitive to all physical vibration directions relative to gravity. For vibration perpendicular to gravity, the response is approximately second-order (small horizontal vibrations are mathematically attenuated). The per-axis approach would give a more physically meaningful dynamic-acceleration measurement [RECORD, external review analysis with mathematical derivation]
- **Reason:** the current method was the original implementation and was never changed [RECORD]. The per-axis approach was acknowledged as technically superior but not implemented [RECORD]
- **Constraint:** implementing the per-axis approach is additional work flagged as Phase 1 Items 4–5 of the remediation checklist [RECORD]. Whether it's in scope was left as an outstanding decision [RECORD]
- **What it costs/limits:** the current signal should be described as a "gravity-referenced acceleration-magnitude residual," not implied to be a fully gravity-compensated vibration measurement [RECORD, external review]
- **Evidence:** the external review provided the mathematical derivation showing the second-order response for horizontal vibration [RECORD]
- **Status:** FINAL (unchanged), with the limitation documented. The per-axis method is listed as potential future improvement, not committed to.

### M-3: RMS as the severity metric

- **Decision:** what single number to use for vibration severity classification
- **Chosen:** RMS (root mean square) over a rolling 50-sample window [RECORD]
- **Alternatives considered:** none discussed in this conversation [RECORD]
- **Comparison:** n/a
- **Reason:** predates this conversation [UNKNOWN from this chat]
- **Constraint:** ISO 10816-7 uses RMS velocity in mm/s; this system computes an acceleration-derived indicator in m/s². The current thresholds must not be presented as ISO severity categories [RECORD, external review Finding 17]
- **What it costs/limits:** the 50-sample window at the measured 45.27Hz rate covers approximately 1.10 seconds, not the assumed 1.0 seconds [RECORD]
- **Evidence:** RMS tracked real disturbance correctly across all test logs [RECORD, transcript summary]
- **Status:** FINAL

### M-4: Threshold multipliers (1.2× for CAUTION, 1.5× for WARNING)

- **Decision:** where to set the CAUTION and WARNING trigger points relative to baseline
- **Chosen:** CAUTION at 1.2× baseline, WARNING at 1.5× baseline [RECORD]
- **Alternatives considered:** statistically derived thresholds (mean ± standard deviation from calibration data) [RECORD, external review Finding 7]
- **Comparison:** the external review noted that 90 seconds of calibration data is enough to compute basic statistics, so the "too short for statistics" justification was weak. The percentage approach is simpler to commission and interpret, which is a legitimate reason for a prototype [RECORD, external review]
- **Reason:** no specific reasoning was stated for these exact multipliers in this conversation. They are acknowledged as prototype design parameters, not experimentally validated thresholds [RECORD]
- **Constraint:** nothing in this project establishes that 20% and 50% correspond to any known fault severity or published limit [RECORD, external review Finding 6]
- **What it costs/limits:** provisional values only. The external review recommended either validating them through controlled tests or explicitly classifying them as provisional [RECORD]
- **Evidence:** both thresholds triggered correctly during formal trials [RECORD, trial logs in transcript summary]
- **Status:** FINAL but explicitly provisional

### M-5: WARNING duration logic (rolling fraction, not simple timer)

- **Decision:** how to prevent brief spikes from triggering WARNING
- **Chosen:** a rolling history of 6000 samples; WARNING triggers when ≥50% of the history is above the elevated threshold [RECORD]
- **Alternatives considered:** a simple continuous timer [RECORD, this was the initial concept before the rolling fraction was implemented]
- **Comparison:** the rolling fraction allows intermittent elevated readings to accumulate, which is more representative of a developing fault than requiring continuous elevation [RECORD, external review Finding 8]. But it also means WARNING can remain active after instantaneous vibration has returned to normal [RECORD, demonstrated in Trial 1's direct WARNING→OK transition]
- **Reason:** to avoid treating short disturbances as sustained faults [RECORD]. The Aldrini and Chihi paper supports the principle of time-gated evaluation [RECORD, external review Finding 9]
- **Constraint:** the history is defined by loop iterations, not elapsed time. If the loop slows down (e.g. when the dashboard is added), the real-world duration changes silently [RECORD, external review Finding 8b]. At the measured 45.27Hz, the 6000-sample window covers ~132.5 seconds, not the intended 120 [RECORD]
- **What it costs/limits:** the specific 2-minute constant is an engineering design parameter, not justified by the Aldrini/Chihi paper (which uses variable observation windows) [RECORD, external review Finding 9]
- **Evidence:** Trial 1 demonstrated the direct WARNING→OK recovery (skipping CAUTION), consistent with the algorithm [RECORD, trial log]. Trial 1 onset-to-WARNING: 85 seconds; Trial 2: 68 seconds (provisional), both measured against the ~132.5s actual window [RECORD]
- **Status:** FINAL. The paper citation supports the principle, not the specific parameter.

### M-6: STOPPED detection (below-baseline monitoring)

- **Decision:** whether and how to detect a pump that has stopped running during scheduled hours
- **Chosen:** STOPPED status triggered when RMS stays below 50% of baseline for 1 minute, only during scheduled run hours [RECORD]
- **Alternatives considered:** also flagging a pump running slower than baseline (Case 2) [RECORD, raised by the student]. This was rejected: the sensor cannot distinguish intentional throttling from a developing fault without motor speed or current data [AI-SUGGESTED reasoning, accepted]
- **Comparison:** Case 1 (pump stops entirely) is genuinely more urgent than high vibration — zero filtration. Case 2 (runs quieter) needs information this sensor doesn't have [RECORD]
- **Reason:** "a pump that stopped running when it shouldn't have" is as important as one running badly [STUDENT, paraphrased]. The RTC/schedule is required to distinguish expected-off from unexpected-off [RECORD]
- **Constraint:** the 50% threshold and 1-minute duration are reasoned starting points based on observed resting RMS (~0.2–0.3 against baseline ~0.6926), not measured/validated values [RECORD]. Would need real measurement against the actual deployed pump [RECORD]
- **What it costs/limits:** low vibration does not conclusively establish a stopped pump — could also be a lower speed, a shifted sensor, or a data problem [RECORD, external review Finding 15]
- **Evidence:** resting RMS consistently 0.2–0.3 across test logs [RECORD]. Code written but not tested on hardware [RECORD]
- **Status:** BUILT, NOT TESTED

### M-7: Scene-change detection method (reference frame comparison)

- **Decision:** how to use the camera to contribute to occupancy detection
- **Chosen:** capture a reference frame of the empty scene, compare subsequent frames within a masked ROI, compute the fraction D of pixels that changed by more than a brightness threshold [RECORD]
- **Alternatives considered:** facial recognition or person identification [RECORD, explicitly rejected on ethical and technical grounds]. Continuous camera monitoring [RECORD, rejected in favour of radar-triggered operation]
- **Comparison:** the chosen method is computationally simple, runs on the ESP32-S3, and avoids identifying individuals. It provides a numerical scene-change measurement, not a person classification [RECORD, consistent with the pasted external review's recommendation]
- **Reason:** "the camera could calculate how much the monitored image changes relative to a reference image" [RECORD, from the pasted review]. Avoids ethical issues with identification [RECORD]. Avoids the computational cost of more sophisticated detection [RECORD]
- **Constraint:** the camera genuinely needs light [RECORD, confirmed by earlier lighting test]. Frame rate ~1fps limits tracking [RECORD]. The ROI must be configured per installation [RECORD]
- **What it costs/limits:** cannot distinguish lighting changes from scene changes (confirmed: switching off the light produced a steady 14.7% change) [RECORD, test log]. Cannot distinguish a person from any other scene change [RECORD]. The 5% threshold was too low (noise floor 7–10%, real entries 34–96%); raised to 15% [RECORD]
- **Evidence:** three successful detection events with clear entry/exit transitions [RECORD, test logs pasted]. Lighting change test [RECORD, test log pasted]. Out-of-frame radar detection finding [RECORD, student observation]
- **Status:** FINAL for the method. The threshold was changed from 5% to 15% in the code [RECORD], but CORRECTED 19 October 2026: the code compares a percentage with a fraction, so the effective threshold was probably 0.15%, not 15% [UNCONFIRMED against the student's file]. Any claim that 15% was applied must wait for the fix and a retest.

### M-8: Fan speed testing as validation method

- **Decision:** whether changing fan speed is a valid way to test the vibration detection system
- **Chosen:** use fan speed changes to validate the detection pipeline's logical behaviour, explicitly NOT as a simulation of mechanical faults [RECORD]
- **Alternatives considered:** none available (no access to a real pump) [RECORD]
- **Comparison:** "turning a fan from speed 1 to speed 3 is not equivalent to introducing a mechanical defect. A healthy motor operating at different speeds may naturally produce different vibration amplitudes" [RECORD, external review Finding 1]. The fan test validates code correctness, not fault discrimination [RECORD]
- **Reason:** "the fan is just a controllable way to make the input number go up and down on command, nothing more, nothing less" [RECORD]
- **Constraint:** no access to a real pool pump; battery-powered fan produces variable vibration depending on charge level [RECORD]
- **What it costs/limits:** "the appropriate engineering claim is that the subsystem provides baseline-relative vibration anomaly detection that may support preventative maintenance. It should not be presented as a validated mechanical-failure prediction system" [RECORD, external review]
- **Evidence:** earlier trials showed clear RMS response to fan operation under 0x00 filter [RECORD]. Under 0x04, fan speed changes produced almost no RMS change [RECORD]
- **Status:** FINAL (the test method itself). The student's own question "is the speed actually telling us something about vibration" was answered honestly [RECORD]

---

## Category 3: Architecture and Communication

### A-1: Two-board architecture (DOIT + Waveshare)

- **Decision:** how many ESP32 boards and what role for each
- **Chosen:** DOIT ESP32 DevKit V1 for vibration, water quality, radar, RTC, and dashboard hosting; Waveshare ESP32-S3-CAM for camera and detection logic [RECORD]
- **Alternatives considered:** none discussed in this conversation; this was already established [RECORD]
- **Comparison:** n/a in this conversation
- **Reason:** not stated in this conversation [UNKNOWN from this chat]. The division follows naturally from which board has a camera and which has the other sensor pins [RECORD, implicit]
- **Status:** FINAL

### A-2: Schedule-gated vibration monitoring

- **Decision:** should vibration monitoring run all the time or only during expected pump operating hours
- **Chosen:** only evaluate vibration during a configured run window; report "OFF SCHEDULE" outside it [RECORD]
- **Alternatives considered:** monitoring all the time [RECORD, implicit — the system had no schedule concept before the RTC was added]
- **Comparison:** without a schedule, a pump sitting idle overnight correctly reads near-zero RMS, which would either be treated as OK (masking a genuine daytime shutdown) or flagged (false alarm every night) [RECORD]
- **Reason:** "the system needs context about when the pump is supposed to be running" to distinguish expected-off from unexpected-off [RECORD]
- **Constraint:** requires a working RTC and an operator-set schedule [RECORD]
- **What it costs/limits:** if the schedule represents pool opening hours instead of pump operating hours, vibration monitoring would be incorrectly suppressed when the pump should be running outside public hours [RECORD, external review Finding 14]
- **Status:** FINAL. Code written but not tested [RECORD]. The rename from scheduleShouldBeMonitoring() to pumpScheduleShouldBeRunning() was proposed but not applied [RECORD].

### A-3: Fail-open schedule default

- **Decision:** what should happen if the RTC isn't working or no schedule has been set
- **Chosen:** scheduleShouldBeMonitoring() returns true — keep monitoring [RECORD]
- **Alternatives considered:** return false (stop monitoring) [RECORD, implicit — this is the opposite choice]
- **Comparison:** "a missing schedule should never silently disable fault detection" [RECORD]
- **Reason:** "it's safer to monitor unnecessarily than to miss a real fault because someone forgot to enter a schedule" [RECORD]
- **Constraint:** none
- **What it costs/limits:** could generate false STOPPED alerts during expected off-hours before a schedule is entered, since the system doesn't know the pump is supposed to be off [AI-SUGGESTED implication, not discussed]
- **Status:** FINAL

### A-4: Pump schedule vs pool access schedule separation

- **Decision:** whether one stored schedule window serves both vibration monitoring and occupancy monitoring
- **Chosen:** two separate, independently settable schedules [RECORD]
- **Alternatives considered:** one shared schedule [RECORD, this was the original implicit design before the review flagged it]
- **Comparison:** "pool opening hours and pump operating hours are not the same thing — a filtration pump may legitimately run outside public hours" [RECORD, external review Finding 14]
- **Reason:** the external review identified four distinct concepts: facility access schedule, expected pump operating schedule, observed pump operating state, and vibration health status [RECORD]
- **Constraint:** none technical; both use the same RTC and Preferences mechanism [RECORD]
- **What it costs/limits:** two stored values instead of one; the dashboard needs two separate forms [RECORD]
- **Status:** DECIDED but NOT IMPLEMENTED. The rename was proposed but not applied to the code [RECORD].

### A-5: Radar wired to DOIT board, not camera board

- **Decision:** which ESP32 board to physically connect the radar to
- **Chosen:** the DOIT board [RECORD]
- **Alternatives considered:** the camera board (Waveshare ESP32-S3-CAM) [RECORD, this was the original plan]
- **Comparison:** the camera board's UART header uses JST-type connectors. The student could not source the matching plug cables locally [RECORD, confirmed from uploaded photos of both boards]. The DOIT board uses standard 2.54mm pin headers, compatible with ordinary jumper wires the student already had [RECORD]
- **Reason:** "I don't have those thin connectors. I can't find them anywhere" [STUDENT]. The DOIT board uses pins compatible with existing jumper wires [RECORD]
- **Constraint:** local component availability — JST cables not obtainable [RECORD]
- **What it costs/limits:** the radar state must be communicated from the DOIT board to the camera board over WiFi, adding latency (~500ms polling) and a network-dropout failure mode that a direct wire wouldn't have [RECORD]. A ground tie between both boards is required [RECORD]
- **Evidence:** photos of both boards uploaded showing the JST connectors on the camera board and standard pins on the radar [RECORD]
- **Status:** FINAL

### A-6: WiFi bridge (DOIT board as AP, camera board as client)

- **Decision:** how to connect the two ESP32 boards
- **Chosen:** the DOIT board creates a local WiFi access point ("PoolPlantRadar", password "poolplant123"), serves radar data as JSON at /radar. The camera board connects as a WiFi station and polls that endpoint every 500ms [RECORD]
- **Alternatives considered:** connecting both boards to the student's phone hotspot [RECORD, the camera board previously connected to hotspot "Nyeleti" for the viewer]. Direct wired connection [RECORD, blocked by the JST connector issue, A-5]
- **Comparison:** the self-contained AP approach needs no external router or internet. The phone hotspot approach would add a dependency on the phone being on [AI-SUGGESTED reasoning]. The wired approach would be more reliable but is blocked by the connector [RECORD]
- **Reason:** "uses your available hardware, avoids the connector issue, and demonstrates communication between independent embedded nodes without requiring internet access" [RECORD, from pasted review]
- **Constraint:** the JST connector issue (A-5) forced a wireless approach [RECORD]
- **What it costs/limits:** WiFi adds latency and a failure mode. The pasted review noted that "wireless is not automatically better than wired" and wired should be documented as an alternative [RECORD]
- **Evidence:** JSON endpoint confirmed from phone browser [RECORD]. Camera board successfully polled radar data [RECORD, serial logs]. Whether the phone and both boards were on the network at the same time, and whether the camera viewer loaded from the phone on that network, was not confirmed [UNKNOWN, corrected 19 October 2026]. The camera board's printed address changed between boots (192.168.4.4, then 192.168.4.2) [RECORD]
- **Status:** FINAL

### A-7: GPIO5 for radar OUT (not GPIO4)

- **Decision:** which pin to use for the radar's digital output
- **Chosen:** GPIO5 [RECORD]
- **Alternatives considered:** GPIO4 [RECORD, this was the original plan from the transcript summary]
- **Comparison:** GPIO4 is already used by the DS18B20 temperature sensor (water quality module) [RECORD, confirmed when the student provided the full pin assignment table]. Wiring both to GPIO4 would cause two different signals to fight over one line [RECORD]
- **Reason:** GPIO4 conflict with DS18B20 [RECORD]
- **Constraint:** hardware pin conflict [RECORD]
- **What it costs/limits:** GPIO5 is an ESP32 boot-strapping pin; startup issues were flagged as possible but none were observed [RECORD, from pasted radar summary]
- **Evidence:** the student's pin assignment table showing GPIO4 used by DS18B20 [RECORD, pasted in conversation]
- **Status:** FINAL

### A-8: Radar-triggered camera operation

- **Decision:** should the camera process frames continuously or only when the radar detects presence
- **Chosen:** radar-triggered — the camera only compares frames against the reference when radarClientPresence() returns true [RECORD]
- **Alternatives considered:** continuous camera monitoring [RECORD, discussed and rejected]
- **Comparison:** continuous monitoring draws significant power, generates constant processing load, and is unreliable for detecting static presence from pixel changes alone [RECORD]. The radar catches the standing-still case the camera would miss [RECORD, confirmed by test data where camera change dropped to 5–10% for a stationary person while radar continued reporting presence]
- **Reason:** "the radar is the reliable trigger that never misses a person, the camera provides visual evidence of what was actually there" [RECORD]. Constant frame capture is also unnecessary power draw [RECORD]
- **Constraint:** acknowledged limitation: the camera cannot detect an event the radar completely misses [RECORD, from pasted review]
- **What it costs/limits:** if the radar fails or misses something, the camera never processes that event [RECORD]. The pasted review called this "a deliberate simplification for the proof of concept" [RECORD]
- **Evidence:** detection test showed radar triggering camera correctly; camera change rose on entry (34–96%), dropped while stationary (5–10%), spiked on exit [RECORD, test logs]
- **Status:** FINAL

### A-9: Single entrance monitoring scope (not full perimeter)

- **Decision:** what area the occupancy module covers
- **Chosen:** one defined entrance/access point [RECORD]
- **Alternatives considered:** full pool perimeter with multiple radar nodes [RECORD, raised by the student: "for future I would say at all points where a person could enter like walls and stuff because if it's theft they won't use the obvious entrance"]
- **Comparison:** one entrance is testable in the student's room and achievable with current hardware. Multiple entrances would require replicating the same node architecture at each point [RECORD]
- **Reason:** "monitoring a defined access point is a far more defensible, testable scope for the proof of concept" [RECORD]. The architecture was designed so additional nodes require replication, not redesign [RECORD]
- **Constraint:** available hardware (one radar, one camera) [RECORD]. Also: the pasted external review recommended entrance-focused monitoring because the LD2410C is primarily designed for indoor/bounded detection [RECORD]
- **What it costs/limits:** does not cover non-obvious entry points. The student correctly identified this as a real deployment concern [STUDENT]
- **Status:** FINAL for proof of concept; multiple entrances documented as future work [RECORD]

### A-10: Detection endpoint with CORS headers

- **Decision:** how the dashboard (served by the DOIT board) accesses detection data from the camera board
- **Chosen:** the camera board serves a /detection JSON endpoint with Access-Control-Allow-Origin: * header [RECORD]. A single-source alternative was then approved: the DOIT board polls /detection itself and bundles everything into one response [RECORD]
- **Alternatives considered:** browser fetching directly from both boards (blocked by CORS without the header) [RECORD]. A server-side proxy on the DOIT board [RECORD, raised by the dashboard chat]
- **Comparison:** the single-source approach (DOIT polls camera, bundles everything) eliminates the browser-to-camera dependency and provides one place to determine camera online/offline status [RECORD]
- **Reason:** "one fetch per refresh instead of two; no dependence on the browser-to-camera link; one place that decides whether the camera is offline" [RECORD, from the dashboard chat's proposal]
- **Constraint:** CORS browser security restrictions require the header for cross-origin requests [RECORD]
- **What it costs/limits:** camera data can be a few seconds old on the dashboard [RECORD]. The CORS header was still added for flexibility [RECORD]
- **Status:** CORS endpoint BUILT [RECORD]. Single-source polling APPROVED but NOT YET BUILT [RECORD]

---

*Categories 4–6 continue in the next section. Say "continue" to proceed.*
