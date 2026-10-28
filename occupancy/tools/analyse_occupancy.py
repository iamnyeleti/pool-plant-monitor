#!/usr/bin/env python3
"""
analyse_occupancy.py

Turns saved camera-board logs into tables and figures. It only READS the logs.

Folder layout (written by run_trial.py):
    <root>/<SCENARIO>/run1.log, run2.log ...

What the logs contain
  * Host-computer timestamps on each line ("HH:MM:SS.mmm > ...").  These are NOT sensor
    acquisition times.
  * "#SCRIPT ..." marker lines written by run_trial.py when it told the person to move.
    Between markers the person was meant to be in one place:
        desk  = at the laptop: the radar sees them, the camera does not
        view  = standing in the camera's picture (the radar sees them too)
        away  = out of the room: neither sensor should see anyone
    The first LAG seconds after each marker are ignored, because a person takes time to move.
    The script times are what was ASKED, not independently observed.

Run:
    python analyse_occupancy.py <root> --out results --threshold 15 --lag 3
    python analyse_occupancy.py <root> --suggest-threshold OCC-3
    python analyse_occupancy.py <root> --estimate-lag
    python analyse_occupancy.py <root> --out results --threshold 3 --shift 7 --lag 1.5
    python analyse_occupancy.py <root> --out results --timeline <root>/OCC-2/run1.log
"""
import argparse
import csv
import math
import os
import re
import statistics as st
import sys
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.ticker
import matplotlib.pyplot as plt

TS = re.compile(r'^(\d{2}):(\d{2}):(\d{2})\.(\d{3}) > ')
RE_RC = re.compile(r'\[RadarClient\] Presence: (YES|NO) \| Distance: (\d+)\s*cm')
RE_EV = re.compile(r'\[Detection\] EVENT #(\d+).*?Camera: ([\d.]+)% change')
RE_DET = re.compile(r'\[Detection\] Radar: (YES|NO) \| Camera: ([\d.]+)% change \| Event: (\w+)')
RE_LAT = re.compile(r'\[Lat\] (\d+) ms')
RE_REF = re.compile(r'Reference captured')
RE_DARK = re.compile(r'Camera: TOO DARK')
RE_BRIGHT = re.compile(r'bright=([\d.]+)')
RE_SCRIPT = re.compile(r'#SCRIPT\s+(\w+)\s*(.*)$')
RE_CAMLINE = re.compile(r'\[Cam\]')
RE_LOOP = re.compile(r'loop (\d+)/(\d+) ms')
RE_JPG = re.compile(r'frame2jpg (\d+)/(\d+) ms')
RE_POLL = re.compile(r'/radar (\d+)/(\d+) ms \(fail (\d+)\)')
RE_CMP = re.compile(r'detect_cmp (\d+)/(\d+) ms')
RE_HEAP = re.compile(r'heap (\d+)')


