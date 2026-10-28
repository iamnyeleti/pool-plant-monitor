# Occupancy module: experiment plan (draft for approval)

Project: Pool Plant Monitor and Preventative Maintenance Advisor (EEE4022S, SW-3)
Written 19 October 2026. Final submission: 27 October 2026, 11:59 pm.
Built only from this conversation, the course handout and the marking rubric. I have not read your Chapters 1 to 5 or your requirement IDs, so anything marked CHECK must be matched to your thesis.

---

## REVISION, 19 October 2026 evening (replaces sections 3, 5 and 6 wherever they differ)

- Positions are now only three: DESK (at the laptop: the radar sees you, the camera does not), VIEW (standing in the camera's picture, radar sees you too) and AWAY (out of the room). P0, P1 and P2 are no longer used. The radar distance in the logs gives the distances, so no tape measure is needed.
- Because you sit within radar range the whole time, the radar presence stays YES during a run. The evidence is therefore taken from scripted segments, not from entry counts or event counters.
- Every run is driven by `run_trial.py`. It resets the camera, writes a timestamped log, prints instructions and beeps when you must move, writes `#SCRIPT` markers into the log at those moments, and files the log, metadata and ground truth by itself.
- `analyse_occupancy.py` cuts each log at the markers and ignores the first 3 s of each segment (movement time). Per segment it reports whether the radar saw someone, the camera's largest and median scene change, whether the camera reached the threshold, radar distance and brightness.
- Run list: OCC-0 (one-entry check), OCC-3 (3 minutes at the desk: camera noise level and calibration set), OCC-2 (five entries into VIEW, verification set), OCC-5-L2 and OCC-5-L3 (dim and dark, reference taken in that light), OCC-5b (lighting changed while at the desk), OCC-1 (empty room, 10 minutes), OCC-4 (three 60 s stays, optional).
- `python analyse_occupancy.py evidence\occupancy --suggest-threshold OCC-3` prints the threshold from the OCC-3 noise level: the whole number just above the largest change seen at the desk.
- Limits added: the script times are what was asked, not observed; you move 1 to 3 s after the prompt; timestamps are the computer's clock.
- Tested: the analysis against hand-computed numbers, and the runner against a fake board (synthetic, not evidence). Not tested: the runner on your actual camera, including the serial reset and the beeps.

---

## 1. Where the evidence stands today

| Item | What exists | Status |
|---|---|---|
| Radar tracks a person (OUT pin and UART distance) | Several uncontrolled walks, logs saved | VERIFIED ON HARDWARE (demonstration only) |
| Radar to camera to DOIT event chain | Camera log: 3 events. DOIT log: events #1 to #8, each after a radar LOW to HIGH | VERIFIED ON HARDWARE (uncontrolled, no ground truth) |
| Camera comparison time | `detect_cmp` 15 to 16 ms, `[Lat]` 15 ms for 3 events | VERIFIED ON HARDWARE (one run) |
| Radar poll time | 41 to 70 ms average, 133 ms worst, 0 failures in 20 windows | VERIFIED ON HARDWARE (one run) |
| Camera loop stalls | In the one `[Cam]` log: the 8 windows with no JPEG conversion had a longest loop of 107 to 128 ms. The 12 windows with JPEG conversions had 375 to 2033 ms | VERIFIED ON HARDWARE (one run) |
| Scene-change threshold | The code compares a percentage with a fraction, so the effective threshold was probably 0.15%, not 15% (fix pending). EVENT #1 was "corroborated" at 1.8% | Everything classified by threshold so far is PRELIMINARY |
| Lighting sensitivity | One observation: light off during presence gave a steady 14.7% change | PRELIMINARY (one observation) |
| Radar present, person out of camera view | One observation: about 7.7% change | PRELIMINARY (one observation) |
| Repeats per scenario, scripted ground truth, false-alarm baseline, non-human test, communication-loss test | None | NOT DONE |

Conclusion: you have a working demonstration and some useful timing numbers. You do not yet have controlled, repeated, analysed evidence. That is the gap this plan closes.

## 2. Candidate research sub-question (CHECK against your Chapter 1)

Does combining a 24 GHz radar with a grayscale scene-change camera give an operator more reliable information about activity in an access zone than either sensor alone, and what are the conditions under which each fails?

Every experiment below answers part of that. The expected finding is a characterised trade-off, not a detection accuracy. Do not claim an accuracy or false-positive rate from five events.

## 3. Fixed setup and controls

Record these once, with a photograph, before any run:

- Firmware: commit hash of PoolPlantMonitor (network-bench) and OccupancyModule. Save `git log --oneline -1` from each.
- Radar settings, read back in HLKRadarTool and screenshotted: detection range (2.25 m was set earlier), unmanned duration (2 s). Do not change them between runs.
- Positions, measured with a tape and marked on the floor:
  - P0: start point, outside radar range.
  - P1: inside radar range and inside the camera's region of interest.
  - P2: inside radar range, outside the camera's field of view. Check this on the live camera page before the run.
- Camera region of interest: 440 × 380 pixels of 640 × 480. Working: 440 × 380 = 167,200; 640 × 480 = 307,200; 167,200 ÷ 307,200 = 0.544, so 54.4% of the frame.
- Camera compare: once per second while radar presence is YES. Pixel counts as changed if its brightness differs from the reference by more than 30 (scale 0 to 255).
- Reference frame: taken at each camera boot after the 15 s countdown, in the lighting of that run. Every run starts with a fresh camera boot.
- Script for each entry: start at P0, walk to the position, stand 10 s, walk back to P0, wait 8 s.
- Not measured: radar's own reaction time, illuminance (use a phone light-meter app only as a rough note), walking speed.

## 4. Threshold discipline (what stops this being tuned to pass)

1. Calibration set: OCC-3 (radar present, nobody in camera view). Its change values show the camera noise level.
2. Rule, fixed now: the threshold is set just above the largest change seen in OCC-3, rounded up to the next whole percent.
3. Freeze it. Then run OCC-2 (entries in view) as the verification set.
4. Report the separation: smallest OCC-2 entry change minus the chosen threshold.
5. State that the noise level belongs to this room, camera and lighting.

## 5. Experiment matrix

Duration includes camera reboots. Total P0 is roughly 40 minutes of bench time plus setup. CHECK: map each to your FR IDs (FR-O01 is radar and camera corroboration).

| ID | Purpose | Entries | Expected class | Priority | Time |
|---|---|---|---|---|---|
| OCC-0 | Confirm the units fix and logging work (precondition, not a result) | 0 | none | P0 | 5 min |
| OCC-1 | Empty-zone baseline: radar episodes with nobody in range | 0 | none | P0 | 10 min |
| OCC-2 | Entry in view of both sensors (verification set) | 5 | both | P0 | 8 min |
| OCC-3 | Radar-only: person at P2 (calibration set, noise level) | 5 | radar_only | P0 | 8 min |
| OCC-5 | Lighting, matched reference: L1 normal, L2 dim, L3 dark, entries at P1 | 3 per level | both, L3 too dark | P0 | 12 min |
| OCC-5b | Lighting, mismatched reference: reference in L1, then person at P2 under L2 (radar present, change caused by light only) | 3 | radar_only if no light effect | P0 | 4 min |
| OCC-4 | Stationary person at P1 for 60 s | 3 | both, then decay | P1 | 5 min |
| OCC-7 | Non-human movement: small fan running at P1, nobody present | 3 | unknown (the question) | P1 | 5 min |
| OCC-8 | Communication loss: unplug the DOIT board's USB while presence is YES | 2 | unknown | P1 | 5 min |
| OCC-9 | Effect of the camera page on loop timing: page closed 2 min, then open 2 min | 2 | n/a | P1 | 6 min |

Ethics gate: OCC-2 to OCC-5b and OCC-8 involve a person in view of the camera, and OCC-4. The handout says no data may be collected without ethics clearance. I don't know your status. Do OCC-0, OCC-1, OCC-7 and OCC-9 now. Do not run the rest until you have confirmed clearance with Dr Winberg. The camera stores no images in this build, but that is for him to judge.

## 6. Logging plan

Before the first run:
1. In both `platformio.ini` files change the monitor filter line to `monitor_filters = default, time, log2file`. The `time` filter adds the computer's clock to each line, so durations can be computed. I believe `time` must come before `log2file`. Check the first lines of a new log: they should start like `10:23:45.123 > `. These times are computer-side, not sensor acquisition times.
2. One log per run, filed as `evidence/occupancy/OCC-2/run1.log` (scenario folder, run file). Never edit a log.
3. Write `evidence/occupancy/ground_truth.csv` as you go, using `ground_truth_template.csv`. Columns: scenario, file, planned_entries, expected (both, radar_only or none).
4. After each run, note in a text file: firmware hashes, lighting, anything unusual.

Firmware gaps to fix first (small):
- Units fix on the threshold (pending).
- Print the average brightness of the region each second, so lighting can be quantified. At present only a "too dark" line is printed.
- "Radar communication lost" state does not exist. OCC-8 documents that as a finding. Do not fix it before the test.

## 7. Analysis

Run `python analyse_occupancy.py evidence/occupancy --out results --threshold <frozen value>`.

Per scenario it reports: radar episodes, camera-corroborated episodes, radar-only episodes, per planned entry ratios, maximum scene change per episode (median, minimum, maximum), `[Lat]`, episode durations (if timestamps), too-dark lines, loop stalls, radar poll failures. It never edits the logs. It was tested on the real log from this conversation and on a synthetic parser file; the synthetic file is not data.

Report counts as counts (for example 5 of 5). Give minimum, median and maximum, not statistical significance, at these sample sizes.

## 8. Figures and tables to produce

| Item | Question | Data | Source |
|---|---|---|---|
| F1 Geometry diagram | Where is each sensor's region and where were P0, P1, P2? | Tape measurements, photographs | New, drawn by hand or Matplotlib |
| F2 Scene change by scenario | Does the camera separate entries from radar-only presence? | `episodes.csv` | Tool output, `fig_change_by_scenario` |
| F3 Timeline | How do radar distance and scene change evolve in one entry and in a stationary stay? | One OCC-2 and one OCC-4 log | Tool output, `--timeline` |
| F4 Loop stalls | What does the camera page do to the main loop? | OCC-9 | Tool output, `fig_cam_stalls` |
| F5 Lighting | How does lighting change the scene-change value? | OCC-5 and OCC-5b rows | Table from `episodes.csv` |
| T1 Scenario summary | Counts per scenario | `scenarios.csv` | Tool output |
| T2 Latency budget | Where does time go between radar event and camera comparison? | Poll interval 500 ms, poll time (measured), compare time (measured) | Calculation |

T2 working so far: 500 ms (poll interval) + 133 ms (slowest poll seen) + 16 ms (compare) = 649 ms. The radar's own reaction time is not included and not measured.

## 9. Pre-stated interpretation (written before testing)

- OCC-2: if every planned entry gives one radar episode and a camera event, say "in this setup the camera corroborated all N entries", and give the separation from the frozen threshold. Do not generalise.
- OCC-3: any radar episode with no camera event is a radar-only event. State that radar presence alone cannot say whether the cause is a person.
- OCC-5/5b: any event that appears with no person in view is lighting-induced. That limits corroboration.
- OCC-7: whatever the radar does with the fan is the result. The manufacturer warns about non-human movement, so a radar trigger would be consistent with that.
- OCC-8: a presence value that stays unchanged after the DOIT board is unplugged is a communication-failure result, not an absence-of-person result. It supports keeping those two states separate (FR-S04, CHECK).

## 10. What each chapter gets (CHECK against your section numbers)

- Chapter 3 (method): sections 3 to 6 above: controls, scenarios, ground truth, threshold discipline, logging, tool.
- Chapter 4 (design): radar and camera roles, radar-triggered gating, comparison algorithm and its parameters, Wi-Fi bridge and heartbeat, logging formats, and the units bug as a verification finding.
- Chapter 5 (results): F2 to F5, T1, T2, and the discussion of: radar-only events, lighting, non-human movement, communication loss, loop stalls, and what the evidence does and does not establish.

## 11. Limits to state in the report whatever the results

- One room, one camera, one radar, one person, small numbers of events.
- Ground truth comes from a scripted procedure, not independent observation.
- Computer-side timestamps. Radar reaction time unmeasured.
- Scene change is not identification and radar presence is not proof of a person.
- Camera cannot detect anything the radar misses, because it is radar-triggered.
