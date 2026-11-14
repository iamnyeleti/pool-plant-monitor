# Pool Plant Monitor

**Integrated vibration, water-quality and access-zone sensing: a low-cost, locally hosted bench proof of concept.**

Final-year project (EEE4022S), Department of Electrical Engineering, University of Cape Town, 2026.
Author: Nyeleti Chantel Mushwana. Supervisor: Dr Simon Winberg.

Two ESP32 boards bring three plant-room checks to one local web page, with no internet connection, no plant controller and no actuation:

- **Motor condition:** pump-motor vibration against a baseline recorded on site, plus schedule-aware stoppage detection (MPU6050).
- **Water condition:** temperature against an operator-set range, pH against fixed limits (7.2 to 7.8) and clarity relative to a clear-water reference (DS18B20, DFRobot pH and turbidity sensors).
- **Access-zone activity:** 24 GHz radar presence (LD2410C) that gates a camera measuring grayscale scene change, without identifying anyone.

The **DOIT ESP32 DevKit V1** reads the motor, water and radar sensors and serves the dashboard. A **Waveshare ESP32-S3-CAM** processes camera frames and reports to the main board over Wi-Fi.

## Interactive dashboard demo

The real dashboard is served by the ESP32 over its own Wi-Fi network, so it cannot be opened over the internet. A standalone copy that runs on **labelled simulated data** (not live measurements or experimental results) is here:

**https://iamnyeleti.github.io/pool-plant-monitor/dashboard-demo.html**

Its source is `docs/dashboard-demo.html`. The same page switches to live data only when it is served by the board at `192.168.4.1`.

## What this is, and what it is not

It **is** a monitoring and decision-support prototype: an early prompt for an operator to inspect.

It is **not**:

- a fault diagnostic (it cannot say why vibration has risen or predict remaining life),
- a measurement instrument with established accuracy (no reference pH meter, thermometer or turbidimeter was available),
- proof that pool water is safe (disinfectant residual and microbiology are not measured),
- an installed or field-tested system (all testing was on a bench).

It never switches pumps, chemical dosing or access control, and it has no cloud connectivity.

## Status

Verdicts follow the final report (Table 5.1). **Verified** means the behaviour was observed on hardware against criteria fixed before the run. **Partly verified** means some of the evidence is simulated, incomplete or failed. Of 13 functional requirements, **2 are verified, 10 are partly verified and 1 is not implemented**. No verdict establishes measurement accuracy.

| Module | What was built | Evidence | Verdict |
|---|---|---|---|
| Vibration | Baseline-relative RMS (CAUTION, WARNING), spread-based stoppage, pump window, SENSOR ERROR, event records | Hardware VP01 to VP06; simulation V-SIM-01 to 03 | Partly verified (FR-V01 to V03) |
| Water quality | Temperature range, two-point pH calibration with fixed limits, relative clarity, validity checks | W-SW 45/45 known-input checks; W-CONF settings kept over 18/18 restarts; W-DISC 60/60 polls after the limits were revised; no completed pH calibration | FR-W01 and FR-S03 verified; FR-W02 and FR-W03 partly verified |
| Access zone | Radar presence, camera scene change, one event per radar episode | 11 scripted runs, one room, one participant | FR-O01 partly verified; **FR-O02 (closed-hours flag) not implemented** |
| Integration and interface | Single access point, dashboard, `/api/status`, settings form | VP07-ALL run 5: 169/169 requests answered with every module on | FR-S01, S02, S04, S05 partly verified |

## Key bench results

