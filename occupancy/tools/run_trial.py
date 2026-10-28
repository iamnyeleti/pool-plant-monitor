#!/usr/bin/env python3
"""
run_trial.py

Runs ONE occupancy trial for you. You only move when it tells you.

    python tools\\run_trial.py OCC-2

What it does, so you do not have to:
  * opens the camera board's serial port, resets the board and logs everything with a
    timestamp to evidence\\occupancy\\<scenario>\\runN.log
  * waits for the camera to take its reference picture (you stay at your desk, out of view)
  * prints a large instruction and beeps each time you must move, and writes a #SCRIPT
    marker into the log at that moment
  * writes ground_truth.csv and runN_meta.json (git commit hashes, times) by itself

Your desk is the 'radar sees you, camera does not' position. 'view' means standing in the
camera's picture. 'away' means out of the room.

Close any serial monitor first (the port can only be used by one program).

Scenarios:  OCC-0  OCC-3  OCC-2  OCC-4  OCC-5-L2  OCC-5-L3  OCC-5b  OCC-1
Options:    --port COM6   --out <folder>   --note "text"   --yes (skip Enter prompts)
            --dry-run [--speed 4]   synthetic test without hardware (NOT evidence)
"""
import argparse
import csv
import datetime
import json
import os
import random
import subprocess
import sys
import threading
import time

try:
    import winsound
except ImportError:
    winsound = None
try:
    import serial
except ImportError:
    serial = None

VERSION = "1.0"

LIGHT_HELP = {
    "normal": "main light ON",
    "dim": "main light OFF, one small lamp ON",
    "dark": "ALL lights OFF (your laptop screen can stay on)",
}

SCENARIOS = {
    "OCC-0": {"desc": "Check: one entry into the camera's picture", "light": "normal", "entries": 1, "hold": 12, "gap": 10},
    "OCC-3": {"desc": "Noise level: sit at your desk (radar sees you, camera does not) for 3 minutes", "light": "normal", "desk_s": 180},
    "OCC-2": {"desc": "Five entries into the camera's picture, normal light", "light": "normal", "entries": 5, "hold": 12, "gap": 10},
    "OCC-4": {"desc": "Three 60 s stays standing still in the picture", "light": "normal", "entries": 3, "hold": 60, "gap": 15},
    "OCC-5-L2": {"desc": "Three entries, DIM lighting (reference taken dim)", "light": "dim", "entries": 3, "hold": 12, "gap": 10},
    "OCC-5-L3": {"desc": "Three entries, DARK lighting (reference taken dark)", "light": "dark", "entries": 3, "hold": 12, "gap": 10},
    "OCC-5b": {"desc": "Lighting changed after the reference while you sit at your desk", "light": "normal",
               "light_changes": [("dim", 60), ("normal", 60)]},
    "OCC-1": {"desc": "Empty room: leave for 10 minutes, nobody inside", "light": "normal", "away_s": 600},
}


def beep(kind):
    pat = {"in": [(1000, 200), (1000, 200)], "out": [(500, 350)], "end": [(800, 250)] * 3}.get(kind, [])
    for f, d in pat:
        if winsound:
            winsound.Beep(f, d)
        else:
            print("\a", end="", flush=True)
            time.sleep(d / 1000.0)


def banner(text):
    print("\n" + "=" * 66)
    print("  " + text)
    print("=" * 66, flush=True)


def build_timeline(cfg):
    """Return a list of steps: t (seconds after the reference), mark (log marker), say, beep."""
    steps = [{"t": 0, "mark": f"light {cfg['light']}", "say": None, "beep": None},
             {"t": 0, "mark": "state desk", "say": "STAY AT YOUR DESK. Do not enter the camera's picture.", "beep": None}]
    t = 10
    if "entries" in cfg:
        n = cfg["entries"]
        for i in range(1, n + 1):
            steps.append({"t": t, "mark": f"state view n={i}",
                          "say": f"ENTRY {i} of {n}: STEP INTO THE CAMERA'S PICTURE and STAND STILL, facing the camera.", "beep": "in"})
            t += cfg["hold"]
            steps.append({"t": t, "mark": "state desk", "say": "BACK TO YOUR DESK and SIT STILL.", "beep": "out"})
            t += cfg["gap"]
    if "desk_s" in cfg:
        t += cfg["desk_s"]
    for name, secs in cfg.get("light_changes", []):
        steps.append({"t": t, "mark": f"light {name}",
                      "say": f"CHANGE THE LIGHTING TO {name.upper()} NOW ({LIGHT_HELP[name]}), from your desk.", "beep": "in"})
        t += secs
    if "away_s" in cfg:
        grace = 20
        steps.append({"t": t, "mark": "state transit", "say": "LEAVE THE ROOM NOW. Close the door. Stay out until you hear three beeps.", "beep": "in"})
        steps.append({"t": t + grace, "mark": "state away", "say": None, "beep": None})
        t += grace + cfg["away_s"]
        steps.append({"t": t, "mark": "state transit", "say": "COME BACK NOW and SIT AT YOUR DESK.", "beep": "end"})
        t += 20
        steps.append({"t": t, "mark": "state desk", "say": None, "beep": None})
        t += 5
    else:
        t += 5
    steps.append({"t": t, "mark": "end", "say": "FINISHED. You can relax. The log is saved.", "beep": "end"})
    return steps