def parse_log(path):
    episodes, cam, markers, rad, chg = [], [], [], [], []
    cur = None
    refs = dark = idx = day = 0
    ts_present = False
    t0 = last_abs = None

    def open_ep(x, t):
        return {"start_x": x, "start_t": t, "end_t": None, "lat": None, "event": None,
                "event_change": None, "changes": [], "dists": [], "dark": 0, "open_at_eof": False}

    def close_ep(c, t):
        c["end_t"] = t
        episodes.append(c)

    with open(path, "r", errors="replace") as fh:
        for raw in fh:
            line = raw.strip()
            if not line:
                continue
            idx += 1
            t = None
            m = TS.match(line)
            if m:
                h, mi, s, ms = (int(g) for g in m.groups())
                sec = h * 3600 + mi * 60 + s + ms / 1000.0
                abs_sec = sec + day
                if last_abs is not None and abs_sec < last_abs - 1:   # wrapped past midnight
                    day += 86400
                    abs_sec = sec + day
                last_abs = abs_sec
                if t0 is None:
                    t0 = abs_sec
                t = abs_sec - t0
                ts_present = True
                line = line[m.end():]
            x = t if t is not None else float(idx)

            ms_ = RE_SCRIPT.search(line)
            if ms_:
                toks = ms_.group(2).split()
                kv = {k: v for k, v in (tok.split("=", 1) for tok in toks if "=" in tok)}
                arg = next((tok for tok in toks if "=" not in tok), "")
                if t is not None:
                    markers.append((t, ms_.group(1), arg, kv))
                continue

            if RE_REF.search(line):
                refs += 1
                if cur is not None:
                    close_ep(cur, t)
                    cur = None
            if RE_DARK.search(line):
                dark += 1
                if cur is not None:
                    cur["dark"] += 1
            bright = None
            mb = RE_BRIGHT.search(line)
            if mb and "[Detection]" in line:
                bright = float(mb.group(1))

            m = RE_LAT.search(line)
            if m:
                if cur is None:
                    cur = open_ep(x, t)
                cur["lat"] = int(m.group(1))

            m = RE_EV.search(line)
            if m:
                if cur is None:
                    cur = open_ep(x, t)
                ch = float(m.group(2))
                cur["event"] = int(m.group(1))
                cur["event_change"] = ch
                cur["changes"].append(ch)
                chg.append({"x": x, "t": t, "ch": ch, "active": True, "bright": bright})

            m = RE_DET.search(line)
            if m and m.group(1) == "YES":
                if cur is None:
                    cur = open_ep(x, t)
                ch = float(m.group(2))
                cur["changes"].append(ch)
                chg.append({"x": x, "t": t, "ch": ch, "active": m.group(3) == "ACTIVE", "bright": bright})

            m = RE_RC.search(line)
            if m:
                present, dist = m.group(1) == "YES", int(m.group(2))
                if present:
                    if cur is None:
                        cur = open_ep(x, t)
                    cur["dists"].append(dist)
                rad.append({"x": x, "t": t, "present": present, "dist": dist})
                if not present and cur is not None:
                    close_ep(cur, t)
                    cur = None

            if "Radar cleared, event ended" in line and cur is not None:
                close_ep(cur, t)
                cur = None

            if RE_CAMLINE.search(line):
                rec = {"x": x}
                mm = RE_LOOP.search(line)
                if mm:
                    rec["loop_avg"], rec["loop_max"] = int(mm.group(1)), int(mm.group(2))
                mm = RE_JPG.search(line)
                if mm:
                    rec["jpg_max"] = int(mm.group(2))
                mm = RE_POLL.search(line)
                if mm:
                    rec["poll_avg"], rec["poll_max"], rec["poll_fail"] = (int(g) for g in mm.groups())
                mm = RE_CMP.search(line)
                if mm:
                    rec["cmp_max"] = int(mm.group(2))
                mm = RE_HEAP.search(line)
                if mm:
                    rec["heap"] = int(mm.group(1))
                cam.append(rec)

    if cur is not None:
        cur["open_at_eof"] = True
        episodes.append(cur)
    for e in episodes:
        e["corroborated"] = e["event"] is not None
        e["max_change"] = max(e["changes"]) if e["changes"] else None
        e["first_change"] = e["changes"][0] if e["changes"] else None
        e["duration_s"] = (e["end_t"] - e["start_t"]) if (e["end_t"] is not None and e["start_t"] is not None) else None
        e["min_dist"] = min(e["dists"]) if e["dists"] else None
    return {"episodes": episodes, "cam": cam, "markers": markers, "radar": rad, "changes": chg,
            "refs": refs, "dark": dark, "ts": ts_present}


def med(v):
    return st.median(v) if v else None


def fmt(v, nd=1):
    return "" if v is None else (f"{v:.{nd}f}" if isinstance(v, float) else str(v))


