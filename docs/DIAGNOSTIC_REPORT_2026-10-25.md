# Diagnostic report, 25 October 2026

Branch status-state-fields. Compared 8b62290 (24 Oct, rtc ready true) with 3ed1114 (25 Oct, rtc not_found).

## Part A: clock

### A1. What changed between 8b62290 and 3ed1114
`git diff --stat`: 5 files. include/radar.h (+5), include/vibration.h (+6), src/dashboard.cpp (+1405/-151, rewritten page and JSON), src/radar.cpp (25), src/vibration.cpp (+6).

Files NOT changed: src/schedule.cpp, include/schedule.h, src/main.cpp, platformio.ini, include/module_flags.h, network code.

Changes touching the clock or anything before it:
- src/schedule.cpp: unchanged. Setup is `rtc.begin()`; `rtcReady` = its return value. `lostPower()` is checked only after `rtcReady = true` and only calls `adjust()`; it cannot clear the flag.
- src/main.cpp: unchanged. Order is networkSetup, vibrationSetup, scheduleSetup, radarSetup, radarServerSetup, waterQualitySetup, dashboardSetup. `Wire.begin()` is at vibration.cpp:384 (inside vibrationSetup), unchanged.
- src/vibration.cpp: only 5 getter functions added after line 876 (`vibrationGetLastGoodAgeMs`, `vibrationPumpWindowSet`, start/end minute, window state). Nothing in setup, nothing on I2C.
- src/radar.cpp: adds `lastFrameMs` and getters. UART only, no I2C.
- src/dashboard.cpp: the `rtc` block of the status JSON changed (see below). No I2C or init code.

### How the rtc block is computed (dashboard.cpp, `rtcJson()`)
- `CFG_RTC_CONNECTED` false: state "disabled", reason "disabled".
- else `scheduleIsReady()` true: state "live", reason null.
- else: state "unavailable", reason "not_found" (a hard-coded string).
- `ready` = `scheduleIsReady()` = `RTC_INSTALLED && rtcReady`; `rtcReady` is set only in `scheduleSetup()` from `rtc.begin()`.

### Can not_found / ready false mean something other than "chip did not answer"?
- `rtc.begin()` (RTClib 2.1.4) calls Adafruit_I2CDevice::begin(), which does `Wire.begin()` then `detected()`, an address-only probe of 0x68. So ready false means the probe got no ACK at boot.
- `not_found` is a label for any `!scheduleIsReady()`. It does not distinguish causes. Within this code the only way to get there with the flag on is a failed `rtc.begin()`.
- Power-lost flag: cannot cause it (checked after ready is set).
- Init order: unchanged from 8b62290, where the clock was found. Vibration's `Wire.begin()` runs first in both.
- Retry: there is no retry. One attempt at boot, never repeated. A single failed probe (bus not settled, module powered late, SDA/SCL glitch at reset) stays failed until the next reset.
- Bus left in a bad state by another module: no code path found that does this. Only the MPU6050 code uses Wire besides the RTC, and it is unchanged. Not excludable from source alone.
- The RTC flag, environment and library versions are identical in both commits, so the source gives no reason for the change in behaviour.

Conclusion: no software cause found. The difference between the 24 and 25 Oct results is not explained by any code change. Likely causes are hardware or timing (contact, power-up order, bus state at that boot), UNKNOWN until a boot log is read. The serial line "DS3231 not found. Check wiring (SDA/SCL) and power." would confirm a failed probe. I did not open any serial port.

### A2. Fix
None. No small, certain, local bug found. No clock code changed.

## Part B: event picture

- B1: `occupancyJson()` in src/dashboard.cpp now also returns `cameraIp` (stored heartbeat sender address, or null if none) and `captureUrl` (`http://<cameraIp>/capture?e=<eventCount>` only when state is live AND eventActive is true, else null). Nothing else in the block changed. The board stores and fetches no picture.
- B2: embedded page replaced with C:\Users\nyele\Downloads\pool_plant_dashboard_final.html, byte for byte (line endings normalised to LF, trailing newline trimmed before the raw-string terminator). The file contains no `)HTML"` sequence. Route and `send_P` unchanged.
- B3: esp32doit-all built. RAM 18.1% (59340 bytes). Flash (program size) 79.7% (1045157 of 1310720 bytes), below the 85% limit. Commit 83c4c49 "dash: event picture url and page". No attribution lines. Not pushed.
- B4: uploaded to COM5 with `-e esp32doit-all` only. Esptool: hash of data verified, hard reset. Not verified on the running board: nothing was read back from the board.
