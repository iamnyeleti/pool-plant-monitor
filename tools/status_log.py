#!/usr/bin/env python3
"""
status_log.py   (version 1.0)

Saves, on the laptop, exactly what the dashboard shows, so the report has data and not only screenshots.

  --once     ask the board for ONE status, print it, and print how far the board's clock (RTC) is from
             the laptop's clock. Use this to check the clock fix. Nothing is saved.
  (default)  every 2 s ask the board for /api/status and save it for --seconds seconds:
               status_log.jsonl   the full status text of every poll, with the laptop time
               status_log.csv     the main fields in columns (opens in Excel)
               summary.json       counts and the clock offset at the end
               empty_scene.jpg    (optional) one picture from the camera, the first time the camera
                                  compares a scene that has not changed
               event_<n>.jpg      (optional) one picture from the camera each time a new event starts

The laptop must be joined to the Wi-Fi called PoolPlantRadar. It only READS; it sends nothing to the board.
All times are the laptop's clock. The only people who can appear in the pictures are those in the camera's view.

Run (Windows, from the PoolPlantMonitor folder):
  & "C:\\Users\\nyele\\.platformio\\penv\\Scripts\\python.exe" tools\\status_log.py --once
  & "C:\\Users\\nyele\\.platformio\\penv\\Scripts\\python.exe" tools\\status_log.py --seconds 330 --out evidence\\integration\\VP07-ALL
"""
import argparse
import csv
import datetime as dt
import json
import os
import statistics
import sys
import time
import urllib.error
import urllib.request

VERSION = "1.0"

COLUMNS = [
    "laptop_time", "elapsed_s", "ok", "error",
    "rtc_time", "rtc_state", "rtc_offset_s",
    "vib_state", "vib_status", "vib_rms", "vib_baseline",
    "radar_state", "radar_presence", "radar_distanceCm",
    "occ_state", "occ_moduleOnline", "occ_eventCount", "occ_eventActive", "occ_changePercent",
    "occ_lastEvent", "occ_heartbeatAgeMs",
    "temp_state", "temp_valueC", "temp_reason",
    "ph_state", "ph_rawMv", "ph_value", "ph_status", "ph_statusReason",
    "turb_state", "turb_rawVolts", "turb_status",
]


def dig(d, *path):
    for p in path:
        if not isinstance(d, dict) or p not in d:
            return None
        d = d[p]
    return d