class FakeSerial:
    """Synthetic board for --dry-run. Values are invented to test the pipeline. NOT evidence."""

    def __init__(self, speed):
        self.speed = speed
        self.buf = bytearray()
        self.lock = threading.Lock()
        self.state = "desk"
        self.light = "normal"
        self.rng = random.Random(7)
        self.stop = threading.Event()
        self.thread = None

    def _emit(self, s):
        with self.lock:
            self.buf += (s + "\r\n").encode()

    def reset(self):
        self.stop.set()
        if self.thread:
            self.thread.join()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _sleep(self, s):
        end = time.time() + s / self.speed
        while time.time() < end and not self.stop.is_set():
            time.sleep(0.02)

    def _run(self):
        self._emit("Occupancy module starting...")
        for n in range(15, 0, -1):
            if self.stop.is_set():
                return
            self._emit(f"Capturing in {n}...")
            self._sleep(1)
        self._emit("Reference captured. Detection is now active.")
        active = False
        k = 0
        while not self.stop.is_set():
            self._sleep(1)
            k += 1
            st = self.state
            if st == "away":
                self._emit("[RadarClient] Presence: NO | Distance: 199cm")
                active = False
            else:
                dist = self.rng.randint(90, 130) if st == "view" else self.rng.randint(40, 55)
                self._emit(f"[RadarClient] Presence: YES | Distance: {dist}cm")
                ch = max(0.0, self.rng.gauss(55, 8) if st == "view" else self.rng.gauss(3, 1))
                bright = {"normal": 110, "dim": 60, "dark": 12}[self.light] + self.rng.random()
                if ch >= 15 and not active:
                    active = True
                    self._emit(f"[Detection] EVENT #1 \u2014 Radar: YES | Camera: {ch:.1f}% change (corroborated) | bright={bright:.1f}")
                else:
                    self._emit(f"[Detection] Radar: YES | Camera: {ch:.1f}% change | Event: {'ACTIVE' if active else 'none'} | bright={bright:.1f}")
            if k % 5 == 0:
                self._emit("[Cam] loop 2/120 ms; fb_get 0/0 ms; frame2jpg 0/0 ms; /radar 50/70 ms (fail 0); detect_cmp 16/16 ms; heap 240000 PSRAM 7449267")

    def read(self, n):
        time.sleep(0.05)
        with self.lock:
            out = bytes(self.buf[:n])
            del self.buf[:n]
        return out

    def close(self):
        self.stop.set()


class Logger:
    def __init__(self, path, ser):
        self.f = open(path, "w", encoding="utf-8")
        self.ser = ser
        self.lock = threading.Lock()
        self.countdown = threading.Event()
        self.ref = threading.Event()
        self.halt = threading.Event()
        self.th = threading.Thread(target=self._run, daemon=True)

    @staticmethod
    def ts():
        return datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]

    def write(self, text):
        with self.lock:
            self.f.write(f"{self.ts()} > {text}\n")
            self.f.flush()

    def mark(self, text):
        self.write("#SCRIPT " + text)

    def _handle(self, line):
        self.write(line)
        if "Capturing in" in line:
            self.countdown.set()
            print("  camera: " + line.strip(), flush=True)
        elif "Reference captured" in line:
            self.ref.set()
            print("  camera: " + line.strip(), flush=True)
        elif "EVENT #" in line:
            print("  camera: " + line.strip()[:90], flush=True)

    def _run(self):
        buf = b""
        while not self.halt.is_set():
            try:
                data = self.ser.read(512)
            except Exception as e:  # port vanished
                self.write(f"#SCRIPT serial_error {type(e).__name__}")
                break
            if not data:
                continue
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode(errors="replace").rstrip("\r")
                if text.strip():
                    self._handle(text)

    def start(self):
        self.th.start()

    def close(self):
        self.halt.set()
        self.th.join(timeout=2)
        self.f.close()


def git_hash(path):
    try:
        return subprocess.check_output(["git", "-C", path, "rev-parse", "--short", "HEAD"], stderr=subprocess.DEVNULL).decode().strip()
    except Exception:
        return "unknown"


