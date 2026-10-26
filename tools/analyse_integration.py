#!/usr/bin/env python3
"""
analyse_integration.py   (version 1.0)

Turns the VP07-ALL evidence into a results table and a timeline figure.

Inputs
  --runlog   evidence\\vibration\\VP07-ALL\\run1.log          (written by run_vib.py: serial text + #SCRIPT markers)
  --meta     evidence\\vibration\\VP07-ALL\\run1_meta.json    (for the date)
  --status   evidence\\integration\\VP07-ALL\\status_log.csv  (written by status_log.py)
  --out      folder for integration_results.md, integration_checks.csv and integration_timeline.png

All times are the laptop clock. Criteria are the ones written in the protocol BEFORE the run
(change note of 26 Oct 2026). Nothing here changes a criterion after seeing data.

Run:  python analyse_integration.py --runlog ... --meta ... --status ... --out ...
"""
import argparse
import csv
import datetime as dt
import json
import os
import re
import statistics
import sys

LINE = re.compile(r"^(\d\d):(\d\d):(\d\d)\.(\d{3}) > (.*)$")
TIMING = re.compile(r"\[TIMING\] n=(\d+) mean_ms=([\d.]+) min_ms=([\d.]+) max_ms=([\d.]+) std_ms=([\d.]+) over_50ms=(\d+) read_errors=(\d+)")

# Criteria fixed in advance (protocol change note, 26 Oct 2026, before the run)
LAG_S = 15.0            # allowance for the radar and camera to react to a step in or out
MIN_SHARE = 0.95        # share of polls in which each module must report its expected state
CLOCK_MAX_OFFSET_S = 300.0
CLOCK_MAX_RANGE_S = 5.0
MIN_ANSWER_SHARE = 0.95


def f(v):
    try:
        return float(v)
    except Exception:
        return None


def read_runlog(path, day):
    markers, windows, cfg = [], [], None
    for raw in open(path, errors="replace").read().replace("\r", "").split("\n"):
        m = LINE.match(raw)
        if not m:
            continue
        t = dt.datetime.combine(day, dt.time(int(m.group(1)), int(m.group(2)), int(m.group(3)), int(m.group(4)) * 1000))
        body = m.group(5)
        if "#SCRIPT" in body:
            markers.append((t, body.split("#SCRIPT", 1)[1].strip()))
        tm = TIMING.search(body)
        if tm:
            windows.append((t, int(tm.group(1)), float(tm.group(2)), float(tm.group(5)), float(tm.group(4)), int(tm.group(6)), int(tm.group(7))))
        if body.startswith("[CFG]") and cfg is None:
            cfg = body
    return markers, windows, cfg


def read_status(path):
    rows = []
    with open(path, newline="", encoding="utf-8") as fh:
        for r in csv.DictReader(fh):
            try:
                r["_t"] = dt.datetime.fromisoformat(r["laptop_time"])
            except Exception:
                continue
            rows.append(r)
    return rows


def truth(v):
    return str(v).strip().lower() == "true"


