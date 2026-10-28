# Diagnostic report: esp32doit-all, water and occupancy modules

Repo: PoolPlantMonitor, branch `integration`, HEAD 8b62290. Camera code: OccupancyModule (working tree has uncommitted timing changes). Written 2026-10-24 (file name dated 10-23 as requested).
Method: source reading, `git`, installed library and framework sources, and existing log files. No serial port opened, nothing uploaded, nothing pushed, no code changed.
Where a fact could not be found it says UNKNOWN. Paths are relative to PoolPlantMonitor unless they start with OccupancyModule.

## 0. Findings in short

| Symptom | Verdict | Evidence |
|---|---|---|
| temperature unavailable / not_found | DS18B20 did not answer the one-time bus search at boot. The firmware did not change; the same water code found the sensor on 10-21. Most likely wiring or power on G4. Not provable from software. | A1, A6 |
| pH calibration unavailable | Expected. Calibration is session-only by design and is never saved. It must be redone with serial keys `7` then `4` after every boot or upload. | A2 |
| turbidity NO_BASELINE | Expected. Same reason. Serial key `b` sets it each boot. | A2 |
| restoredFromFlash true | Correct. It means only the temperature range and preferred pH were restored. NVS is not being erased. | A2 |
| occupancy moduleOnline false | No heartbeat reached the main board. The VP07-ALL log has 0 `[Occ]` lines in 6 minutes. Cause on the camera side is UNKNOWN. The main-board route and server are fine. | A4 |
| Code bug that is small and certain | None found. No code was changed and no `fix-all-modules` branch was created. | A6 |

## A1. DS18B20 code and pin table