def next_run(folder):
    n = 1
    while os.path.exists(os.path.join(folder, f"run{n}.log")):
        n += 1
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenario", choices=sorted(SCENARIOS))
    ap.add_argument("--port", default="COM6")
    ap.add_argument("--out", default=None)
    ap.add_argument("--note", default="")
    ap.add_argument("--yes", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--speed", type=float, default=1.0, help="dry-run only: speed-up factor")
    a = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = a.out or os.path.join(root, "evidence", "occupancy")
    if a.dry_run:
        out = os.path.join(out, "_dryrun")
    else:
        a.speed = 1.0
    cfg = SCENARIOS[a.scenario]
    steps = build_timeline(cfg)
    total = steps[-1]["t"]

    if a.dry_run:
        banner("DRY RUN: SYNTHETIC DATA. THIS IS NOT EVIDENCE.")
    banner(f"{a.scenario}: {cfg['desc']}")
    print(f"  About {int((total + 45) // 60)} to {int((total + 45) // 60) + 1} minutes in total.")
    print(f"  Lighting for this run: {cfg['light'].upper()} ({LIGHT_HELP[cfg['light']]}).")
    print("  Sit at your desk. Do not enter the camera's picture until told.")
    if not a.dry_run and serial is None:
        print("  pyserial is missing. Run:  pip install pyserial")
        return 1
    if not (a.yes or a.dry_run):
        input("\n  Set the lighting as above, then press Enter to start... ")

    folder = os.path.join(out, a.scenario)
    os.makedirs(folder, exist_ok=True)
    run_n = next_run(folder)
    log_path = os.path.join(folder, f"run{run_n}.log")

    if a.dry_run:
        ser = FakeSerial(a.speed)
    else:
        try:
            ser = serial.Serial(a.port, 115200, timeout=0.2)
        except Exception as e:
            print(f"\n  Could not open {a.port}: {e}")
            print("  Close any serial monitor (click in its terminal and press Ctrl+C), then try again.")
            print("  If the camera is on a different COM number, add:  --port COM7")
            return 1

    log = Logger(log_path, ser)
    started = datetime.datetime.now().isoformat(timespec="seconds")
    log.write(f"#SCRIPT runner_start scenario={a.scenario} run={run_n} version={VERSION}" + (" dryrun=1" if a.dry_run else ""))
    log.start()
    aborted = False
    try:
        print("\n  Resetting the camera. Stay seated, out of the camera's picture.")
        if a.dry_run:
            ser.reset()
        else:
            ser.dtr = False
            ser.rts = True
            time.sleep(0.15)
            ser.rts = False
        if not log.countdown.wait(12):
            beep("in")
            banner("PRESS THE CAMERA'S RESET BUTTON NOW")
            if not log.countdown.wait(60):
                print("  No countdown seen. Check the camera is plugged in and on the right port.")
                aborted = True
        if not aborted:
            if not log.ref.wait(45 / (a.speed if a.dry_run else 1)):
                print("  The camera did not report a reference picture. Run again.")
                aborted = True
        if not aborted:
            banner("Reference picture taken. Starting in 5 seconds.")
            time.sleep(5 / a.speed)
            t0 = time.monotonic()
            last_status = 0.0
            for s in steps:
                due = t0 + s["t"] / a.speed
                while time.monotonic() < due:
                    time.sleep(0.05)
                    now = time.monotonic()
                    if now - last_status > 30 / a.speed:
                        left = max(0, (total - (now - t0) * a.speed))
                        print(f"  ... about {int(left // 60)}:{int(left % 60):02d} left", flush=True)
                        last_status = now
                if s["mark"]:
                    log.mark(s["mark"])
                    if a.dry_run:
                        parts = s["mark"].split()
                        if parts[0] == "state":
                            ser.state = parts[1] if parts[1] in ("view", "away") else "desk"
                        elif parts[0] == "light":
                            ser.light = parts[1]
                if s["say"]:
                    banner(s["say"])
                if s["beep"] and not a.dry_run:
                    beep(s["beep"])
    except KeyboardInterrupt:
        aborted = True
        print("\n  Stopped by you.")
    finally:
        log.mark("end" if not aborted else "aborted")
        time.sleep(0.5)
        log.close()
        ser.close()

    finished = datetime.datetime.now().isoformat(timespec="seconds")
    meta = {"scenario": a.scenario, "run": run_n, "started": started, "finished": finished, "aborted": aborted,
            "dry_run": a.dry_run, "port": a.port, "light": cfg["light"], "config": {k: v for k, v in cfg.items() if k != "desc"},
            "commit_OccupancyModule": git_hash(root),
            "commit_PoolPlantMonitor": git_hash(os.path.join(root, "..", "PoolPlantMonitor")),
            "python": sys.version.split()[0], "runner_version": VERSION, "note": a.note,
            "timestamps": "computer clock, not sensor acquisition time"}
    with open(os.path.join(folder, f"run{run_n}_meta.json"), "w") as fh:
        json.dump(meta, fh, indent=2)

    if not aborted:
        expected = "both" if "entries" in cfg else ("none" if "away_s" in cfg else "radar_only")
        planned = cfg.get("entries", 0)
        gt = os.path.join(out, "ground_truth.csv")
        new = not os.path.exists(gt)
        with open(gt, "a", newline="") as fh:
            w = csv.writer(fh)
            if new:
                w.writerow(["scenario", "file", "planned_entries", "expected"])
            w.writerow([a.scenario, f"run{run_n}.log", planned, expected])
        banner(f"Saved: {log_path}")
        print("  Nothing else to do for this run. Run the next scenario when ready.")
        return 0
    banner("Run NOT completed. The partial log was kept; tell me and do not use it as a result.")
    return 2


if __name__ == "__main__":
    sys.exit(main())
