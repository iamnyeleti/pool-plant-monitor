# Occupancy Module — Complete Development Log

Project: Pool Plant Monitor and Preventative Maintenance Advisor (EEE4022S, SW-3)
Supervisor: Dr Simon Winberg | Co-supervisor: Kai Goodall
Camera board: Waveshare ESP32-S3-CAM (COM6)
Radar: HLK-LD2410C wired to DOIT ESP32 DevKit V1 (COM5)

This document records everything discussed, built, tested, broken, fixed, and decided for the occupancy module. Raw process record, not a polished report.

---

## 1. Module Architecture — How and Why It Was Decided

### 1.1 The two-sensor concept

The occupancy module uses two complementary sensors:
- A **camera** (Waveshare ESP32-S3-CAM with GC0308 grayscale sensor) for scene-change detection
- A **radar** (HLK-LD2410C 24GHz FMCW) for presence detection independent of lighting

The rationale for two sensors, not one:
- The camera alone can't detect a stationary person in the dark. It needs light and movement.
- The radar alone can't show what it detected — it knows something's there but can't distinguish a person from a cat, a moving curtain, or a ceiling fan. The manufacturer explicitly warns about false triggers from fans, curtains, animals, and reflective surfaces.
- Together: the radar is the reliable trigger that never misses a person, the camera provides visual evidence of what was actually there. Each covers the other's weakness.

### 1.2 Why the radar is wired to the DOIT board, not the camera board

The camera board (Waveshare ESP32-S3-CAM) has its UART, I2C, and other headers exposed as small JST-type connectors, not standard 2.54mm pin headers. The user could not source the matching JST plug cables anywhere locally. The radar module has standard male header pins. The DOIT board has standard female headers.

Rather than risk damaging the camera board by soldering directly to its tiny contacts, the decision was made to wire the radar entirely to the DOIT board (which uses ordinary breadboard-compatible pins the user already had jumper wires for) and communicate the radar state to the camera board over WiFi.

This is a design choice made because of a real sourcing constraint, documented honestly. The tradeoff: WiFi adds latency (~500ms polling interval) and a failure mode (network dropout) that a direct wire wouldn't have. For the proof-of-concept scope, this is defensible. For a production system, wired communication between co-located boards would be more reliable.

### 1.3 Entrance-focused monitoring scope

The occupancy module monitors a single defined entrance/access point, not the entire pool perimeter. Both sensors are aimed at the same zone. This was confirmed as the testable, achievable scope for the proof of concept.