- **Stoppage:** a stopped motor still read about 0.32 m/s² RMS, so stoppage is judged by the spread of the readings instead. The STOPPED threshold (0.102 m/s²) was set on one run (VP02) and held unchanged on a second (VP04): the nearest stopped value was 2.3 times below it and the nearest running value 3.0 times above. STOPPED appeared 69.3 s after switch-off, after the 60 s confirmation hold, and only inside the pump window.
- **Disconnected water inputs** read about 0.142 V at the pins, not zero. After the validity limits were revised, each disconnection tested was reported unavailable in 60 of 60 polls. A probe left dry (3.57 V) still passes as live.
- **Camera threshold:** the first setting (3 %) failed its verification runs and was raised to 15 % afterwards. That revision is not independent validation. The dark-frame limit was raised from 20 to 70 so the camera refuses to compare instead of logging 0.0 %.
- **Radar:** presence in all 54 occupied segments and in none of 469 empty-room samples.
- **Drift:** the running vibration level moved between 0.90 and 1.46 times its baseline, and the pH 7 buffer reading moved by 97.3 mV (about 0.57 pH) between sessions.
- **Integration:** with every module running and two web clients, 3.2 % of vibration reads (351 of 11,112) arrived more than 50 ms after the previous one, against a criterion of none.

## Known limitations

- The "pump" is a handheld-fan motor, with the accelerometer taped to a wooden board. Thresholds apply to this rig only.
- **pH:** no calibrated value was demonstrated; both buffer points were never held in one power cycle, and the kit buffers (pH 4 and 7) lie below the 7.2 to 7.8 pool band.
- **Turbidity** is relative to a clear-water reading, not NTU. A probe raised out of the water reads as cloudier water.
- A sustained **WARNING** was shown in simulation only; no mechanical fault was introduced.
- **Occupancy** was tested by one participant in one room, with a fixed camera, in normal, dim and dark light. Presence and scene change do not identify anyone and do not prove a person caused a change. The radar reports whether someone is in the zone, not how many times they entered.
- No closed-hours flag and no access schedule.
- **In the all-modules build the pump window cannot be set**, because the serial command that sets it is compiled out with the water module, so stoppage detection is off in that build.
- A refused dark frame is sent as −1 and shown on the page as "−1.0 %". There is no radar-lost or clock-error state.
- The event buffer holds ten entries in RAM and is cleared at restart. History is kept by the browser and can be downloaded as CSV; nothing is stored on the board.
- The pH calibration, the turbidity baseline and the camera reference are held in RAM and must be renewed after every restart.
- No authentication or encryption on the local access point, and the credentials are hard-coded.
- Both boards were USB-powered. Local hosting removes the need for the internet, not for power.
- Log timestamps come from the laptop clock. The DS3231 supplies the time of day for pump-window decisions only.

## Privacy and ethics

The camera compares grayscale pixel intensities against an empty-scene reference and reports a scene-change percentage. No face or person recognition is performed.

- Neither board stores images. The empty-scene reference is held in the camera board's memory and cleared at restart.
- The evidence logs contain numbers and timestamps, not image frames.
- The camera's `/capture` page **can** send a live image to a connected browser on request, and the dashboard can hold one event picture in the browser tab until it is closed. Non-identifying processing does not remove every privacy risk.
- An ethics pre-screening letter (11 August 2026) found that a full ethics application was not required on the information submitted. A new questionnaire is required if the project changes.

## Hardware and pins

### DOIT ESP32 DevKit V1

| Peripheral | Connection | Notes |
|---|---|---|
| MPU6050 (vibration) | I2C, SDA GPIO21, SCL GPIO22; AD0 to 3.3 V | Address 0x69. Clone sensor, so registers are read directly |
| DS3231 (RTC) | I2C, shared bus | Address 0x68 |
| DS18B20 (temperature) | 1-Wire, GPIO4 | Two 10 kΩ in parallel (about 5 kΩ) pull-up to 3.3 V |
| pH interface board | ADC1, GPIO34 | 5 V supply, direct connection |
| Turbidity sensor | ADC1, GPIO35 | 5 V supply, 1:1 divider |
| LD2410C (radar) | OUT to GPIO5; UART2 RX GPIO16, TX GPIO17, 256000 baud | 5 V supply. Range 2.25 m and unmanned duration 2 s, set with HLKRadarTool |

Both analogue inputs use ADC1 because ADC2 is unavailable while Wi-Fi is active.

