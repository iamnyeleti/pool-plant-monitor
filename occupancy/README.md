# OccupancyModule — Camera Node Firmware

Part of the Pool Plant Monitor project (EEE4022S, UCT).  
**Student:** Nyeleti Mushwana (MSHNYE002) · **Supervisor:** Dr Simon Winberg

**Board:** Waveshare ESP32-S3-CAM · **Port:** COM6 · **Branch:** `network-fix`

---

## What this firmware does

Runs on the ESP32-S3 camera board. Connects to the DOIT board's access point as a Wi-Fi station, polls `GET /radar` every 500 ms for presence, and pushes an occupancy heartbeat to the DOIT board every 2 s.

While radar reports presence, the module captures a grayscale frame once per second and compares a region of interest against a stored empty-scene reference using pixel-level differencing. If the percentage of changed pixels exceeds the threshold, an event count is incremented.

**No images are stored. No face detection. No identification.** The system reports only presence and scene-change percentage.

### Constraints

- The DOIT access point must be running before this board boots. If the DOIT board restarts, reset this board afterwards.
- At boot, if Wi-Fi is not joined within 20 s, the board never starts its task loop.
- The dashboard page must never poll this board directly. Opening the camera's own live page stalls the detection loop.

---

## Repository structure

```
src/              Firmware source files (C++, Arduino framework)
  main.cpp        Entry point and main loop
  occupancy.cpp   Camera initialisation
  detection.cpp   Frame-differencing and event counting
  radar_client.cpp Radar polling and heartbeat push to DOIT board
  viewer.cpp      Wi-Fi connection and optional live-view page

include/          Header files

evidence/
  occupancy/      Trial logs and ground-truth records from occupancy experiments
                  (OCC-0 through OCC-5; do not edit)

tools/
  run_trial.py          Run a single timed occupancy trial
  analyse_occupancy.py  Analyse trial results and produce summary tables
  trial.cmd / analyse.cmd  Windows wrappers for the above

logs/             Serial monitor logs from development sessions

results_final/    Final analysis outputs: CSVs, timeline and stall figures

archive/          Intermediate results from earlier analysis passes
                  (results_intermediate, results_l2, results_thr3, results_thr15)
```

---

## Building

Open this folder in VS Code with the PlatformIO extension installed. The only environment is `esp32-s3-cam`.

Build only: `pio run -e esp32-s3-cam`  
Build and upload: `pio run -e esp32-s3-cam --target upload`

---

## Running the analysis tools

```bash
cd tools

# Run one timed trial (records to evidence/occupancy/)
python run_trial.py

# Analyse all trials and write results to results_final/
python analyse_occupancy.py
```

On Windows: `trial.cmd` and `analyse.cmd` in the same folder.

---

## Detection parameters (firmware constants)

| Parameter | Value | Notes |
|---|---|---|
| Scene-change threshold | 15 % | Percentage of ROI pixels that must change |
| Pixel difference threshold | 30 counts | Per-pixel absolute difference to count as changed |
| Minimum brightness | 70 | Frames below this mean brightness are skipped (TOO DARK) |
| Region of interest | x 100–540, y 50–430 (VGA) | Fixed crop, not calibrated for other rooms |

These values were tuned on one room and are **not validated** for other environments or lighting conditions.

---

## Known limits

- Parameters are set for one specific room and one camera placement.
- A lighting change alone produced 11.5–12.7 % scene change against the 15 % threshold — the margin is thin.
- In a dark room the camera reports TOO DARK and gives no change value; only radar presence is available.
- Radar presence can drop out briefly on a very still person.
- No detection accuracy, false-positive rate or multi-person performance is claimed.
