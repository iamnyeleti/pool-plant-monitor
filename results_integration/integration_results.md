# VP07-ALL integration results

Run log: `run1.log`. Status log: `status_log.csv` (169 polls). Build line: `[CFG] dlpf=0x00 baseline=0.5356 caution_mult=1.20 warning_mult=1.50 window=6000 fraction=0.50 sample_delay_ms=20 calibration=0 stopped_std=0.102 stopped_hold_s=`.

Criteria and allowances were fixed in the protocol before the run. Allowance for a step in or out: 15 s. Module-state share: 95%. Clock: within 300 s and steady to 5 s.

| ID | Requirement | Check | Result | Verdict |
|---|---|---|---|---|
| C1a | FR-S05 | Ramp-up windows (end before 60 s after the page opened) | n=1 windows; mean 25.263 to 25.263 ms; largest interval 73.351 ms; intervals over 50 ms: 1; read errors: 0 | **INFO** |
| C1b | FR-S05 | Loaded windows: no interval over 50 ms and no read errors | n=5 windows; mean 24.640 to 25.246 ms; largest interval 94.403 ms; intervals over 50 ms: 7; read errors: 0 | **FAIL** |
| C6 | FR-S02 | Share of status polls the board answered | 169 of 169 (100.0%) | **PASS** |
| C2 | FR-S04 | Module reports its expected state: vibration live | 100.0% of 169 polls after 75 s | **PASS** |
| C2 | FR-S04 | Module reports its expected state: clock live | 100.0% of 169 polls after 75 s | **PASS** |
| C2 | FR-S04 | Module reports its expected state: radar live | 100.0% of 169 polls after 75 s | **PASS** |
| C2 | FR-S04 | Module reports its expected state: camera live | 100.0% of 169 polls after 75 s | **PASS** |
| C2 | FR-S04 | Module reports its expected state: temperature live | 100.0% of 169 polls after 75 s | **PASS** |
| C2 | FR-S04 | Module reports its expected state: pH raw input live | 100.0% of 169 polls after 75 s | **PASS** |
| C2 | FR-S04 | Module reports its expected state: turbidity raw input live | 100.0% of 169 polls after 75 s | **PASS** |
| C2 | FR-S04 | Uncalibrated pH never shown as a pH value (value empty, status UNAVAILABLE) | 100.0% of polls | **PASS** |
| C2 | FR-S04 | Turbidity without a baseline reports NO_BASELINE | 100.0% of polls | **PASS** |
| C3 | FR-O01 | Scripted steps in and out of the camera's view | the run log has no scripted-step markers, so the steps were not executed as scripted and cannot be scored | **NOT RUN** |
| C4 | FR-O01 | Radar-only state at the AWAY spot and all-clear state | the run log has no scripted-step markers, so these states cannot be scored | **NOT RUN** |
| C5a | FR-S01 | Clock within 300 s of the laptop (median of laptop minus RTC) | median 110.9 s; the RTC is behind the laptop | **PASS** |
| C5b | FR-S01 | Clock keeps its offset (range of laptop minus RTC no more than 5 s) | range 1.0 s over 169 polls (min 110.4, max 111.4) | **PASS** |

PASS 12, FAIL 1, NOT RUN 2.

A FAIL is reported with its window and cause in the report. It is not explained away.