### ESP32-S3 camera board

Pin mapping is in `occupancy/src/occupancy.cpp`: XCLK on GPIO 38, SIOD on GPIO 8, SIOC on GPIO 7. Frames are GRAYSCALE 640×480, `fb_count=2` in PSRAM, xclk 20 MHz.

## Network

The DOIT board creates a Wi-Fi access point:

- **SSID:** `PoolPlantRadar`
- **Password:** `poolplant123`
- **Dashboard:** `http://192.168.4.1/`
- **Radar JSON:** `http://192.168.4.1/radar`

The camera board joins as a station (DHCP) and serves its viewer page at the address it receives (typically `192.168.4.2`). It sends a heartbeat to `/api/occupancy/heartbeat` every 2 s, and the main board marks it offline after 10 s without one.

> **These are bench-test credentials. Change them, and the radar's Bluetooth password, before any deployment.**

## How to build

Requires [PlatformIO](https://platformio.org/). The repository root is the main-board project.

```
# Main board (vibration, water quality, radar, dashboard), from the repository root
pio run -e esp32doit-devkit-v1

# Camera board (occupancy detection)
cd occupancy
pio run -e esp32-s3-cam
```

Upload ports default to COM5 (main board) and COM6 (camera), as set in each `platformio.ini`.

Each module is switched on or off by build flags (`CFG_VIBRATION_CONNECTED`, `CFG_RADAR_CONNECTED` and so on):

| Environment | Configuration | Used for |
|---|---|---|
| `esp32doit-calibrate` | Vibration and clock; prints the baseline | VP01-CAL |
| `esp32doit-bench` | Vibration and clock; baseline and STOPPED threshold as build flags | VP01 to VP06 |
| `esp32doit-bench-fault` | As bench, plus firmware fault injection | VP05-FAULT |
| `esp32doit-devkit-v1` | Module flags as committed | Water-quality and interface tests |
| `esp32doit-all` | Every module enabled | VP07-ALL |

The vibration baseline (0.5356 m/s², measured in VP01-CAL) and the STOPPED threshold (0.102 m/s², from VP02) are compiled in. Re-record the baseline with `esp32doit-calibrate` whenever the motor, mounting or operating level changes.

## Commissioning

References are set on site, and three are lost at every restart. Commission in this order: credentials and coverage, clock, vibration baseline and STOPPED threshold, pump window, operator preferences. After each restart, renew the **camera reference** (leave the view during the 15 s countdown), the **pH calibration** (pH 7.00 then pH 4.00 buffers) and the **turbidity baseline** (clear water).

## Repository layout

```
├── src/                    Main-board firmware (vibration, water quality, radar, dashboard, network, schedule)
├── include/                Main-board headers, including module_flags.h
├── platformio.ini          Main-board build environments
├── tools/                  Vibration test runner (run_vib.py), status logger, integration analysis
├── logs/                   Serial capture logs from bench development
├── evidence/
│   ├── vibration/          VP01-CAL to VP05-NOSENSOR and VP07-ALL records
│   ├── water-quality/      Stage 1 to 5 builds, raw status logs, T2 and T4 disconnection runs
│   └── integration/        VP07-ALL timeline, results and dashboard screenshots
├── ch5_figures_pack/       Scripts, data and figures used in report Chapter 5
├── archive/                Earlier patches and test commit notes
├── occupancy/
│   ├── src/, include/      Camera-board firmware
│   ├── platformio.ini      Camera-board build environment
│   ├── tools/              Trial runner and analysis scripts
│   ├── evidence/occupancy/ Raw trial logs and metadata (OCC-0 to OCC-5b)
│   └── results_final/      Occupancy analysis output (final thresholds)
└── docs/                   Development logs, decision record, dashboard demo
    └── prototypes/         Laptop-only prototype code (not in firmware)
```

## Running trials and analysis

### Vibration

`tools/run_vib.py` runs a scripted vibration test over serial, adds laptop timestamps and test markers, and saves the log with its metadata. The figures in report Chapter 5 can be regenerated from the raw logs with `ch5_figures_pack/ch5_figures_pack/run_all.py`.

### Occupancy

`run_trial.py` runs one scripted occupancy trial. Close any serial monitor first.

```
python occupancy/tools/run_trial.py OCC-2 --port COM6
```

The script resets the board, waits for the reference capture, then prompts the participant to move to DESK (radar only), VIEW (radar and camera) or AWAY at scripted times. It writes `runN.log` and `runN_meta.json` into `occupancy/evidence/occupancy/<scenario>/`. Scenarios: OCC-0, OCC-1, OCC-2, OCC-3, OCC-5-L2, OCC-5-L3, OCC-5b.

`analyse_occupancy.py` reads the saved logs and produces tables and figures:

```
python occupancy/tools/analyse_occupancy.py occupancy/evidence/occupancy --out results --threshold 3 --lag 1.5
python occupancy/tools/analyse_occupancy.py occupancy/evidence/occupancy --suggest-threshold OCC-3
python occupancy/tools/analyse_occupancy.py occupancy/evidence/occupancy --estimate-lag
```

The analysis shifts the scripted position markers by about 5 s (guided by radar-distance changes) and excludes a further 1.5 s after each. These are analysis choices, not measured movement times. Two OCC-1 DESK intervals were excluded after inspecting the data; the raw records remain in the evidence folder.

## Run register

Built from the `runN_meta.json` files in `occupancy/evidence/occupancy/`.

| Scenario | Run | Started | Light | Cam commit | Main commit | Aborted |
|---|---|---|---|---|---|---|
| OCC-0 | 1 | 2026-10-20 01:58 | normal | 4fffb08 | a1a792e | no |
| OCC-1 | 1 | 2026-10-20 02:28 | normal | 231f413 | a1a792e | no |
| OCC-2 | 1 | 2026-10-20 02:13 | normal | 231f413 | a1a792e | no |
| OCC-2 | 2 | 2026-10-20 03:09 | normal | 2cbc219 | a1a792e | no |
| OCC-3 | 1 | 2026-10-20 02:00 | normal | 4fffb08 | a1a792e | no |
| OCC-3 | 2 | 2026-10-20 02:46 | normal | 231f413 | a1a792e | no |
| OCC-5-L2 | 1 | 2026-10-20 02:17 | dim | 231f413 | a1a792e | no |
| OCC-5-L3 | 1 | 2026-10-20 02:21 | dark | 231f413 | a1a792e | no |
| OCC-5-L3 | 2 | 2026-10-20 03:06 | dark | 2cbc219 | a1a792e | no |
| OCC-5b | 1 | 2026-10-20 02:25 | normal | 231f413 | a1a792e | no |
| OCC-5b | 2 | 2026-10-20 03:12 | normal | 2cbc219 | a1a792e | no |

## Commit references in the report

The tag `occupancy-evidence` (a1a792e) marks the main-board configuration used for the occupancy trials.

Some commit IDs in the report come from the original development repositories, before the code was combined here, so they cannot be looked up in this repository. The camera commits (4fffb08, 231f413, 2cbc219, bb5d33c) come from the camera firmware's earlier history; its final source is in `occupancy/src/`. The main-board commits correspond to these commits here:

| Report | This repository | Content |
|---|---|---|
| a344047 | 50e46ee | Water quality stage 4: known-input test scenarios |
| f6ace5d | 9eba2d2 | Water quality stage 5: revised limits, water card, preferences |
| b1aba44 | 5a11825 | VP07-ALL criteria fixed before the run |
| c30f3d3 | 86cd418 | VP07-ALL procedure revision |

## Report

The full write-up, *Pool Plant Monitor: Integrated Vibration, Water-Quality and Access-Zone Sensing* (October 2026), contains the requirements, test protocols, results and revision log. This README summarises it; where they differ, the report is authoritative. Use of AI tools is declared in the report (Appendix A.7).

---

Course project EEE4022S, University of Cape Town, 2026.
