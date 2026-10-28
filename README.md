# Pool Plant Monitor

A condition-monitoring and maintenance-advisory system for swimming-pool plant rooms, built on two ESP32 boards. The DOIT ESP32 DevKit V1 runs vibration analysis (MPU6050), water-quality sensing (pH, temperature, turbidity), radar-based presence detection (LD2410) and a local web dashboard. An ESP32-S3 camera board runs a frame-difference occupancy-detection algorithm that compares grayscale frames against a stored reference, corroborated by the radar data received from the DOIT board over Wi-Fi. No images are stored or transmitted; only scalar change-percentage values leave the camera.

## Module status

| Module | Status |
|---|---|
| Vibration (MPU6050 RMS, persistent-warning logic) | VERIFIED ON HARDWARE |
| Radar presence (LD2410 OUT pin + UART) | VERIFIED ON HARDWARE |
| Radar HTTP server (`/radar` JSON endpoint) | VERIFIED ON HARDWARE |
| Dashboard (web UI, `/api/status`) | VERIFIED ON HARDWARE |
| Shared network module (single AP, single WebServer) | VERIFIED ON HARDWARE |
| Camera occupancy detection (frame-difference) | VERIFIED ON HARDWARE |
| Occupancy heartbeat (camera → DOIT `/api/occupancy/heartbeat`) | VERIFIED ON HARDWARE |
| RTC schedule (DS3231) | IMPLEMENTED BUT UNTESTED |
| Water quality (pH, temperature, turbidity) | IMPLEMENTED BUT UNTESTED |
| Cloud connectivity | NOT IMPLEMENTED |

## Hardware and pins

### DOIT ESP32 DevKit V1

| Peripheral | Pin(s) | Notes |
|---|---|---|
| MPU6050 (vibration) | SDA/SCL (default I2C), AD0 → 3.3 V | Address 0x69 (DS3231 occupies 0x68) |
| DS3231 (RTC) | SDA/SCL (shared I2C bus) | Address 0x68 |
| LD2410 (radar) | OUT → GPIO 5, TX → GPIO 16, RX → GPIO 17 | UART2 at 256000 baud |
| Turbidity sensor | GPIO 35 (ADC) | |
| DS18B20 (temperature) | GPIO 4 (OneWire) | |
| pH sensor | GPIO 34 (ADC) | |

### ESP32-S3 camera board

Pin mapping is in `OccupancyModule/src/occupancy.cpp`. XCLK on GPIO 38, SIOD on GPIO 8, SIOC on GPIO 7. Frame format: GRAYSCALE 640x480, fb_count=2 in PSRAM, xclk=20 MHz.

## How to build