def compute_segments(parsed, thr, lag, shift=0.0):
    """Cut a run into segments between #SCRIPT markers and measure each one.
    shift: seconds added to every 'state' marker, because people move after the prompt, not at it."""
    segs = []
    state = None
    n = ""
    light = "normal"
    start = None

    def close(end):
        if state in (None, "transit") or start is None or end is None or end <= start:
            return
        w0 = start + lag
        rs = [r for r in parsed["radar"] if r["t"] is not None and w0 <= r["t"] <= end]
        cs = [c for c in parsed["changes"] if c["t"] is not None and w0 <= c["t"] <= end]
        yes = [r for r in rs if r["present"]]
        vals = [c["ch"] for c in cs]
        br = [c["bright"] for c in cs if c["bright"] is not None]
        segs.append({
            "kind": state, "n": n, "light": light, "t_start": start, "t_end": end,
            "window_s": max(0.0, end - w0),
            "radar_samples": len(rs), "radar_any": bool(yes),
            "radar_frac": (len(yes) / len(rs)) if rs else None,
            "radar_dist_median_cm": med([r["dist"] for r in yes]),
            "change_samples": len(vals), "change_max": max(vals) if vals else None,
            "change_median": med(vals),
            "frac_ge_thr": (sum(1 for v in vals if v >= thr) / len(vals)) if vals else None,
            "camera_any": any(v >= thr for v in vals), "bright_median": med(br), "values": vals,
        })

    for (t, kind, arg, kv) in parsed["markers"]:
        if kind == "light":
            if state is not None:
                close(t)
                start = t
            light = arg or light
        elif kind == "state":
            close(t + shift)
            state, n, start = arg, kv.get("n", ""), t + shift
        elif kind == "end":
            close(t)
            state = start = None
    return segs


def estimate_lags(parsed, near=70.0, far=100.0, span=25.0):
    """How long after each prompt did the RADAR show you had moved? (independent of the camera)
    in : prompt 'view'  -> first time two consecutive radar distances are >= far (cm)
    out: prompt 'desk' after a view -> first time two consecutive distances are <= near (cm)
    A distance of 0 is a radar glitch and is skipped."""
    rad = [r for r in parsed["radar"] if r["t"] is not None and r["present"] and r["dist"] > 0]
    res = []
    prev = None
    for (t, kind, arg, kv) in parsed["markers"]:
        if kind != "state":
            continue
        mode = "in" if arg == "view" else ("out" if (arg == "desk" and prev == "view") else None)
        prev = arg
        if mode is None:
            continue
        cand = [r for r in rad if t <= r["t"] <= t + span]
        lag = None
        for i in range(len(cand) - 1):
            a, b = cand[i], cand[i + 1]
            if (mode == "in" and a["dist"] >= far and b["dist"] >= far) or (mode == "out" and a["dist"] <= near and b["dist"] <= near):
                lag = a["t"] - t
                break
        res.append((mode, lag))
    return res


def read_ground_truth(root):
    gt = {}
    p = os.path.join(root, "ground_truth.csv")
    if os.path.exists(p):
        with open(p, newline="") as fh:
            for r in csv.DictReader(fh):
                try:
                    gt[(r["scenario"].strip(), r["file"].strip())] = (int(r["planned_entries"]), r["expected"].strip())
                except (KeyError, ValueError):
                    pass
    return gt


