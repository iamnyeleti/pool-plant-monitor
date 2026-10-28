# Pool Plant Monitor — Main Controller Firmware

Final-year Mechatronics Engineering project (EEE4022S, UCT).  
**Student:** Nyeleti Mushwana (MSHNYE002) · **Supervisor:** Dr Simon Winberg

**Board:** DOIT ESP32 DevKit V1 · **Port:** COM5 · **Branch:** `integration`

---

## What this firmware does

Runs on the main DOIT ESP32 board. Creates a Wi-Fi access point (`PoolPlantRadar`, 192.168.4.1), hosts a single-page dashboard on port 80, and coordinates all sensing modules:

- **Vibration** — MPU-6050 accelerometer, cooperative loop, threshold-based anomaly detection.
- **Water quality** — pH (DFRobot SEN0161-V2), temperature (DS18B20), turbidity (DFRobot analog). Calibrated via serial commands.
- **Radar** — HLK-LD2410C via UART2 and OUT pin. Serves `GET /radar` for the camera node.
- **Occupancy** — receives heartbeat pushes from the camera node at `GET /api/occupancy/heartbeat`.
- **Schedule / RTC** — DS3231 real-time clock. Pump run window stored in flash, used by the vibration module.

---

## Repository structure

```
src/              Firmware source files (C++, Arduino framework)
include/          Header files
lib/              Placeholder — third-party libraries managed by PlatformIO
test/             Placeholder for unit tests

evidence/
  vibration/      Hardware test logs and results (VP01 through VP07)
  water-quality/  Hardware test logs and results (T1 through T4)
    raw/          Raw /api/status JSON captures from test sessions
    T4-upload/    Full T4 session: logs, status captures, commit snapshot
  integration/    All-modules integration test (VP07-ALL)
    results/      Integration analysis CSV and summary

docs/             Diagnostic reports, developer notes
tools/            Python analysis scripts (run_vib.py, status_log.py, analyse_integration.py)
logs/             Serial monitor logs from all development sessions

ch5_figures_pack/ Chapter 5 thesis figures: scripts, data and rendered PDFs/PNGs
wokwi.toml        Wokwi simulator configuration
diagram.json      Wokwi circuit diagram

archive/          Development scratch files kept for reference
                  (old patch diffs, commit-hash notes, T4 zip)
```

---

## Building

Open this folder in VS Code with the PlatformIO extension installed. PlatformIO resolves all library dependencies from `platformio.ini` automatically.

Select one named environment. **Never use the "All" task** — it uploads every environment in sequence.

| Environment | Modules enabled | Use for |
|---|---|---|
| `esp32doit-all` | Everything | Full dashboard with all sensors |
| `esp32doit-bench` | Vibration + RTC | Vibration-only testing |
| `esp32doit-calibrate` | Vibration + RTC (calibration mode) | Baseline measurement |
| `esp32doit-bench-fault` | Vibration + RTC + fault injection | Fault detection testing |
| `esp32doit-devkit-v1` | Water quality + dashboard | Water-only testing |

Build only (no upload): `pio run -e esp32doit-all`  
Build and upload: `pio run -e esp32doit-all --target upload`

---

## API endpoints

| Endpoint | Method | Description |
|---|---|---|
| `/` | GET | Dashboard page |
| `/api/status` | GET | JSON: vibration, rtc, radar, occupancy, schedule, water |
| `/radar` | GET | Radar JSON (used by camera node — do not change) |
| `/api/occupancy/heartbeat` | GET | Receives heartbeat from camera node — do not change |
| `/api/water/config` | GET | Current water preference settings |
| `/api/water/config` | POST | Update temperature range and preferred pH |

---

## Water quality calibration (serial, after every boot)

Connect to COM5 at 115200 baud.

| Key | Action |
|---|---|
| `7` | Calibrate pH with pH 7.0 buffer |
| `4` | Calibrate pH with pH 4.0 buffer |
| `r` | Reset pH calibration |
| `b` | Set turbidity clear-water baseline |

Pump run window (only when `WATER_QUALITY_CONNECTED` is 0):

```
PUMP HH:MM HH:MM    set start and end time
PUMP CLEAR          clear the window
PUMP?               print current window
```

---

## Known prototype limits

- Wi-Fi credentials are hard-coded. No authentication on any endpoint.
- pH calibration and turbidity baseline are lost on every restart.
- The temperature probe search runs once at boot and is not retried.
- The clock may drift; the pump schedule is compared against it without correction.
- Full integration with all modules running simultaneously has been tested in one session only (VP07-ALL, 24 October 2026).