Requires [PlatformIO](https://platformio.org/).

```
# DOIT board (vibration, radar, dashboard)
cd PoolPlantMonitor
pio run -e esp32doit-devkit-v1

# Camera board (occupancy detection)
cd OccupancyModule
pio run -e esp32-s3-cam
```

Upload ports default to COM5 (DOIT) and COM6 (camera) as set in each `platformio.ini`.

## Network

The DOIT board creates a Wi-Fi access point:

- **SSID**: `PoolPlantRadar`
- **Password**: `poolplant123`
- **AP IP**: `192.168.4.1`
- **Dashboard**: `http://192.168.4.1/`
- **Radar JSON**: `http://192.168.4.1/radar`

The camera board connects as a station client (DHCP). Its viewer page is served on the IP it receives (typically `192.168.4.2`).

> **These are bench-test credentials and must be changed before any deployment.**

## Repository layout

```
├── src/                  DOIT board firmware
├── include/              DOIT board headers
├── platformio.ini        DOIT board PlatformIO config
├── logs/                 Serial capture logs from bench tests
├── OccupancyModule/
│   ├── src/              Camera board firmware
│   ├── include/          Camera board headers
│   ├── platformio.ini    Camera board PlatformIO config
│   ├── tools/            Trial runner and analysis scripts
│   ├── evidence/         Raw trial logs and metadata
│   │   └── occupancy/    Per-scenario folders (OCC-0 through OCC-5b)
│   ├── results_final/    Analysis output (final thresholds)
│   ├── results_thr15/    Analysis output (15% threshold)
│   └── results_thr3/     Analysis output (3% threshold)
├── docs/                 Development logs and design records
│   └── prototypes/       Laptop-only prototype code (not in firmware)
└── .gitignore
```

## Running trials and analysis

### run_trial.py

Runs one scripted occupancy trial. Close any serial monitor first.

```
python OccupancyModule/tools/run_trial.py OCC-2 --port COM6
```

The script opens the serial port, resets the board, waits for the reference-frame capture, then prompts you to move at scripted times. It writes `runN.log` and `runN_meta.json` into `evidence/occupancy/<scenario>/`.

Scenarios: OCC-0, OCC-1, OCC-2, OCC-3, OCC-5-L2, OCC-5-L3, OCC-5b. Options: `--port`, `--out`, `--note`, `--yes`, `--dry-run [--speed N]`.

### analyse_occupancy.py

Reads the saved logs and produces CSV tables and figures.

```
python OccupancyModule/tools/analyse_occupancy.py OccupancyModule/evidence/occupancy --out results --threshold 3 --lag 1.5
python OccupancyModule/tools/analyse_occupancy.py OccupancyModule/evidence/occupancy --suggest-threshold OCC-3
python OccupancyModule/tools/analyse_occupancy.py OccupancyModule/evidence/occupancy --estimate-lag
```

### Evidence folder layout

Each scenario folder (e.g. `OCC-2/`) contains:
- `runN.log` — timestamped serial output with `#SCRIPT` markers
- `runN_meta.json` — commit hashes, times, config, Python version

Top-level evidence files: `ground_truth.csv`, `lag_estimate.txt`, `threshold_freeze.txt`.

## Run register

Built from the `runN_meta.json` files in `OccupancyModule/evidence/occupancy/`.

| Scenario | Run | Started | Light | Cam commit | DOIT commit | Aborted |
|---|---|---|---|---|---|---|
| OCC-0 | 1 | 2026-10-20 01:58 | normal | 4fffb08 | 9a6ae31 | no |
| OCC-1 | 1 | 2026-10-20 02:28 | normal | 231f413 | 9a6ae31 | no |
| OCC-2 | 1 | 2026-10-20 02:13 | normal | 231f413 | 9a6ae31 | no |
| OCC-2 | 2 | 2026-10-20 03:09 | normal | 2cbc219 | 9a6ae31 | no |
| OCC-3 | 1 | 2026-10-20 02:00 | normal | 4fffb08 | 9a6ae31 | no |
| OCC-3 | 2 | 2026-10-20 02:46 | normal | 231f413 | 9a6ae31 | no |
| OCC-5-L2 | 1 | 2026-10-20 02:17 | dim | 231f413 | 9a6ae31 | no |
| OCC-5-L3 | 1 | 2026-10-20 02:21 | dark | 231f413 | 9a6ae31 | no |
| OCC-5-L3 | 2 | 2026-10-20 03:06 | dark | 2cbc219 | 9a6ae31 | no |
| OCC-5b | 1 | 2026-10-20 02:25 | normal | 231f413 | 9a6ae31 | no |
| OCC-5b | 2 | 2026-10-20 03:12 | normal | 2cbc219 | 9a6ae31 | no |

## Commits and tags

| Reference | Hash | Description |
|---|---|---|
| Tag `occupancy-evidence` | 9a6ae31 | DOIT board bench configuration (network-bench) |
| Tag `occupancy-evidence-camera` | bb5d33c | Camera board with evidence, tools and results |

The `main` branch is based on commit `9a6ae31`, which is the bench-test configuration. In `src/main.cpp` at this commit, `VIBRATION_CONNECTED`, `RTC_CONNECTED` and `WATER_QUALITY_CONNECTED` are set to `false`; `RADAR_CONNECTED`, `RADAR_SERVER_CONNECTED` and `DASHBOARD_CONNECTED` are set to `true`. To enable other modules, set the corresponding flag to `true` and rebuild.

## Ethics

The occupancy-detection algorithm compares grayscale pixel intensities against a stored reference frame and reports only a scalar change percentage. No images are stored, transmitted or made available outside the camera board's local processing loop. The camera viewer page (`/capture`) is served only on the local access point for development purposes. This approach was chosen specifically to avoid capturing identifiable information. The method was reviewed and approved by the project supervisor.

## Limitations

- This is a prototype built and tested by one operator in one room (a home office used as a proxy for a pool plant room).
- Occupancy detection has been tested under normal, dim and dark lighting with a fixed camera position. Performance in other environments is not verified.
- Vibration thresholds were calibrated on a bench test rig, not on a real pool pump.
- Water-quality sensors are implemented in firmware but have not been tested with real hardware.
- The system has no cloud connectivity, no authentication and no encryption on the local AP.
- All timestamps in the evidence logs are from the host computer clock, not sensor acquisition time.

---

Course project EEE4022S, University of Cape Town, 2026.