def write_csv(path, rows, drop=()):
    if not rows:
        return
    keys = []
    for r in rows:
        for k in r:
            if k not in keys and k not in drop:
                keys.append(k)
    with open(path, "w", newline="") as fh:
        w = csv.DictWriter(fh, fieldnames=keys, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("root")
    ap.add_argument("--out", default="results")
    ap.add_argument("--threshold", type=float, default=15.0, help="scene-change threshold, percent")
    ap.add_argument("--lag", type=float, default=3.0, help="seconds ignored after each movement prompt")
    ap.add_argument("--stall-ms", type=int, default=500, help="loop time drawn as a reference line (a chosen value, not a requirement)")
    ap.add_argument("--shift", type=float, default=0.0, help="seconds added to every movement marker (see --estimate-lag)")
    ap.add_argument("--estimate-lag", action="store_true", help="print how late you moved after each prompt, from the radar distance, then stop")
    ap.add_argument("--near", type=float, default=70.0, help="radar distance (cm) at or below which you count as back at the desk")
    ap.add_argument("--far", type=float, default=100.0, help="radar distance (cm) at or above which you count as in the camera's picture")
    ap.add_argument("--exclude", nargs="*", default=[], metavar="SCENARIO:kind", help="drop these segments from tables and figures, e.g. OCC-1:desk")
    ap.add_argument("--timeline", default=None)
    ap.add_argument("--suggest-threshold", default=None, metavar="SCENARIO",
                    help="print the whole number just above the largest change seen while at the desk in SCENARIO")
    a = ap.parse_args()

    files = []
    for dirpath, dirs, fns in sorted(os.walk(a.root)):
        dirs[:] = [d for d in dirs if not d.startswith("_")]
        for fn in sorted(fns):
            if fn.lower().endswith(".log"):
                scen = os.path.basename(dirpath) if os.path.abspath(dirpath) != os.path.abspath(a.root) else "(root)"
                files.append((scen, fn, os.path.join(dirpath, fn)))
    if not files:
        print("No .log files found under", a.root)
        return 1

    parsed = {(s, f): parse_log(p) for s, f, p in files}
    pass  # (segments are built below)
    if a.estimate_lag:
        allin, allout = [], []
        for (s, f), r in sorted(parsed.items()):
            res = estimate_lags(r, a.near, a.far)
            ins = [l for m, l in res if m == "in" and l is not None]
            outs = [l for m, l in res if m == "out" and l is not None]
            allin += ins
            allout += outs
            if res:
                print(f"{s}/{f}: in {', '.join(f'{x:.1f}' for x in ins) or '-'} s | out {', '.join(f'{x:.1f}' for x in outs) or '-'} s")
        both = allin + allout
        if both:
            print(f"\nMedian delay after a 'step in' prompt: {st.median(allin):.1f} s (n={len(allin)})" if allin else "")
            print(f"Median delay after a 'back to desk' prompt: {st.median(allout):.1f} s (n={len(allout)})" if allout else "")
            print(f"Suggested --shift (median of all): {st.median(both):.0f}")
            print(f"Distances used: in = {a.far:g} cm or more, out = {a.near:g} cm or less. These come from your own radar readings.")
        else:
            print("No movement prompts with usable radar distances found.")
        return 0

    segs_all = []
    for (s, f), r in parsed.items():
        for i, sg in enumerate(compute_segments(r, a.threshold, a.lag, a.shift), 1):
            sg.update({"scenario": s, "file": f, "seg": i})
            segs_all.append(sg)

    if a.exclude:
        ex = {tuple(x.split(":", 1)) for x in a.exclude if ":" in x}
        segs_all = [sg for sg in segs_all if (sg["scenario"], sg["kind"]) not in ex]

    if a.suggest_threshold:
        vals = [v for sg in segs_all if sg["scenario"] == a.suggest_threshold and sg["kind"] == "desk" for v in sg["values"]]
        if not vals:
            print(f"No desk-segment camera samples found for {a.suggest_threshold}.")
            return 1
        top = max(vals)
        print(f"Scenario {a.suggest_threshold}: {len(vals)} camera samples while at the desk (lag {a.lag:g} s ignored).")
        print(f"Largest change seen: {top:.1f}%   Median: {st.median(vals):.1f}%")
        print(f"Suggested threshold (whole number just above the largest): {math.floor(top) + 1}")
        return 0

    os.makedirs(a.out, exist_ok=True)
    gt = read_ground_truth(a.root)

    runs, eps_all, cam_rows = [], [], []
    for (s, f), r in parsed.items():
        eps = r["episodes"]
        loop_max = [c["loop_max"] for c in r["cam"] if "loop_max" in c]
        planned, expected = gt.get((s, f), ("", ""))
        runs.append({"scenario": s, "file": f, "timestamps": "host" if r["ts"] else "none (line order)",
                     "script_markers": len(r["markers"]), "planned_entries": planned, "expected": expected,
                     "radar_episodes": len(eps), "camera_events": sum(1 for e in eps if e["corroborated"]),
                     "references": r["refs"], "too_dark_lines": r["dark"],
                     "cam_windows": len(loop_max), "loop_max_ms": max(loop_max) if loop_max else "",
                     "windows_over_stall": sum(1 for v in loop_max if v >= a.stall_ms),
                     "poll_fail_total": sum(c.get("poll_fail", 0) for c in r["cam"]),
                     "heap_min": min((c["heap"] for c in r["cam"] if "heap" in c), default="")})
        for i, e in enumerate(eps, 1):
            eps_all.append({"scenario": s, "file": f, "episode": i, "start_t": e["start_t"], "duration_s": e["duration_s"],
                            "event_no": e["event"], "lat_ms": e["lat"], "first_change_pct": e["first_change"],
                            "max_change_pct": e["max_change"], "min_radar_distance_cm": e["min_dist"],
                            "open_at_eof": e["open_at_eof"]})
        for c in r["cam"]:
            cam_rows.append({"scenario": s, "file": f, **c})

    write_csv(os.path.join(a.out, "runs.csv"), runs)
    write_csv(os.path.join(a.out, "episodes.csv"), eps_all)
    write_csv(os.path.join(a.out, "cam_windows.csv"), cam_rows)
    write_csv(os.path.join(a.out, "segments.csv"), segs_all, drop=("values",))

    grp = defaultdict(list)
    for sg in segs_all:
        grp[(sg["scenario"], sg["light"], sg["kind"])].append(sg)
    summ = []
    for (s, l, k), ss in sorted(grp.items()):
        allv = [v for x in ss for v in x["values"]]
        mx = [x["change_max"] for x in ss if x["change_max"] is not None]
        summ.append({"scenario": s, "light": l, "kind": k, "segments": len(ss),
                     "radar_saw_someone": sum(1 for x in ss if x["radar_any"]),
                     "camera_at_or_above_threshold": sum(1 for x in ss if x["camera_any"]),
                     "segment_max_change_median_pct": fmt(med(mx)), "segment_max_change_min_pct": fmt(min(mx)) if mx else "",
                     "segment_max_change_max_pct": fmt(max(mx)) if mx else "",
                     "all_samples": len(allv), "all_samples_median_pct": fmt(med(allv)),
                     "radar_distance_median_cm": fmt(med([x["radar_dist_median_cm"] for x in ss if x["radar_dist_median_cm"] is not None]), 0),
                     "brightness_median": fmt(med([x["bright_median"] for x in ss if x["bright_median"] is not None]))})
    write_csv(os.path.join(a.out, "segment_summary.csv"), summ)

    plotg = [(k, ss) for k, ss in sorted(grp.items()) if any(x["change_max"] is not None for x in ss)]
    if plotg:
        fig, ax = plt.subplots(figsize=(max(6.0, 1.3 * len(plotg) + 2), 4.2))
        for i, ((s, l, k), ss) in enumerate(plotg):
            pts = [x for x in ss if x["change_max"] is not None]
            for j, x in enumerate(pts):
                ax.scatter(i + (j - (len(pts) - 1) / 2) * 0.07, x["change_max"], marker="o" if x["radar_any"] else "x",
                           color="black", s=28, zorder=3)
        ax.axhline(a.threshold, linestyle="--", color="grey", linewidth=1)
        ax.text(len(plotg) - 0.5, a.threshold, f" threshold {a.threshold:g}%", va="bottom", ha="right", fontsize=8, color="grey")
        ax.set_xticks(range(len(plotg)))
        ax.set_xticklabels([f"{s}\n{l}/{k}" for (s, l, k), _ in plotg], fontsize=7)
        ax.set_xlim(-0.6, len(plotg) - 0.4)
        ax.set_ylabel("Largest scene change in segment (% of ROI pixels)")
        ax.set_title("Camera scene change by scripted position", fontsize=10)
        ax.scatter([], [], marker="o", color="black", label="radar saw someone in the segment")
        ax.scatter([], [], marker="x", color="black", label="radar saw no one")
        ax.legend(fontsize=7, loc="best")
        fig.tight_layout()
        fig.savefig(os.path.join(a.out, "fig_change_by_segment.png"), dpi=200)
        fig.savefig(os.path.join(a.out, "fig_change_by_segment.pdf"))
        plt.close(fig)

    series = defaultdict(list)
    for c in cam_rows:
        if "loop_max" in c:
            series[(c["scenario"], c["file"])].append(c["loop_max"])
    if series:
        fig, ax = plt.subplots(figsize=(7, 3.6))
        for (s, f), v in list(series.items())[:8]:
            ax.plot(range(1, len(v) + 1), v, marker=".", linewidth=1, label=f"{s}/{f}")
        ax.axhline(a.stall_ms, linestyle=":", color="grey", linewidth=1)
        ax.xaxis.set_major_locator(matplotlib.ticker.MaxNLocator(integer=True))
        ax.set_xlabel("5 s reporting window")
        ax.set_ylabel("Longest loop iteration (ms)")
        ax.set_title(f"Camera main-loop stalls (dotted line: {a.stall_ms} ms, chosen reference)", fontsize=10)
        ax.legend(fontsize=7)
        fig.tight_layout()
        fig.savefig(os.path.join(a.out, "fig_cam_stalls.png"), dpi=200)
        fig.savefig(os.path.join(a.out, "fig_cam_stalls.pdf"))
        plt.close(fig)

    if a.timeline:
        r = parse_log(a.timeline)
        fig, (a1, a2) = plt.subplots(2, 1, figsize=(7.2, 4.8), sharex=True)
        rs = [p for p in r["radar"] if p["present"]]
        if rs:
            a1.scatter([p["t"] if p["t"] is not None else p["x"] for p in rs], [p["dist"] for p in rs], s=8, color="black")
        a1.set_ylabel("Radar distance (cm)\n(presence = YES only)")
        cs = r["changes"]
        if cs:
            a2.plot([c["t"] if c["t"] is not None else c["x"] for c in cs], [c["ch"] for c in cs], color="black", linewidth=1, marker=".")
        a2.axhline(a.threshold, linestyle="--", color="grey", linewidth=1)
        a2.set_ylabel("Camera scene change (%)")
        for sg in compute_segments(r, a.threshold, a.lag, a.shift):
            for axx in (a1, a2):
                if sg["kind"] == "view":
                    axx.axvspan(sg["t_start"], sg["t_end"], color="0.85", zorder=0)
                elif sg["kind"] == "away":
                    axx.axvspan(sg["t_start"], sg["t_end"], color="0.93", hatch="//", zorder=0)
        a2.set_xlabel("Time since first line (s)" if r["ts"] else "Log line order (no timestamps)")
        a1.set_title(os.path.basename(a.timeline) + "  (grey: scripted 'view'; hatched: scripted 'away')", fontsize=9)
        fig.tight_layout()
        fig.savefig(os.path.join(a.out, "fig_timeline.png"), dpi=200)
        fig.savefig(os.path.join(a.out, "fig_timeline.pdf"))
        plt.close(fig)

    print(f"Logs read: {len(runs)}   Segments: {len(segs_all)}   Radar episodes: {len(eps_all)}   Output: {a.out}")
    for s in summ:
        print(f"  {s['scenario']:9s} {s['light']:6s} {s['kind']:5s} | segments {s['segments']} | radar saw someone {s['radar_saw_someone']} | "
              f"camera >= threshold {s['camera_at_or_above_threshold']} | largest-change median {s['segment_max_change_median_pct']}%")
    if not any(r["timestamps"] == "host" for r in runs):
        print("NOTE: no timestamps found; segment tables will be empty.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