def nearest(rows, t):
    return min(rows, key=lambda r: abs((r["_t"] - t).total_seconds())) if rows else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--runlog", required=True)
    ap.add_argument("--meta", required=True)
    ap.add_argument("--status", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--speed", type=float, default=1.0, help="only for testing with a sped-up dry run")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    meta = json.load(open(a.meta))
    day = dt.date.fromisoformat(meta["started"][:10])
    markers, windows, cfg = read_runlog(a.runlog, day)
    rows = read_status(a.status)
    lag = dt.timedelta(seconds=LAG_S / a.speed)
    tsec = lambda s: dt.timedelta(seconds=s / a.speed)

    checks = []  # (id, requirement, what was checked, result text, PASS/FAIL/NOT RUN)

    def add(cid, req, what, result, verdict):
        checks.append((cid, req, what, result, verdict))

    mk = {name: t for t, name in markers}
    t_page = next((t for t, n in markers if n.startswith("load phone_page")), None)
    t_start = next((t for t, n in markers if n.startswith("state running")), markers[0][0] if markers else None)

    # ---- C1 acquisition timing
    if windows and t_page:
        loaded, ramp = [], []
        for w in windows:
            (loaded if (w[0] - t_page) >= tsec(60) else ramp).append(w)
        def summ(ws):
            return "n=%d windows; mean %.3f to %.3f ms; largest interval %.3f ms; intervals over 50 ms: %d; read errors: %d" % (
                len(ws), min(w[2] for w in ws), max(w[2] for w in ws), max(w[4] for w in ws), sum(w[5] for w in ws), sum(w[6] for w in ws)) if ws else "no windows"
        add("C1a", "FR-S05", "Ramp-up windows (end before 60 s after the page opened)", summ(ramp), "INFO")
        ok = bool(loaded) and sum(w[5] for w in loaded) == 0 and sum(w[6] for w in loaded) == 0
        add("C1b", "FR-S05", "Loaded windows: no interval over 50 ms and no read errors", summ(loaded), "PASS" if ok else ("FAIL" if loaded else "NOT RUN"))
    else:
        add("C1", "FR-S05", "Timing windows", "no [TIMING] windows or no page marker in the run log", "NOT RUN")

    after = [r for r in rows if t_start and r["_t"] >= t_start + tsec(75)]
    # ---- C5 answered polls
    if rows:
        ok_rows = [r for r in rows if truth(r["ok"])]
        share = len(ok_rows) / len(rows)
        add("C6", "FR-S02", "Share of status polls the board answered", "%d of %d (%.1f%%)" % (len(ok_rows), len(rows), 100 * share), "PASS" if share >= MIN_ANSWER_SHARE else "FAIL")
    else:
        add("C6", "FR-S02", "Status polls", "no rows in the status log", "NOT RUN")

    # ---- C2 honest module states
    def share(pred, rs):
        rs = [r for r in rs if truth(r["ok"])]
        return (sum(1 for r in rs if pred(r)) / len(rs)) if rs else None
    expect = [
        ("vibration live", lambda r: r["vib_state"] == "live"),
        ("clock live", lambda r: r["rtc_state"] == "live"),
        ("radar live", lambda r: r["radar_state"] == "live"),
        ("camera live", lambda r: r["occ_state"] == "live"),
        ("temperature live", lambda r: r["temp_state"] == "live"),
        ("pH raw input live", lambda r: r["ph_state"] == "live"),
        ("turbidity raw input live", lambda r: r["turb_state"] == "live"),
    ]
    for label, pred in expect:
        s = share(pred, after)
        add("C2", "FR-S04", "Module reports its expected state: " + label,
            "n/a" if s is None else "%.1f%% of %d polls after 75 s" % (100 * s, len([r for r in after if truth(r['ok'])])),
            "NOT RUN" if s is None else ("PASS" if s >= MIN_SHARE else "FAIL"))
    s = share(lambda r: r["ph_value"] in ("", "None", None) and r["ph_status"] == "UNAVAILABLE", after)
    add("C2", "FR-S04", "Uncalibrated pH never shown as a pH value (value empty, status UNAVAILABLE)",
        "n/a" if s is None else "%.1f%% of polls" % (100 * s), "NOT RUN" if s is None else ("PASS" if s == 1.0 else "FAIL"))
    s = share(lambda r: r["turb_status"] == "NO_BASELINE", after)
    add("C2", "FR-S04", "Turbidity without a baseline reports NO_BASELINE",
        "n/a" if s is None else "%.1f%% of polls" % (100 * s), "NOT RUN" if s is None else ("PASS" if s >= MIN_SHARE else "FAIL"))

    # ---- C3 radar and camera agreement at the scripted steps
    for t, name in markers:
        m = re.match(r"event enter n=(\d+)", name)
        if m:
            n = int(m.group(1))
            before = [r for r in rows if r["_t"] <= t and truth(r["ok"]) and r["occ_eventCount"] not in ("", None)]
            c0 = f(before[-1]["occ_eventCount"]) if before else None
            win = [r for r in rows if t <= r["_t"] <= t + lag and truth(r["ok"])]
            up = [r for r in win if r["occ_eventCount"] not in ("", None) and c0 is not None and f(r["occ_eventCount"]) is not None and f(r["occ_eventCount"]) >= c0 + 1]
            pres = any(truth(r["radar_presence"]) for r in win)
            hit = up[0] if up else None
            res = "count before %s; " % (int(c0) if c0 is not None else "?")
            res += ("count rose to %d after %.1f s" % (f(hit["occ_eventCount"]), (hit["_t"] - t).total_seconds() * a.speed)) if hit else "count did not rise within %.0f s" % LAG_S
            res += "; radar presence %s" % ("seen" if pres else "NOT seen")
            ok = bool(hit) and pres and f(hit["occ_eventCount"]) == c0 + 1
            add("C3", "FR-O01", "Step IN %d: radar sees presence and the camera counts exactly one new event within %.0f s" % (n, LAG_S), res, "PASS" if ok else "FAIL")
        m = re.match(r"event leave n=(\d+)", name)
        if m:
            n = int(m.group(1))
            win = [r for r in rows if t <= r["_t"] <= t + lag and truth(r["ok"])]
            clear = [r for r in win if r["radar_presence"] in ("False", "false") and r["occ_eventActive"] in ("False", "false", "", "None")]
            res = ("radar clear and event closed after %.1f s" % ((clear[0]["_t"] - t).total_seconds() * a.speed)) if clear else "radar and event did not both clear within %.0f s" % LAG_S
            add("C3", "FR-O01", "Step OUT %d: radar clears and the event closes within %.0f s" % (n, LAG_S), res, "PASS" if clear else "FAIL")
        if name.startswith("shot 1"):
            r = nearest([x for x in rows if truth(x["ok"])], t)
            chg = f(r["occ_changePercent"]) if r else None
            ok = bool(r) and truth(r["radar_presence"]) and chg is not None and 0 <= chg < 15 and not truth(r["occ_eventActive"])
            add("C4", "FR-O01", "Radar-only state at the AWAY spot: radar sees you, camera compares a valid small change (proves a reference frame exists)",
                "radar presence %s, change %s%%, event active %s" % (r["radar_presence"] if r else "?", r["occ_changePercent"] if r else "?", r["occ_eventActive"] if r else "?"),
                "PASS" if ok else "FAIL")
        if name.startswith("shot 3"):
            r = nearest([x for x in rows if truth(x["ok"])], t)
            ok = bool(r) and r["radar_presence"] in ("False", "false")
            add("C4", "FR-O01", "All-clear state: radar presence false", "radar presence %s" % (r["radar_presence"] if r else "?"), "PASS" if ok else "FAIL")

    if not any(n.startswith("event enter") for _, n in markers):
        add("C3", "FR-O01", "Scripted steps in and out of the camera's view", "the run log has no scripted-step markers, so the steps were not executed as scripted and cannot be scored", "NOT RUN")
    if not any(n.startswith("shot 1") for _, n in markers):
        add("C4", "FR-O01", "Radar-only state at the AWAY spot and all-clear state", "the run log has no scripted-step markers, so these states cannot be scored", "NOT RUN")
    # ---- C5 clock
    offs = [f(r["rtc_offset_s"]) for r in rows if truth(r["ok"]) and f(r["rtc_offset_s"]) is not None]
    if offs:
        med, rng = statistics.median(offs), max(offs) - min(offs)
        add("C5a", "FR-S01", "Clock within %.0f s of the laptop (median of laptop minus RTC)" % CLOCK_MAX_OFFSET_S,
            "median %.1f s; the RTC is %s the laptop" % (med, "behind" if med > 0 else "ahead of"), "PASS" if abs(med) <= CLOCK_MAX_OFFSET_S else "FAIL")
        add("C5b", "FR-S01", "Clock keeps its offset (range of laptop minus RTC no more than %.0f s)" % CLOCK_MAX_RANGE_S,
            "range %.1f s over %d polls (min %.1f, max %.1f)" % (rng, len(offs), min(offs), max(offs)), "PASS" if rng <= CLOCK_MAX_RANGE_S else "FAIL")
    else:
        add("C5", "FR-S01", "Clock offset", "the status log has no RTC time", "NOT RUN")

    # ---- write tables
    with open(os.path.join(a.out, "integration_checks.csv"), "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh); w.writerow(["id", "requirement", "check", "result", "verdict"]); w.writerows(checks)
    n_pass = sum(1 for c in checks if c[4] == "PASS"); n_fail = sum(1 for c in checks if c[4] == "FAIL"); n_not = sum(1 for c in checks if c[4] == "NOT RUN")
    with open(os.path.join(a.out, "integration_results.md"), "w", encoding="utf-8") as fh:
        fh.write("# VP07-ALL integration results\n\n")
        fh.write("Run log: `%s`. Status log: `%s` (%d polls). Build line: `%s`.\n\n" % (os.path.basename(a.runlog), os.path.basename(a.status), len(rows), (cfg or "none")[:160]))
        fh.write("Criteria and allowances were fixed in the protocol before the run. Allowance for a step in or out: %.0f s. Module-state share: %.0f%%. Clock: within %.0f s and steady to %.0f s.\n\n" % (LAG_S, 100 * MIN_SHARE, CLOCK_MAX_OFFSET_S, CLOCK_MAX_RANGE_S))
        fh.write("| ID | Requirement | Check | Result | Verdict |\n|---|---|---|---|---|\n")
        for c in checks:
            fh.write("| %s | %s | %s | %s | **%s** |\n" % c)
        fh.write("\nPASS %d, FAIL %d, NOT RUN %d.\n" % (n_pass, n_fail, n_not))
        fh.write("\nA FAIL is reported with its window and cause in the report. It is not explained away.\n")

    # ---- figure
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        okr = [r for r in rows if truth(r["ok"])]
        t0 = t_start or okr[0]["_t"]
        x = [((r["_t"] - t0).total_seconds() * a.speed) for r in okr]
        fig, ax = plt.subplots(4, 1, figsize=(10, 9), sharex=True)
        ax[0].plot(x, [f(r["vib_rms"]) for r in okr], lw=1.2, color="#128e9a")
        ax[0].axhline(0.5356, color="#8ba1b0", ls="--", lw=1); ax[0].set_ylabel("Vibration RMS\n(m/s$^2$)")
        ax[1].step(x, [1 if truth(r["radar_presence"]) else 0 for r in okr], where="post", color="#555")
        ax[1].set_yticks([0, 1]); ax[1].set_yticklabels(["clear", "presence"]); ax[1].set_ylabel("Radar")
        ax[2].plot(x, [f(r["occ_changePercent"]) if (f(r["occ_changePercent"]) is not None and f(r["occ_changePercent"]) >= 0) else None for r in okr], ".", color="#c77700", ms=3)
        ax[2].axhline(15, color="#c0392b", ls="--", lw=1); ax[2].set_ylabel("Camera scene\nchange (%)")
        ax2 = ax[2].twinx(); ax2.step(x, [f(r["occ_eventCount"]) for r in okr], where="post", color="#2a7d4f", lw=1.2); ax2.set_ylabel("Event count")
        ax[3].plot(x, [f(r["rtc_offset_s"]) for r in okr], color="#6c3483", lw=1.2); ax[3].set_ylabel("Laptop minus RTC (s)")
        ax[3].set_xlabel("Seconds from the start of the run (laptop clock)")
        for t, name in markers:
            xs = (t - t0).total_seconds() * a.speed
            col = {"event enter": "#2a7d4f", "event leave": "#c0392b", "load camera_reset": "#1f618d", "load phone_page": "#1f618d", "shot": "#999999"}
            c = next((v for k, v in col.items() if name.startswith(k)), None)
            if c:
                for axx in ax:
                    axx.axvline(xs, color=c, lw=0.8, alpha=0.6)
        ax[0].set_title("VP07-ALL: all modules running (vertical lines: scripted steps)")
        fig.tight_layout(); fig.savefig(os.path.join(a.out, "integration_timeline.png"), dpi=160)
    except Exception as e:
        print("figure not drawn:", e)

    print(open(os.path.join(a.out, "integration_results.md"), encoding="utf-8").read())
    return 0


if __name__ == "__main__":
    sys.exit(main())