Pin and objects, [water_quality.cpp:27](../src/water_quality.cpp#L27), [:110-111](../src/water_quality.cpp#L110):

```cpp
static const int ONE_WIRE_PIN = 4;        // G4 on the board: DS18B20 data wire
static OneWire oneWire(ONE_WIRE_PIN);
static DallasTemperature tempSensor(&oneWire);
```

Scan logic, once, in `waterQualitySetup()` [water_quality.cpp:923-932](../src/water_quality.cpp#L923):

```cpp
tempSensor.begin();
tempFound = (tempSensor.getDeviceCount() > 0);
if (tempFound) {
  tempSensor.setResolution(TEMP_RESOLUTION_BITS);
  tempSensor.setWaitForConversion(false);
  Serial.println("DS18B20 temperature sensor found.");
} else {
  Serial.println("DS18B20 NOT found. Check the three wires and the pull-up resistor on G4.");
}
```

`DallasTemperature::begin()` (library 3.11.0, DallasTemperature.cpp:112-136) does `reset_search()` then `while (search(...))`. It runs once. There is no retry anywhere. `temperatureUpdate()` returns immediately when `!tempFound` ([water_quality.cpp:810](../src/water_quality.cpp#L810)), so a sensor that is missing at boot stays missing until the next reboot.

Where "not_found" is set: [water_quality.cpp:273](../src/water_quality.cpp#L273), in `computeTempState()`:
`if (!tempFound && simScen != 't') { *reason = "not_found"; return "unavailable"; }`
It is reported as `temperature.reason` by `jstate()` ([:1207](../src/water_quality.cpp#L1207)) and logged as `[WQ] temp: unavailable (not_found)`.

Evidence from logs (all in this repo):
- VP07-ALL (esp32doit-all, 2026-10-24 01:50): `DS18B20 NOT found...` then `[WQ] temp: unavailable (not_found)` (evidence/vibration/VP07-ALL/run1.log).
- 2026-10-21 18:41 to 21:41, ten logs (logs/device-monitor-261021-180153.log through -212416.log): `DS18B20 temperature sensor found.` and `[Temperature] 22.62 C`. These builds had AP, dashboard and water quality, and no RTC, vibration or radar lines.
- 2026-10-21 03:37 to 11:27, eight logs: `NOT found` (wiring state at that time: UNKNOWN).
- `ONE_WIRE_PIN = 4` has not changed since commit a74749d (10-19). `water_quality.cpp`, `dashboard.cpp` and `network.cpp` are identical between tag `wq-stage5` and HEAD (`git diff --stat wq-stage5 HEAD -- src include platformio.ini` lists only module_flags.h, schedule.h/.cpp, main.cpp, vibration.cpp, vib_history.h, platformio.ini, the vibration doc).

### Every pin used in esp32doit-all

| GPIO | Module | Use | Source |
|---|---|---|---|
| 1, 3 | USB serial (UART0) | Serial 115200 | main.cpp:19 |
| 4 | Water | DS18B20 OneWire data | water_quality.cpp:27 |
| 5 | Radar | LD2410 OUT, input | radar.cpp:5, :19 |
| 16 | Radar | Serial2 RX (radar TX) | radar.cpp:21 |
| 17 | Radar | Serial2 TX (radar RX) | radar.cpp:21 |
| 21 | Vibration + RTC | I2C SDA, default pin | vibration.cpp:384; variant doitESP32devkitV1 pins_arduino.h:13 |
| 22 | Vibration + RTC | I2C SCL, default pin | same, pins_arduino.h:14 |
| 34 | Water | pH signal, ADC1, input only | water_quality.cpp:28 |
| 35 | Water | Turbidity signal (via divider), ADC1, input only | water_quality.cpp:26 |
| none | Network | Wi-Fi AP (internal radio) | network.cpp:16-17 |
| none | Dashboard, schedule logic | no pins | |

Pins shared by two modules: none in the firmware. The only shared resource is the I2C bus (SDA 21, SCL 22), used by MPU6050 (0x69) and DS3231 (0x68), which is intended. No pin is a boot-strapping conflict at runtime: GPIO5 and GPIO4 are used as plain input and OneWire. Electrical sharing (3V3 and GND rails, pull-up on G4) is UNKNOWN from software.

## A2. Calibration and baseline storage

Preferences namespace `"wq"`, keys `tSet, tLow, tHigh, pSet, pPref` only ([water_quality.cpp:860-889](../src/water_quality.cpp#L860)). The code comment says it plainly at [:862](../src/water_quality.cpp#L862): "Calibration and the turbidity baseline are session-only and are not stored."

| Item | Stored in | Saved to flash? |
|---|---|---|
| pH 7 and pH 4 points, Tcal | RAM: `ph7Mv, ph4Mv, phTcal, ph7Set, ph4Set` (:236-242) | Never. No `prefs.put*` for them anywhere. |
| Turbidity baseline | RAM: `turbBaselineV, turbBaselineSet` (:228-229) | Never. |
| Temperature range, preferred pH | NVS namespace `wq` | Yes, only by a valid change through `/api/water/config` POST ([:1128-1135](../src/water_quality.cpp#L1128)). |

How they are set: serial keys read in `waterQualityUpdate()` ([:949-953](../src/water_quality.cpp#L949)) and `handleKey()` ([:735-752](../src/water_quality.cpp#L735)): `7` and `4` call `calCapture()`, `r` resets, `b` calls `baselineCapture()`. There is no HTTP route or dashboard button for them (routes: dashboard.cpp:305-309). Capture needs 30 valid one-second samples, spread within 5 mV (pH) or 0.05 V (turbidity), the pH mean inside the buffer window, and a slope between 150 and 190 mV/pH once both points exist ([:393-467](../src/water_quality.cpp#L393)).

Exact conditions:
- `calibration_unavailable` ([:499](../src/water_quality.cpp#L499)): pH channel is live, but `phValueNow()` returned false because `phCalLevel() != 2` ([:354](../src/water_quality.cpp#L354)), meaning not both `ph7Set` and `ph4Set`. JSON also shows `"calibration":"unavailable"` ([:1248](../src/water_quality.cpp#L1248)) and the page text "not calibrated since restart" (dashboard.cpp:137).
- `NO_BASELINE` ([:512](../src/water_quality.cpp#L512)): turbidity live, but `effBaselineSet()` false, meaning `turbBaselineSet` false and no `u` simulation.

Does anything erase or ignore them?
- Nothing erases NVS. platformio.ini has no `board_build.partitions`, no `upload_flags`, no `--erase`, no `extra_scripts`. The partition table is the framework default.csv (nvs at 0x9000, size 0x5000). Proof NVS survives uploads: VP07-ALL log shows `[CFG] restored temperature range 20.0 to 30.0 C` and `[CFG] restored preferred pH 7.50` after a power-on reset and a fresh upload (the build stamp is 01:37 and the run 01:50).
- Every reset or upload loses calibration and baseline, because they are RAM-only. The runner meta for VP07-ALL has `"cal_lines": []` and `"baseline_from_calibration": null`, so no calibration was done in that run.
- The code never saves them. That is a design property, not a bug. Persisting them would be a design change, so none was made.

## A3. Blocking calls in the esp32doit-all loop

Order in `loop()` ([main.cpp:36-45](../src/main.cpp#L36)):
1. `vibrationUpdate()` :37
2. `scheduleUpdate()` :38
3. `radarUpdate()` :39
4. `radarServerUpdate()` :40 (empty, radar_server.cpp:34)
5. `waterQualityUpdate()` :41
6. `dashboardUpdate()` :42 (empty, dashboard.cpp:314)
7. `networkUpdate()` :43, which is `server.handleClient()` at [network.cpp:40](../src/network.cpp#L40). This is the only place web requests are served: once per loop, last.
8. `delay(20)` :44

Measured (not guessed): VP07-ALL `[TIMING]` per minute: mean 24.6 to 25.3 ms, min 23.84 ms, max 37 to 94 ms, 0 to 5 samples over 50 ms per minute, read_errors 0. So the loop does about 4 to 5 ms of work besides the 20 ms delay, with occasional spikes. What causes the spikes is UNKNOWN.

Calls that can take more than 5 ms:

| file:line | Call | Notes |
|---|---|---|
| main.cpp:44 | `delay(20)` | every iteration, 20 ms |
| water_quality.cpp:824 | `getTempCByIndex(0)` | Not running now (`tempFound` false). When the sensor is present: bit-banged OneWire, interrupts masked per bit. My estimate from OneWire slot times is about 10 to 12 ms once a second. Estimate, not measured. |
| water_quality.cpp:816 | `requestTemperatures()` | estimate about 2 ms, once a second, when present |
| vibration.cpp:570 | `mpuReadAccel` (I2C) | Normally about 1 to 2 ms (estimate). Wire default timeout is 50 ms (Wire.cpp:48), so a bus fault can cost up to 50 ms per transaction. |
| vibration.cpp:580 | `mpuInit()` | only while `!sensorOk`, every 5 s, 3 transactions |
| schedule.cpp:40 | `rtc.now()` | every loop, I2C 7-byte read, same 50 ms worst case |
| schedule.cpp:66 | `rtc.now()` via `scheduleGetTimeString()` | called from `/api/status` (dashboard.cpp:184), heartbeat (dashboard.cpp:211) and vibration event logging (vibration.cpp:156) |
| Serial prints | many | UART TX uses only the 128-byte FIFO (HardwareSerial.cpp:549, `_txBufferSize(0)` :73). A `print` blocks when the FIFO is full, about 11.5 bytes per ms at 115200. Lines per loop: vibration.cpp:811-848 (every 250 ms), :725 (1 s), radar.cpp:43-70 (500 ms), water_quality.cpp:989-1037 (1 s), schedule.cpp:45 (1 s). Whether they overflow the FIFO together is UNKNOWN. |
| water_quality.cpp:1128-1135 | NVS writes (`prefs.put*`) | only on a config POST. Flash commit time UNKNOWN. |
| dashboard.cpp:168 | `send_P` of the page HTML | in `handleClient`, time UNKNOWN |
| dashboard.cpp:171-195 | `handleStatus` JSON build and send | in `handleClient`, time UNKNOWN |
| radar.cpp:37 | `radar.read()` | not blocking: drains the UART with `while (available())` (ld2410.cpp:125-131) |
| water_quality.cpp:957, :960 | `analogReadMilliVolts` x2 | tens to hundreds of microseconds (estimate) |
| water_quality.cpp:949 | serial `while` | bounded by bytes available |

Setup only (not in loop): `while (!Serial) delay(10)` main.cpp:20 (does not hang: `operator bool` is `uartIsDriverInstalled`, HardwareSerial.cpp:480); `delay(100)` radar.cpp:22; `radar.begin(Serial2)` waits up to 1000 ms because `waitForRadar` defaults to true (ld2410.h:39, ld2410.cpp:73-80); `WiFi.softAP` network.cpp:17; `tempSensor.begin()` water_quality.cpp:923.

## A4. Camera reconnect and the occupancy online condition

Camera (OccupancyModule/src/viewer.cpp):
- Boot only: `WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.begin(...)` lines 70-72, then a 20 s wait loop at lines 76-79: `while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) { delay(500); ... }`.
- If not connected after 20 s: lines 81-84 print `WiFi FAILED...` and `return`. `wifiOk` stays false. The web server (`server.on`, `server.begin`, lines 87-99) is never started, and `viewerUpdate()` only runs `server.handleClient()` when `wifiOk` (line 104). Nothing retries `viewerSetup()`. So a camera that boots before the main board's AP is up, or while it is restarting, has no viewer page until the camera itself is reset.
- If Wi-Fi drops after a successful start: `wifiOk` stays true. The Arduino core auto-reconnects: `_autoReconnect = true` by default (WiFiSTA.cpp:116) and the STA_DISCONNECTED handler calls `WiFi.disconnect(); WiFi.begin();` when reconnectable (WiFiGeneric.cpp:1084-1094). So the camera should rejoin an AP that restarts with the same SSID and password. I did not test this: UNKNOWN in practice.
- Heartbeat code does not look at `wifiOk`. `radarClientUpdate()` (radar_client.cpp:26-76) runs every 500 ms, polls `http://192.168.4.1/radar` with 1 s connect and 1 s read timeouts (lines 31-32), and sends the heartbeat only if that poll returned 200 and 2 s have passed (line 52). While the link is down each poll blocks the camera loop up to about 1 s; there is no give-up limit there.
- Camera boot order (OccupancyModule/src/main.cpp:11-30): about 1.5 s + 0.5 s + up to 20 s Wi-Fi + 15 s reference countdown.

Main board (src/dashboard.cpp):
- `moduleOnline` condition: `lastOccupancyHeartbeat != 0 && (millis() - lastOccupancyHeartbeat < 10000)` at [dashboard.cpp:172](../src/dashboard.cpp#L172) (JSON, line 187) and [:318](../src/dashboard.cpp#L318). So online needs a heartbeat within the last 10 s.
- Route registered: [dashboard.cpp:309](../src/dashboard.cpp#L309) `networkServer().on("/api/occupancy/heartbeat", HTTP_GET, handleOccupancyHeartbeat);` inside `dashboardSetup()`, called from main.cpp:31 because `CFG_DASHBOARD_CONNECTED=1` in esp32doit-all. `/radar` is registered at radar_server.cpp:30 (flag `CFG_RADAR_SERVER_CONNECTED=1`).
- Server started: `networkBegin()` main.cpp:33 calls `server.begin()` (network.cpp:35). The VP07-ALL log confirms `Dashboard routes registered.` and `Web server started on port 80.`
- Evidence of no heartbeat: VP07-ALL has 0 `[Occ]` lines in 6 minutes. Earlier logs with `[Occ] online` exist (logs/device-monitor-261019-210403.log, -261020-003513.log, roundB-doit-2026-10-19.log), so the path worked before.
- Camera state during VP07-ALL (powered, joined, which firmware): UNKNOWN. No camera log covers that time; OccupancyModule/logs holds only 2026-10-19 and 10-20 logs.

## A5. Differences between environments

All three share `platform = espressif32`, `board = esp32doit-devkit-v1`, `framework = arduino`, the same four `lib_deps`, `monitor_speed`, `monitor_filters`, `upload_port`, `monitor_port`. They inherit these from `esp32doit-devkit-v1` through `extends`.

| Item | esp32doit-devkit-v1 | esp32doit-bench | esp32doit-all |
|---|---|---|---|
| build_flags | none (module_flags.h defaults apply) | listed below | listed below |
| VIB_BASELINE | default 0.7854 (vibration.cpp:84) | 0.5356 | 0.5356 |
| VIB_STOPPED_STD | default 0.05 (vibration.cpp:106) | 0.102 | 0.102 |
| CFG_VIBRATION_CONNECTED | 0 (default) | 1 | 1 |
| CFG_RTC_CONNECTED | 0 (default) | 1 | 1 |
| CFG_RADAR_CONNECTED | 0 (default) | 0 | 1 |
| CFG_RADAR_SERVER_CONNECTED | 0 (default) | 0 | 1 |
| CFG_WATER_QUALITY_CONNECTED | 1 (default) | 0 | 1 |
| CFG_DASHBOARD_CONNECTED | 1 (default) | 0 | 1 |
| lib_deps | RTClib ^2.1.4, OneWire ^2.3.8, DallasTemperature ^3.11.0, ld2410 ^0.2.2 | same (inherited) | same (inherited) |
| partitions | none set, default.csv | same | same |
| board options | none set | same | same |
| upload_flags / erase | none | none | none |

Defaults are from [module_flags.h](../include/module_flags.h). `build_flags` in bench and all replace rather than extend the parent's (the parent has none). The water and dashboard code run in `esp32doit-devkit-v1` (defaults) and in `esp32doit-all`. Differences between them that touch the water module: all adds `Wire.begin()` on 21/22, the RTC, the MPU6050 and the radar (pins 5, 16, 17). None uses GPIO4, 34 or 35.

## A6. Ranked causes

Temperature `not_found`:
1. DS18B20 not answering at boot on G4 (wiring, pull-up or power). Probable, not proven. Evidence: it was found for ten consecutive boots on 10-21 with identical water code (A1), `[WQ] temp` says `not_found` which is only set when the boot-time search finds zero devices ([water_quality.cpp:924](../src/water_quality.cpp#L924), :273), and the other module changes in esp32doit-all share no pin with G4 (A1 table). Whether a connection moved when the MPU6050, RTC and radar were wired is UNKNOWN. A check without code: confirm 3V3, GND and DATA on G4 and a 4.7 k pull-up between DATA and 3V3, then reboot.
2. Detection runs only once at boot ([water_quality.cpp:923-924](../src/water_quality.cpp#L923), :810), so any momentary failure at power-up (for example a supply dip when the Wi-Fi AP starts, which comes before water setup in main.cpp:24 and :30) becomes permanent until reboot. This is a design weakness and an amplifier, not proven as the cause. Adding a periodic re-scan would be a design change, so it was not done.
3. Pin conflict: ruled out in software (table in A1).
4. Partition, flags, erase, upload: ruled out for the temperature (A2, A5).

Calibration unavailable and NO_BASELINE: one cause, with certainty: the values are RAM-only by design ([water_quality.cpp:862](../src/water_quality.cpp#L862)) and no one pressed `7`, `4`, `b` after the last boot. Not a bug.

Occupancy `moduleOnline` false:
1. Camera not sending heartbeats. Possible reasons from the code: camera not powered; camera booted when the AP was not up and gave up after 20 s (viewer.cpp:76-84); radar poll to `/radar` not returning 200, which also suppresses the heartbeat (radar_client.cpp:52). Which one applies: UNKNOWN. Needs the camera's serial output for the same time.
2. Main-board route or server not running: ruled out by the VP07-ALL boot lines.

Code change: none. Neither the temperature nor the occupancy symptom has a small, certain, local software bug behind it, so no branch `fix-all-modules` was created, no build was run and nothing was uploaded.

## B1. Bus and UART constants

- `Wire.begin();` with no arguments: [vibration.cpp:384](../src/vibration.cpp#L384). Default pins SDA 21, SCL 22 (framework variant doitESP32devkitV1, pins_arduino.h:13-14). Nothing else calls `Wire.begin` with pins; `rtc.begin()` (schedule.cpp:19) takes the default `Wire`.
- Radar: `Serial2.begin(256000, SERIAL_8N1, 16, 17);` [radar.cpp:21](../src/radar.cpp#L21), RX = GPIO16, TX = GPIO17. OUT pin GPIO5 (radar.cpp:5).
- MPU6050: `static const uint8_t MPU_ADDR = 0x69;` [vibration.cpp:12](../src/vibration.cpp#L12) (AD0 tied to 3.3 V).
- DS3231: no constant in project code. The library defines `#define DS3231_ADDRESS 0x68` in .pio/libdeps/esp32doit-all/RTClib/src/RTC_DS3231.cpp:3. The vibration.cpp comment at lines 10-11 says the DS3231 is at 0x68.

## B2. WARNING logic (vibration.cpp)

- Elevated sample: `isElevatedNow = (rms > warningThreshold())`, i.e. RMS above baseline x 1.5 ([:644-646](../src/vibration.cpp#L644); multiplier :90; baseline 0.5356 in this build, so about 0.8034).
- History size: `ELEVATED_HISTORY_SIZE = WARNING_DURATION_MS / ASSUMED_LOOP_INTERVAL_MS` = 120000 / 20 = 6000 samples ([:100-101](../src/vibration.cpp#L100), :122-125). The boot line prints `window=6000`.
- Fraction: `WARNING_FRACTION_THRESHOLD = 0.5` ([:103](../src/vibration.cpp#L103)); `warningCondition = (elevatedCount / 6000.0) >= 0.5` ([:676-682](../src/vibration.cpp#L676)).
- Must be full: yes. `if (elevatedHistoryFull)` ([:674](../src/vibration.cpp#L674)); the flag is set when the index first wraps ([:663-669](../src/vibration.cpp#L663)). `resetSampleHistory()` clears it after a sensor outage (:373-380).
- The history counts loop samples, not time. The assumed 20 ms interval differs from the measured loop of about 25 ms (A3), so 6000 samples cover about 150 s in esp32doit-all, not 120 s. Derived from the numbers above.
- Priority in the status string ([:695-720](../src/vibration.cpp#L695)): OFF SCHEDULE, then STOPPED, then WARNING, then CAUTION, then OK.

## B3. Images kept or sent (detection.cpp)

- Kept: only the reference frame, one buffer of 640x480 bytes from `ps_malloc` ([:32](../src/detection.cpp#L32), :58), filled by `memcpy` at :81 in `detectionCaptureReference()`.
- The live frame is used in place by `compareFrameToReference()` and returned to the driver at :169 (`esp_camera_fb_return`). It is not copied or stored.
- detection.cpp sends nothing anywhere and has no accessor that returns the reference frame. It only exposes numbers (event state, percent, count, brightness).
- Outside detection.cpp: viewer.cpp `/capture` (lines 39-65) converts a fresh live frame to JPEG and returns it to any client on the AP; it is not stored. The heartbeat (radar_client.cpp:55-58) carries only event count, active flag and change percent.

## B4. PUMP command validation (vibration.cpp:272-303)

Input is trimmed and uppercased. Then:
- `PUMP?` prints the window.
- `PUMP` alone prints usage.
- `PUMP CLEAR` clears and saves.
- `PUMP ` followed by text: `sscanf(s.c_str() + 5, "%d:%d %d:%d", ...) == 4`, hours 0 to 23, minutes 0 to 59 for both, and `(h1*60+m1) != (h2*60+m2)`, so start equal to end is rejected ([:288-291](../src/vibration.cpp#L288)). Otherwise it prints usage. A window with start later than end is accepted and treated as crossing midnight (:336-337).
- Stored in NVS namespace `pump` (:242-261). On load, values >= 1440 or start == end clear the window (:250).
- `sscanf` ignores anything after the fourth number (for example `PUMP 08:00 20:00abc`). That follows from `sscanf` behavior and was not tested on the device.
- Important for esp32doit-all: the serial reader is compiled out when the water module is enabled (`#if !CFG_WATER_QUALITY_CONNECTED`, [:307-321](../src/vibration.cpp#L307)), so PUMP commands cannot be entered in esp32doit-all. There is no dashboard route for them either (routes at dashboard.cpp:305-309). Only a window already saved in flash is used (`pumpLoad()` at :388).

## B5. Vibration status string in /api/status

Yes. `handleStatus()` copies `vibrationGetStatus()` straight into `vibration.status` ([dashboard.cpp:176](../src/dashboard.cpp#L176)) with no filtering. `currentStatus` takes the values "OK", "SENSOR ERROR" (vibration.cpp:396, :598), "OFF SCHEDULE" (:697), "STOPPED" (:702), "WARNING" (:707), "CAUTION" (:713). How the dashboard page renders these strings was not checked (the vibration part of the HTML, dashboard.cpp lines 1-124, was not read): UNKNOWN.

## B6. platformio.ini and versions

```ini
; PlatformIO Project Configuration File
;
;   Build options: build flags, source filter
;   Upload options: custom upload port, speed and extra flags
;   Library options: dependencies, extra library storages
;   Advanced options: extra scripting
;
; Please visit documentation for the other options and examples
; https://docs.platformio.org/page/projectconf.html

[env:esp32doit-devkit-v1]
platform = espressif32
board = esp32doit-devkit-v1
framework = arduino
lib_deps = 
	adafruit/RTClib@^2.1.4
	paulstoffregen/OneWire@^2.3.8
	milesburton/DallasTemperature@^3.11.0
	ncmreynolds/ld2410@^0.2.2
monitor_speed = 115200
monitor_filters = default, log2file
upload_port = COM5
monitor_port = COM5

[env:esp32doit-calibrate]
extends = env:esp32doit-devkit-v1
build_flags =
	-DVIB_CALIBRATION_MODE=1
	-DCFG_VIBRATION_CONNECTED=1
	-DCFG_RTC_CONNECTED=1
	-DCFG_RADAR_CONNECTED=0
	-DCFG_RADAR_SERVER_CONNECTED=0
	-DCFG_WATER_QUALITY_CONNECTED=0
	-DCFG_DASHBOARD_CONNECTED=0

[env:esp32doit-bench]
extends = env:esp32doit-devkit-v1
; VIB_BASELINE below is a PLACEHOLDER (same as the committed default); set it from a calibration run.
build_flags =
	-DVIB_BASELINE=0.5356
	-DVIB_STOPPED_STD=0.102
	-DCFG_VIBRATION_CONNECTED=1
	-DCFG_RTC_CONNECTED=1
	-DCFG_RADAR_CONNECTED=0
	-DCFG_RADAR_SERVER_CONNECTED=0
	-DCFG_WATER_QUALITY_CONNECTED=0
	-DCFG_DASHBOARD_CONNECTED=0

[env:esp32doit-all]
extends = env:esp32doit-devkit-v1
build_flags =
	-DVIB_BASELINE=0.5356
	-DVIB_STOPPED_STD=0.102
	-DCFG_VIBRATION_CONNECTED=1
	-DCFG_RTC_CONNECTED=1
	-DCFG_RADAR_CONNECTED=1
	-DCFG_RADAR_SERVER_CONNECTED=1
	-DCFG_WATER_QUALITY_CONNECTED=1
	-DCFG_DASHBOARD_CONNECTED=1

[env:esp32doit-bench-fault]
extends = env:esp32doit-bench
build_flags =
	${env:esp32doit-bench.build_flags}
	-DVIB_FAULT_AT_S=90
	-DVIB_FAULT_MS=5000
```

Installed versions (from `pio.exe --version`, `pio pkg list -e esp32doit-all`, and package.json files under C:\Users\nyele\.platformio):

| Component | Version |
|---|---|
| PlatformIO Core | 6.2.0 (`pio` is not on PATH; used `~\.platformio\penv\Scripts\pio.exe`) |
| Platform espressif32 | 7.1.2 |
| framework-arduinoespressif32 | 4.20017.260907+sha.dcc1105b (PlatformIO numbering; the Arduino core release this maps to was not read from the package: UNKNOWN) |
| toolchain-xtensa-esp32 | 8.4.0+2021r2-patch5 |
| tool-esptoolpy | 2.41100.260830 |
| RTClib | 2.1.4 |
| Adafruit BusIO (dependency) | 1.17.4 |
| OneWire | 2.3.8 |
| DallasTemperature | 3.11.0 |
| ld2410 | 0.2.2 |
| ArduinoJson (OccupancyModule, `.pio/libdeps`) | 7.4.3 |

The camera project is a separate PlatformIO project: env `esp32-s3-cam`, esp32-s3-devkitc-1, QIO/OPI PSRAM, default_16MB.csv, COM6, lib `bblanchon/ArduinoJson@^7.4.3` (OccupancyModule/platformio.ini).

## B7. README and remotes

- PoolPlantMonitor/README.md exists and is tracked in the PoolPlantMonitor repo. Its content was not read for this report.
- `git remote -v` in PoolPlantMonitor (branch integration):
  `origin https://github.com/iamnyeleti/PoolPlantMonitor-.git (fetch)` and `(push)`.
- `git remote -v` in `C:\Users\nyele\FINAL YEAR PROJ` (the repo that contains OccupancyModule, branch network-fix): no remotes configured (empty output).
- OccupancyModule has no README (only the PlatformIO stub OccupancyModule/include/README), and the outer repo has no README at its root.

## Working tree

No source file was modified and no commit was made. The only new file is this report. PoolPlantMonitor shows untracked evidence and log files that were already there (T4_upload, status_*.jsonl, logs, evidence/vibration/VP07-ALL).