def fetch(url, timeout):
    req = urllib.request.Request(url, headers={"Cache-Control": "no-cache"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def parse_rtc(text):
    try:
        return dt.datetime.strptime(text, "%Y-%m-%d %H:%M:%S")
    except Exception:
        return None


def flatten(status, now, t0, error=None):
    row = {c: None for c in COLUMNS}
    row["laptop_time"] = now.isoformat(timespec="milliseconds")
    row["elapsed_s"] = round((now - t0).total_seconds(), 1)
    row["ok"] = error is None
    row["error"] = error
    if status:
        row["rtc_time"] = dig(status, "rtc", "time")
        row["rtc_state"] = dig(status, "rtc", "state")
        rt = parse_rtc(row["rtc_time"]) if isinstance(row["rtc_time"], str) else None
        if rt:
            row["rtc_offset_s"] = round((now - rt).total_seconds(), 1)
        row["vib_state"] = dig(status, "vibration", "state")
        row["vib_status"] = dig(status, "vibration", "status")
        row["vib_rms"] = dig(status, "vibration", "rms")
        row["vib_baseline"] = dig(status, "vibration", "baseline")
        row["radar_state"] = dig(status, "radar", "state")
        row["radar_presence"] = dig(status, "radar", "presence")
        row["radar_distanceCm"] = dig(status, "radar", "distanceCm")
        row["occ_state"] = dig(status, "occupancy", "state")
        row["occ_moduleOnline"] = dig(status, "occupancy", "moduleOnline")
        row["occ_eventCount"] = dig(status, "occupancy", "eventCount")
        row["occ_eventActive"] = dig(status, "occupancy", "eventActive")
        row["occ_changePercent"] = dig(status, "occupancy", "changePercent")
        row["occ_lastEvent"] = dig(status, "occupancy", "lastEvent")
        row["occ_heartbeatAgeMs"] = dig(status, "occupancy", "heartbeatAgeMs")
        row["temp_state"] = dig(status, "water", "temperature", "state")
        row["temp_valueC"] = dig(status, "water", "temperature", "valueC")
        row["temp_reason"] = dig(status, "water", "temperature", "reason")
        row["ph_state"] = dig(status, "water", "ph", "state")
        row["ph_rawMv"] = dig(status, "water", "ph", "rawMv")
        row["ph_value"] = dig(status, "water", "ph", "value")
        row["ph_status"] = dig(status, "water", "ph", "status")
        row["ph_statusReason"] = dig(status, "water", "ph", "statusReason")
        row["turb_state"] = dig(status, "water", "turbidity", "state")
        row["turb_rawVolts"] = dig(status, "water", "turbidity", "rawVolts")
        row["turb_status"] = dig(status, "water", "turbidity", "status")
    return row


def one_line(row):
    def f(v, fmt="{}"):
        return "-" if v is None else fmt.format(v)
    return ("%s | vib %s %s | radar %s %s cm | camera %s chg %s ev %s%s | temp %s | pH %s mV %s | turb %s V | RTC off %s s" % (
        row["laptop_time"][11:19], f(row["vib_status"]), f(row["vib_rms"], "{:.3f}"),
        {True: "YES", False: "no", None: "-"}[row["radar_presence"]], f(row["radar_distanceCm"]),
        f(row["occ_state"]), f(row["occ_changePercent"], "{:.1f}%"), f(row["occ_eventCount"]),
        "*" if row["occ_eventActive"] else "", f(row["temp_valueC"], "{:.1f}"),
        f(row["ph_rawMv"], "{:.0f}"), f(row["ph_status"]), f(row["turb_rawVolts"], "{:.2f}"),
        f(row["rtc_offset_s"])))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="192.168.4.1")
    ap.add_argument("--port", type=int, default=80)
    ap.add_argument("--once", action="store_true")
    ap.add_argument("--seconds", type=float, default=340)
    ap.add_argument("--wait", type=float, default=300, help="start this program BEFORE the run: it waits up to this many seconds for the board to answer, then starts saving")
    ap.add_argument("--interval", type=float, default=2.0)
    ap.add_argument("--out", default=os.path.join("evidence", "integration", "VP07-ALL"))
    ap.add_argument("--camera-ip", default="192.168.4.2")
    ap.add_argument("--no-pictures", action="store_true", help="do not save camera pictures")
    a = ap.parse_args()
    base = "http://%s:%d" % (a.host, a.port)

    if a.once:
        t0 = dt.datetime.now()
        try:
            status = json.loads(fetch(base + "/api/status", 4))
        except Exception as e:
            print("Could not read the board: %s\nIs the laptop joined to PoolPlantRadar? Is the board powered?" % e)
            return 1
        row = flatten(status, dt.datetime.now(), t0)
        print(one_line(row))
        print("RTC says:   ", row["rtc_time"], "(state: %s)" % row["rtc_state"])
        print("Laptop says:", row["laptop_time"][:19])
        if row["rtc_offset_s"] is None:
            print("The board has no usable clock time right now.")
            return 2
        off = row["rtc_offset_s"]
        print("Laptop minus RTC = %.1f s (%s)" % (off, "RTC is BEHIND the laptop" if off > 0 else "RTC is AHEAD of the laptop"))
        print("Hours: %.2f" % (off / 3600.0))
        return 0

    os.makedirs(a.out, exist_ok=True)
    # Wait for the board. You can start this program before the run; the board restarts at the start of the run.
    waited = time.monotonic()
    first = True
    while True:
        try:
            fetch(base + "/api/status", 2)
            break
        except Exception:
            if time.monotonic() - waited > a.wait:
                print("The board never answered in %.0f s. Is the laptop joined to PoolPlantRadar?" % a.wait)
                return 1
            if first or int(time.monotonic() - waited) % 10 == 0:
                print("Waiting for the board to answer... (this is normal before the run starts)", flush=True)
                first = False
            time.sleep(1.0)
    jl = open(os.path.join(a.out, "status_log.jsonl"), "w", encoding="utf-8")
    cf = open(os.path.join(a.out, "status_log.csv"), "w", newline="", encoding="utf-8")
    cw = csv.DictWriter(cf, fieldnames=COLUMNS)
    cw.writeheader()
    t0 = dt.datetime.now()
    end = time.monotonic() + a.seconds
    n_ok = n_bad = 0
    offsets = []
    last_active = False
    got_empty = False
    pictures = []
    print("status_log %s: polling %s every %.1f s for %.0f s. Saving to %s. Press Ctrl+C to stop early." % (VERSION, base, a.interval, a.seconds, a.out))
    try:
        while time.monotonic() < end:
            tick = time.monotonic()
            now = dt.datetime.now()
            status, err = None, None
            try:
                raw = fetch(base + "/api/status", 3)
                status = json.loads(raw)
            except Exception as e:
                err = str(e)[:120]
            row = flatten(status, now, t0, err)
            cw.writerow(row)
            jl.write(json.dumps({"laptop_time": row["laptop_time"], "ok": err is None, "error": err, "status": status}) + "\n")
            jl.flush(); cf.flush()
            if err is None:
                n_ok += 1
                if row["rtc_offset_s"] is not None:
                    offsets.append(row["rtc_offset_s"])
            else:
                n_bad += 1
            print(one_line(row) if err is None else "%s | NO ANSWER from the board (%s)" % (now.strftime("%H:%M:%S"), err), flush=True)

            if not a.no_pictures and err is None:
                chg = row["occ_changePercent"]
                active = bool(row["occ_eventActive"])
                try:
                    if (not got_empty and row["occ_state"] == "live" and row["radar_presence"] is True
                            and isinstance(chg, (int, float)) and 0 <= chg < 5 and not active):
                        p = os.path.join(a.out, "empty_scene.jpg")
                        open(p, "wb").write(fetch("http://%s/capture" % a.camera_ip, 8))
                        got_empty = True
                        pictures.append({"file": "empty_scene.jpg", "laptop_time": row["laptop_time"], "change_percent": chg})
                        print("   saved picture:", p)
                    if active and not last_active and isinstance(chg, (int, float)) and chg >= 0:
                        name = "event_%s.jpg" % (row["occ_eventCount"] if row["occ_eventCount"] is not None else "x")
                        p = os.path.join(a.out, name)
                        open(p, "wb").write(fetch("http://%s/capture" % a.camera_ip, 8))
                        pictures.append({"file": name, "laptop_time": row["laptop_time"], "change_percent": chg})
                        print("   saved picture:", p)
                except Exception as e:
                    print("   (picture not saved: %s)" % str(e)[:80])
                last_active = active
            time.sleep(max(0.0, a.interval - (time.monotonic() - tick)))
    except KeyboardInterrupt:
        print("\nStopped by you.")
    finally:
        jl.close(); cf.close()
        summary = {
            "version": VERSION, "board": base, "started_laptop_time": t0.isoformat(timespec="seconds"),
            "polls_answered": n_ok, "polls_unanswered": n_bad, "interval_s": a.interval,
            "rtc_offset_s_median": statistics.median(offsets) if offsets else None,
            "rtc_offset_s_min": min(offsets) if offsets else None,
            "rtc_offset_s_max": max(offsets) if offsets else None,
            "pictures": pictures, "note": "laptop clock; the logger only reads",
        }
        json.dump(summary, open(os.path.join(a.out, "summary.json"), "w"), indent=2)
        print("\nSaved: status_log.csv, status_log.jsonl, summary.json in", a.out)
        print("Answered %d, unanswered %d." % (n_ok, n_bad))
        if offsets:
            print("RTC offset (laptop minus RTC): median %.1f s, range %.1f to %.1f s" % (summary["rtc_offset_s_median"], summary["rtc_offset_s_min"], summary["rtc_offset_s_max"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