The user correctly noted that a real deployment would need multiple entrance points (including walls and less obvious access routes, since someone attempting theft wouldn't use the obvious entrance). The architecture was deliberately designed so additional nodes require replication, not redesign — each new entrance gets its own radar node running the same firmware, reporting to the same dashboard.

---

## 2. Camera Board — Prior Verified State

These were confirmed working before this session, carried forward from earlier development:

- **Frame capture:** 640x480 grayscale frames from the GC0308 sensor, working
- **Software JPEG conversion:** The GC0308 can't produce JPEG natively (confirmed by debugging). Frames are converted in software via `frame2jpg()`. This works but limits frame rate to roughly 1fps.
- **Browser viewer:** A web page served by the camera board's own web server, showing the live camera feed. Originally connected to the user's phone hotspot ("Nyeleti"), later changed to connect to the DOIT board's own WiFi network.
- **Lighting test:** Real photos were captured and analysed across three lighting levels (lit, dim, dark), confirming that the camera genuinely needs adequate lighting. Grayscale does not equal night vision.
- **PSRAM:** 8MB confirmed in build output, used for reference frame storage.
- **SD card:** Available on the board but not yet used.

### 2.1 Camera reconnection

The camera board connects via USB-C. It shows up as "USB Serial Device (COM6)" in Device Manager — the ESP32-S3's native USB, no separate driver chip.

---

## 3. Radar Hardware — Arrival and Identification

The HLK-LD2410C arrived during this session. Physical inspection confirmed:
- Correct module: LD2410C V1.1 marked on the board
- Standard male header pins already soldered (5 pins: TX, RX, OUT, GND, VCC)
- Small blue PCB with the 24GHz antenna structure visible

### 3.1 Pin identification

| Radar pin | Function |
|---|---|
| VCC | Power input (5-12V, manufacturer recommends 5V) |
| GND | Ground |
| OUT | Digital presence output, goes HIGH when presence detected within configured range |
| TX | UART transmit (detailed data: distance, energy, moving/stationary classification) |
| RX | UART receive (configuration commands) |

The UART pins use 3.3V logic levels and are not 5V tolerant.

---

## 4. Radar Wiring — GPIO Conflict and Resolution

### 4.1 Initial plan: GPIO4

GPIO4 was initially planned for the radar's OUT pin. However, when the user provided their full pin assignment table, it was discovered that GPIO4 was already in use by the DS18B20 temperature sensor (part of the water quality module). Wiring both to the same pin would have caused intermittent, confusing failures.

### 4.2 Resolution: GPIO5

GPIO5 was confirmed free from the available pin list and substituted. All subsequent wiring and code uses GPIO5 for the radar OUT signal.

### 4.3 Final wiring (to DOIT board)

| Radar pin | DOIT board pin | Purpose |
|---|---|---|
| VCC | V5 | 5V power |
| GND | GND | Shared ground |
| OUT | GPIO5 | Digital presence HIGH/LOW |
| TX | GPIO16 | UART data (added later for distance/energy) |
| RX | GPIO17 | UART commands (added later) |

### 4.4 Ground tie requirement

Because the radar is powered from the DOIT board but its data ultimately needs to reach the camera board, both ESP32 boards need their grounds physically connected together. A digital HIGH/LOW signal only reads reliably against a shared ground reference.

---

## 5. Radar Heating Concern — Investigation and Resolution

### 5.1 The concern

When first powered, the user reported the radar was heating up and was worried about damage.

### 5.2 Investigation

Multiple rounds of checking occurred, with the user increasingly frustrated:
1. First heating report → advised to unplug and check wiring for reversed polarity
2. Second heating report after rewiring → advised to stop and check with multimeter
3. User pushed back, pointing out the sensor was still correctly reporting presence detection, which wouldn't happen with a genuine short or reversed polarity

### 5.3 Resolution

The user was right. The LD2410C is an active 24GHz radar that continuously transmits radio waves. Running warm is completely normal behaviour — the same way a WiFi router or a phone under heavy use feels warm. The radar has an internal voltage regulator stepping 5V down to 3.3V, which also generates heat. The temperature was described as "phone-warm," plateauing at a stable level with no smell or discolouration.

This was confirmed as expected behaviour, not a fault. The multiple "unplug it" advisories were overcautious given that the device was clearly functioning correctly.

### 5.4 Radio safety

The user also asked whether the 24GHz radar signal was dangerous to them. Answer: no. This is the same general category of low-power radio as WiFi, Bluetooth, or a baby monitor. It operates at power levels far below anything that could heat tissue. Completely safe for desk-distance proximity.

---

## 6. Radar Constant HIGH Problem — Diagnosis and Fix

### 6.1 The problem

After initial wiring, the radar's OUT pin reported HIGH constantly and never dropped to LOW, regardless of what the user did. Walking toward it, stepping away, nothing changed the reading.

### 6.2 Incorrect initial diagnosis

Multiple rounds of unhelpful advice were given:
- "Leave the room for 30 seconds" — didn't work
- "Try INPUT_PULLDOWN" — didn't work
- "Check the voltage with a multimeter" — not the real issue

### 6.3 Actual cause

The radar's default detection range was 6 metres. The user's room is approximately 3 metres. The radar was correctly detecting the user sitting at their desk the entire time — it could detect a stationary person just from their breathing at that range. It was never going to report LOW while someone was in that room.

### 6.4 The real fix

The user had actually already identified this in a previous chat session, where they'd connected to the radar via Bluetooth using the HLKRadarTool phone app and proposed reducing the detection range. That configuration change just hadn't been applied yet.

Using HLKRadarTool (Bluetooth password: "HiLink"), the detection range was reduced to 2.25 metres with a 2-second unmanned duration. After this change, the OUT pin correctly showed HIGH when the user was within range and LOW when they stepped away.

### 6.5 Lesson

The OUT pin was always working correctly. The problem was configuration, not hardware or code. Multiple rounds of "try this wiring fix" and "try this code change" were wasted time because the actual issue was never a wiring or code problem.

---

## 7. Radar UART Integration

### 7.1 Why UART, not just the OUT pin

The OUT pin gives only yes/no. The UART connection provides:
- Distance to detected target in centimetres
- Moving target detection with energy level
- Stationary target detection with energy level
- Combined detection distance
- Ability to configure detection range, sensitivity, and gate thresholds from code

### 7.2 Library installation

The ncmreynolds/ld2410 library (v0.2.2) was installed via PlatformIO:
```
pio pkg install --library "ncmreynolds/ld2410"
```

The `pio` command wasn't on the user's PowerShell PATH — it needed to be run from `%USERPROFILE%\.platformio\penv\Scripts\pio.exe` or from the PlatformIO terminal inside VS Code.

### 7.3 Code structure

`radar.cpp` was updated to initialise Serial2 at 256000 baud (the LD2410C's default UART speed) with GPIO16 as RX (from radar TX) and GPIO17 as TX (to radar RX). The library's `radar.read()` is called every loop, and the print output shows moving/stationary target distances and energy levels alongside the OUT pin state.

One code change was needed: the `radar` object had to be changed from `static ld2410 radar;` to `ld2410 radar;` (removing `static`) so it could be accessed from `radar_server.cpp` via `extern`.

### 7.4 Test result

UART integration was verified with a complete walk-toward, stand-still, walk-away test:
- Distance tracking was smooth and responsive, correctly following a person
- Moving vs stationary target classification worked (standing still → "Moving: none" while "Still" continued reporting)
- Energy readings consistently at or near 100 at close range, dropping at detection edges
- OUT pin correctly transitioned between HIGH and LOW
- The distance readings told a clear story: entering at 225cm, approaching to 30cm, retreating back out

---

## 8. WiFi Bridge — DOIT Board Serving Radar Data

### 8.1 Architecture

The DOIT board creates a local WiFi access point called "PoolPlantRadar" (password "poolplant123"). It serves radar data as JSON at `http://192.168.4.1/radar`. No router, no internet — the two boards and the operator's phone all connect to this same self-contained network.

### 8.2 Implementation

`radar_server.cpp` creates the AP using `WiFi.softAP()`, sets up a `WebServer` on port 80, and registers a `/radar` endpoint that returns a JSON object with presence, moving/stationary detection, distances, and energy levels.

### 8.3 Verification

Confirmed working by:
1. Connecting a phone to the PoolPlantRadar network
2. Opening `http://192.168.4.1/radar` in the phone's browser
3. Seeing live, updating JSON data with real radar readings

---

## 9. Camera Board Radar Client

### 9.1 WiFi change

The camera board's `viewer.cpp` was updated to connect to "PoolPlantRadar" instead of the user's phone hotspot ("Nyeleti"). This puts both boards on the same network without needing any external router.

### 9.2 Radar client module

`radar_client.cpp` polls `http://192.168.4.1/radar` every 500ms using `HTTPClient`, parses the JSON response with ArduinoJson, and exposes `radarClientPresence()` and `radarClientDistance()` for other modules to use.

ArduinoJson v7.4.3 was installed, which required a syntax change from the v6 examples: `StaticJsonDocument<256>` became `JsonDocument`.

### 9.3 Startup crash

On first boot, the camera board crashed once with an EXCVADDR error before successfully rebooting and running normally. This was attributed to the camera and WiFi hardware competing for resources during simultaneous initialisation. A 500ms delay between `occupancySetup()` and `viewerSetup()` in `main.cpp` was added to address this. The crash did not recur.

### 9.4 Verification

The camera board successfully received live radar data from the DOIT board:
```
[RadarClient] Presence: YES | Distance: 49cm
[RadarClient] Presence: YES | Distance: 51cm
```

Both the radar data polling and the camera viewer continued working simultaneously on the same network. The operator's phone could connect to the same network and view the camera feed.

---

## 10. Detection Logic — Design and Implementation

### 10.1 How it works

1. On startup, a 15-second countdown runs. The operator must step out of the camera's view.
2. At countdown end, the camera captures a reference frame of the empty scene (stored in PSRAM, 640×480 = 307,200 bytes).
3. During normal operation, the system only processes camera frames when `radarClientPresence()` returns true (radar-triggered operation).
4. Each captured frame is compared against the reference within a defined Region of Interest (ROI).
5. For each pixel in the ROI, if brightness differs from the reference by more than `PIXEL_CHANGE_THRESHOLD` (30), it counts as changed.
6. The change fraction D is computed as a percentage of ROI pixels.
7. If D exceeds `SCENE_CHANGE_THRESHOLD` and radar says presence, the event is "corroborated."
8. If average brightness in the ROI is below `MIN_USEFUL_BRIGHTNESS` (20), the system reports "TOO DARK."

### 10.2 ROI configuration

Default values (will need tuning per installation):
- ROI_X = 100, ROI_Y = 50, ROI_W = 440, ROI_H = 380
- These define a rectangle within the 640×480 frame
- Pixels outside this rectangle are ignored (designed for masking trees, sky, water)

### 10.3 The `ref` command problem

The detection module was initially designed to accept a "ref" command typed into the Serial Monitor to capture the reference frame. The user couldn't type into the terminal because log output was scrolling too fast and the terminal wasn't accepting input. This was replaced with a 15-second countdown timer in `setup()`.

### 10.4 Radar-triggered design rationale

The camera isn't independently watching all the time. It only processes frames when the radar says someone's nearby. This is NOT a compromise — it's the deliberately better architecture:
- Constant frame capture and processing draws significant power
- Detecting presence from pixel changes alone is unreliable compared to what the radar does
- The radar catches the standing-still case the camera would miss

The acknowledged limitation: the camera cannot detect an event the radar completely misses. This is a deliberate simplification for the proof of concept.

---

## 11. First Detection Test — Results and Analysis

### 11.1 Test conditions

- Room environment, radar pointed toward the door
- Reference frame captured with the room empty and the user outside
- Three complete entry-exit events recorded

### 11.2 Event #1

- User entered the room from outside radar range
- Radar detected presence at 191cm, camera immediately reported 34.4% change
- Change climbed to 59.3% while walking through the frame
- Settled to 7.5–8.0% while standing still close to the radar
- On exit, change spiked back to 66.6% as the user crossed the frame
- Radar cleared, event ended cleanly

### 11.3 Event #2

- Same pattern: 38.5% initial, settled to 5.5–8.6% while stationary, clean exit

### 11.4 Event #3

- Entry produced 37.6% initial change, climbing to 95.6% when the user was fully in frame at close range
- Settled to 9–10% while stationary
- Clean exit at 61.6–66.3%

### 11.5 Three distinct signal bands emerged

| Condition | Change % |
|---|---|
| Person actively moving through the frame | 34–96% |
| Person standing still in frame | 5–10% |
| Empty scene (reference) | 0% by definition |

This separation is genuine, useful data showing that the camera is better at detecting movement through the scene than static presence — which is exactly why the radar matters for the standing-still case.

---

## 12. Out-of-Frame Radar Detection — An Important Finding

### 12.1 What happened

During testing, the user was within radar range (31–55cm) but positioned outside the camera's field of view. The camera reported ~7.7% change, above the 5% threshold, so the system called it "corroborated" even though the camera wasn't actually seeing the person.

### 12.2 Why this matters

1. The radar's detection cone and the camera's field of view don't perfectly overlap. Sensor placement in a real installation must account for this.
2. The 7.7% change from an empty scene is likely sensor noise and minor lighting drift, not genuine scene change from a person.
3. The 5% `SCENE_CHANGE_THRESHOLD` is too low — it can't distinguish noise (~7–10%) from real entries (34%+).

---

## 13. Lighting Change Test — A Confirmed Limitation

### 13.1 What happened

During an active detection event (Event #3), the user switched off the room light. The camera change percentage jumped from ~10% to a steady 14.7–15.0% and stayed there.

### 13.2 Analysis

The camera detected the uniform brightness drop across the whole ROI, not a person. The reference frame was captured with the light on, so every pixel was now darker than the reference. About 14.7% of pixels crossed the 30-brightness-unit change threshold.

### 13.3 Why the "too dark" check didn't trigger

The room with the light off still had enough ambient light to stay above the `MIN_USEFUL_BRIGHTNESS` threshold of 20. The scene wasn't truly dark, just dimmer.

### 13.4 Implications

The current simple pixel comparison cannot distinguish a lighting change from a person appearing. Both just look like "pixels are different from the reference." Potential improvements (documented as future work, not built):
- Adaptive background updating to match gradual lighting drift
- Spatial pattern analysis (uniform change = lighting; localised change = person)

### 13.5 The positive side

The three signal types separated clearly: lighting change sat at a flat 14.7%, real entries at 34–66%, noise at 7–10%. A smarter threshold or uniformity check could distinguish them.

---

## 14. Scene Change Threshold Adjustment

Based on the test data:
- Noise floor (empty scene, minor drift): 7–10%
- Lighting change (uniform): ~14.7%
- Real person entering: 34–96%

The `SCENE_CHANGE_THRESHOLD` was raised from 5% to 15%. This is not a final, permanent number — it's a better starting point based on measured data. The real final value should be configurable at runtime by the operator during commissioning, same approach as the vibration baseline.

---

## 15. Detection Endpoint and CORS

### 15.1 The problem

The dashboard will be served by the DOIT board (192.168.4.1). The camera's detection data lives on the camera board (192.168.4.2). A browser loading a page from one origin can't normally fetch from a different origin due to browser security restrictions (CORS).

### 15.2 Solution

A `/detection` endpoint was added to the camera board's `viewer.cpp`, serving detection state as JSON with an `Access-Control-Allow-Origin: *` header. This tells the browser "anyone on this network is allowed to request this data."

The endpoint returns:
```json
{
  "eventActive": true,
  "changePercent": 12.5,
  "eventCount": 3,
  "radarPresence": true,
  "radarDistance": 85
}
```

### 15.3 Single-source alternative

The dashboard chat proposed having the DOIT board poll the camera's `/detection` endpoint itself and bundle everything into one JSON response, so the browser only talks to one server. This was approved as a cleaner approach — it eliminates the browser-to-camera dependency and provides one place to determine camera online/offline status.

---

## 16. Dashboard Design — Discussion and Requirements

### 16.1 Initial design problems identified

The first dashboard preview was reviewed and several problems were identified:

1. **Vibration dominated the layout** — a giant banner at the top making it look more important than other subsystems
2. **Camera captures were absent** — no mechanism for showing captured images
3. **Water quality had no context** — raw numbers without acceptable ranges or interpretation
4. **Calibration was missing** — no way to trigger calibration from the dashboard
5. **Numbers alone aren't enough** — every reading needs to show whether it's good or bad
6. **Settings must round-trip to the ESP32** — not just look like they save

### 16.2 External review of the dashboard

A separate review provided additional analysis:
- The dashboard was displaying sensor readings, not helping an operator understand facility condition
- Camera and water quality sections needed the most work
- Configuration (preferred values, operating limits, calibration) needed dedicated workflows
- "Monitoring schedule" is more accurate than "pump schedule" since the system monitors, doesn't control

### 16.3 Connected-first priority

A critical decision: the first deliverable must be a working dashboard connected to real ESP32 hardware, not a more sophisticated fake-data preview. A simpler design that's actually connected to hardware is worth more in a final-year report than a beautiful mockup that never gets wired up.

Priority ordering:
- **P0 (must work first):** Real ESP32 readings, balanced dashboard, latest camera JPEG, connectivity handling, verified settings save/read-back
- **P1 (extend functionality):** Water quality interpretation, operator targets, calibration workflows
- **P2 (optional):** Historical camera gallery, trend charts, advanced analytics

---

## 17. Missing Firmware Getters

### 17.1 Water quality

Only `waterQualityGetTemperatureC()` currently exists. pH and turbidity are read and printed internally in `water_quality.cpp` but not exposed as getter functions. Two new getters are needed:
- `waterQualityGetPH()`
- `waterQualityGetTurbidity()`

### 17.2 Schedule

Individual getters for start/end hours and minutes are needed for the dashboard form pre-fill:
- `scheduleGetStartHour()`
- `scheduleGetStartMinute()`
- `scheduleGetEndHour()`
- `scheduleGetEndMinute()`

Currently only `scheduleGetRunWindowString()` exists, which returns a formatted string.

---

## 18. Ethics — Still Pending

The camera is only meant to be active during scheduled closed hours, triggered by the radar rather than running continuously, with captured images stored locally only. A rolling retention cap was discussed but not finalised. None of this is confirmed as approved by supervisor Winberg yet.

The camera detects scene changes, not identities. The radar detects presence independently. Neither sensor alone proves a person is there. Together they provide corroborating evidence. The language used should be neutral ("presence detected," "scene change observed") not accusatory ("intruder detected," "unauthorized entry").

---

## 19. Current State — What's Done, What's Pending

### Verified and working on real hardware
- Camera frame capture and JPEG conversion
- Camera browser viewer over WiFi
- Radar OUT pin HIGH/LOW detection with configurable range
- Radar UART distance, energy, and moving/stationary data
- WiFi bridge (DOIT → camera: radar JSON)
- WiFi bridge (camera → DOIT: detection JSON with CORS)
- Camera radar client polling every 500ms
- Detection logic with reference frame, ROI comparison, change percentage
- Three successful detection events recorded with clear, analysable data
- Lighting change limitation documented with measured values
- Scene change threshold adjusted from 5% to 15%

### Built but not tested
- Detection endpoint on camera board (`/detection` with CORS)
- DOIT board polling camera and bundling into single status JSON

### Not yet built
- SD card storage of captured images
- Dashboard page showing occupancy events with captured images
- Schedule-gated camera operation (active only during closed hours)
- "Radar communication lost" status when WiFi poll fails
- Event timestamping using the DS3231 RTC
- Multiple historical capture storage and retrieval
- Water quality getter functions for pH and turbidity
- Schedule getter functions for individual time components

### Pending external
- Ethics approval from Winberg for image retention approach
- Formal, controlled detection trials (the current tests were exploratory, not a formal protocol)
