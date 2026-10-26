#!/usr/bin/env python3
"""
run_vib.py

Runs ONE vibration test for you. You only act when it tells you.

    .\\tools\\vib.cmd VP02-ONOFF

It opens the DOIT board's serial port (COM5), resets the board, logs everything with a
timestamp to evidence\\vibration\\<scenario>\\runN.log, prints a large instruction and beeps
when you must do something (fan on, fan off, tap), writes #SCRIPT markers into the log at
those moments, and sends the PUMP schedule commands itself. Run details (git commit, the
firmware's own [CFG] line) go to runN_meta.json.

Close any serial monitor first (the port can only be used by one program).

Scenarios (flash the build named for each BEFORE running):
  VP01-CAL      calibration, build esp32doit-calibrate
  VP01-STAB     5 minutes undisturbed after calibration, build esp32doit-bench
  VP02-ONOFF    fan on and off, STOPPED spread data, build esp32doit-bench
  VP03-DIST     brief, sustained and recurring disturbances, build esp32doit-bench
  VP04-STOP     STOPPED and pump window verification, build esp32doit-bench
  VP05-FAULT    simulated sensor loss at 90 s, build esp32doit-bench-fault
  VP05-NOSENSOR boot with the sensor disconnected (power OFF while you unplug it)
  VP07-ALL      acquisition timing with every module running, build esp32doit-all

Options: --port COM5  --out <folder>  --note "text"  --yes  --force
         --dry-run [--speed 20]  synthetic test without hardware (NOT evidence)
"""
import argparse
import datetime
import json
import os
import random
import re
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

VERSION = "1.1"


def beeps_demo():
    """Plays every VP07-ALL sound with its meaning, so you can learn them before the run. Needs no board."""
    items = [("setup", "1 LONG low beep = SETUP step (start, restart the camera, open the page)"),
             ("shot", "1 short high beep = take a SCREENSHOT"),
             ("walkin", "2 beeps, rising (low then high) = WALK INTO the camera's view"),
             ("walkout", "2 beeps, falling (high then low) = WALK OUT of the radar's range"),
             ("end", "3 short beeps = FINISHED")]
    print("\nThe five sounds, each played twice. Turn the laptop volume up until you can hear them from where you will stand.\n")
    for kind, meaning in items:
        for _ in range(2):
            print("  " + meaning, flush=True)
            beep(kind)
            time.sleep(1.8)
        time.sleep(1.0)
    print("\nDone. Run it again if you want to hear them again.")


def beep(kind):
    pat = {"in": [(1000, 200), (1000, 200)], "out": [(500, 350)], "end": [(800, 250)] * 3,
           "tap": [(1500, 120)],
           # VP07-ALL sounds (one meaning each; see the run sheet)
           "setup": [(600, 1000)],                    # 1 long low beep: do a setup step
           "shot": [(1500, 160)],                     # 1 short high beep: take a SCREENSHOT
           "walkin": [(800, 180), (1300, 180)],       # 2 beeps, low then high (rising): WALK INTO the camera's view
           "walkout": [(1300, 180), (800, 180)]       # 2 beeps, high then low (falling): WALK OUT of the radar's range
           }.get(kind, [])
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


def outside_window_cmd():
    """A pump window that does not include the current time (6 hours ahead, 1 minute long)."""
    start = datetime.datetime.now() + datetime.timedelta(hours=6)
    end = start + datetime.timedelta(minutes=1)
    return f"PUMP {start:%H:%M} {end:%H:%M}"


def add(steps, t, mark=None, say=None, beep_kind=None, cmd=None):
    steps.append({"t": t, "mark": mark, "say": say, "beep": beep_kind, "cmd": cmd})


def build(name):
    st = []
    if name == "VP01-CAL":
        pre = ["Mount the sensor with tape, photograph the rig, and note which batteries are in the fan.",
               "Switch the fan ON at the speed you will use for ALL the tests. Do not change the speed afterwards.",
               "Make sure esp32doit-calibrate is flashed on the board."]
        add(st, 0, "state calibration", "CALIBRATION RUNNING (20 s warm-up, then 90 s recording). Leave the fan and the rig alone.")
        add(st, 150, "end", "FINISHED. Give the baseline number to Claude Code.", "end")
        return dict(env="esp32doit-calibrate", cal=1, pre=pre, steps=st, desc="Calibration of the running reference")
    if name == "VP01-STAB":
        pre = ["The fan must be ON at the calibrated speed. Do not touch the rig for 5 minutes.",
               "Make sure esp32doit-bench (with the new baseline) is flashed."]
        add(st, 0, "state running win=none", "FAN ON AND UNTOUCHED. Do nothing for 5 minutes.", None, "PUMP CLEAR")
        add(st, 300, "end", "FINISHED.", "end")
        return dict(env="esp32doit-bench", cal=0, pre=pre, steps=st, desc="Five minutes undisturbed after calibration")
    if name == "VP02-ONOFF":
        pre = ["Start with the fan ON. You will switch it off and on when told (about every 90 s).",
               "Do not touch the rig otherwise. Make sure esp32doit-bench is flashed."]
        add(st, 0, "state running win=in", "FAN ON. Leave it running.", None, "PUMP 00:00 23:59")
        for i, t in enumerate((90, 180, 270, 360)):
            if i % 2 == 0:
                add(st, t, "state stopped win=in", "SWITCH THE FAN OFF NOW.", "out")
            else:
                add(st, t, "state running win=in", "SWITCH THE FAN ON NOW.", "in")
        add(st, 420, "end", "FINISHED.", "end")
        return dict(env="esp32doit-bench", cal=0, pre=pre, steps=st, desc="Fan on and off (spread data for the STOPPED threshold)")
    if name == "VP03-DIST":
        pre = ["Fan ON at the calibrated speed. You will tap the MOUNTING SURFACE gently (never the fan or any moving part).",
               "Use the same pen and the same gentle strength every time. Make sure esp32doit-bench is flashed."]
        add(st, 0, "state running win=none", "FAN ON. Leave it running for 60 s.", None, "PUMP CLEAR")
        add(st, 60, "state brief n=1", "ONE gentle tap on the mounting surface NOW.", "tap")
        add(st, 63, "state running win=none", "Hands off. Wait.")
        add(st, 100, "state sustained n=1", "TAP gently about once a second for 30 s, starting NOW.", "in")
        add(st, 130, "state running win=none", "STOP tapping. Hands off.", "out")
        for i, t in enumerate((190, 220, 250), 1):
            add(st, t, f"state recurring n={i}", f"BURST {i} of 3: tap 5 times, about once a second, NOW.", "in")
            add(st, t + 6, "state running win=none", "Hands off.")
        add(st, 330, "end", "FINISHED.", "end")
        return dict(env="esp32doit-bench", cal=0, pre=pre, steps=st, desc="Brief, sustained and recurring disturbances at a fixed fan speed")
    if name == "VP04-STOP":
        pre = ["Start with the fan ON. The runner changes the pump window itself.",
               "Make sure esp32doit-bench is flashed with the STOPPED threshold that was set from the VP02 data."]
        add(st, 0, "state running win=in", "PHASE A (pump window includes now). FAN ON.", None, "PUMP 00:00 23:59")
        add(st, 60, "state stopped win=in", "SWITCH THE FAN OFF NOW.", "out")
        add(st, 180, "state running win=in", "SWITCH THE FAN ON NOW.", "in")
        add(st, 240, "state stopped win=out", "PHASE B (pump window does NOT include now). SWITCH THE FAN OFF NOW.", "out", outside_window_cmd())
        add(st, 360, "state running win=out", "SWITCH THE FAN ON NOW.", "in")
        add(st, 420, "state stopped win=none", "PHASE C (no pump window set). SWITCH THE FAN OFF NOW.", "out", "PUMP CLEAR")
        add(st, 510, "state running win=none", "SWITCH THE FAN ON NOW.", "in")
        add(st, 560, "end", "FINISHED.", "end")
        return dict(env="esp32doit-bench", cal=0, pre=pre, steps=st, desc="STOPPED and pump-window verification")
    if name == "VP05-FAULT":
        pre = ["Fan ON. Do nothing. The firmware simulates a sensor loss at 90 s for 5 s by itself.",
               "Make sure esp32doit-bench-fault is flashed."]
        add(st, 0, "state running win=none", "FAN ON. Hands off. A simulated sensor loss happens at about 90 s.", None, "PUMP CLEAR")
        add(st, 300, "end", "FINISHED.", "end")
        return dict(env="esp32doit-bench-fault", cal=0, pre=pre, steps=st, desc="Simulated sensor loss and recovery")
    if name == "VP05-NOSENSOR":
        pre = ["1. UNPLUG THE USB so the board has no power.",
               "2. Disconnect the MPU6050 (unplug the sensor from the board). Never disconnect or reconnect it while powered.",
               "3. Plug the USB back in. The RTC may stay connected.",
               "Make sure esp32doit-bench is flashed."]
        add(st, 0, "state nosensor", "SENSOR DISCONNECTED. Do nothing for 90 s.")
        add(st, 90, "end", "FINISHED. UNPLUG THE USB BEFORE you reconnect the sensor.", "end")
        return dict(env="esp32doit-bench", cal=0, pre=pre, steps=st, desc="Boot with the sensor disconnected")
    if name == "VP07-ALL":
        pre = ["1. Flash esp32doit-all first (every module on). The pH and the turbidity are NOT calibrated in this run, on purpose.",
               "2. Fan ON at the calibrated speed. Write down the dial setting and the batteries. Do not touch the fan or the rig during the run.",
               "3. The status logger window on the laptop is ALREADY RUNNING and waiting for the board (start it before you press Enter here).",
               "4. The iPad is joined to PoolPlantRadar, Safari open and ready. Do not open the dashboard yet.",
               "5. Your AWAY SPOT: inside the radar's range (closer than 2.25 m) where the camera cannot see you. Checked on the camera page BEFORE you start.",
               "6. START POSITION: after you press Enter, walk to the camera's USB cable and stand next to it with the iPad. You restart the camera there at the first long beep."]
        add(st, 0, "state running win=none", "FAN ON. Hands off the rig for the whole run (6 minutes). Stand NEXT TO THE CAMERA'S USB CABLE and wait for the first long beep.")
        add(st, 10, "load camera_reset", "NOW: UNPLUG the camera board's USB cable, count 3 seconds, PLUG IT BACK IN. Press no button. Then walk straight to your AWAY SPOT and stay out of the camera's view until told. Its 15-second countdown starts when it powers up.", "setup")
        add(st, 45, "load phone_page", "NOW: at your AWAY SPOT, open http://192.168.4.1/ on the iPad. Leave the page open with the iPad awake.", "setup")
        add(st, 60, "shot 1 radar_only", "SCREENSHOT 1 on the iPad now (the radar sees you, the camera does not: it should say radar only).", "shot")
        add(st, 70, "event enter n=1", "EVENT 1: walk INTO the camera's view and stand still for 20 seconds.", "walkin")
        add(st, 85, "shot 2 agree", "SCREENSHOT 2 on the iPad now, while you are still in view.", "shot")
        add(st, 90, "event leave n=1", "Walk OUT of the radar's range completely (more than 3 m from the radar). Stay out.", "walkout")
        add(st, 120, "shot 3 all_clear", "SCREENSHOT 3 on the iPad now (the radar should be clear).", "shot")
        add(st, 200, "event enter n=2", "EVENT 2: walk INTO the camera's view and stand still for 20 seconds.", "walkin")
        add(st, 215, "shot 4 agree", "SCREENSHOT 4 on the iPad now, while you are still in view.", "shot")
        add(st, 220, "event leave n=2", "Walk OUT of the radar's range completely. Stay out.", "walkout")
        add(st, 300, "shot 5 top_banner", "Scroll the page to the TOP and take SCREENSHOT 5 (the system status banner).", "shot")
        add(st, 320, "shot 6 engineering", "On the water card tap 'Show engineering values' and take SCREENSHOT 6. Then scroll to the vibration card and tap 'Download readings (CSV)'.", "shot")
        add(st, 360, "end", "FINISHED. The logger stops by itself. You can close the page on the iPad.", "end")
        return dict(env="esp32doit-all", cal=0, pre=pre, steps=st, desc="All modules running: timing, honest states, radar and camera agreement")
    raise SystemExit("Unknown scenario: " + name)


SCENARIO_NAMES = ["VP01-CAL", "VP01-STAB", "VP02-ONOFF", "VP03-DIST", "VP04-STOP", "VP05-FAULT", "VP05-NOSENSOR", "VP07-ALL"]


class FakeSerial:
    """Synthetic board for --dry-run. Values are invented to test the pipeline. NOT evidence."""

    def __init__(self, speed, cal):
        self.speed, self.cal = speed, cal
        self.buf = bytearray()
        self.lock = threading.Lock()
        self.state, self.win = "running", "none"
        self.since = time.time()
        self.rng = random.Random(3)
        self.stop = threading.Event()
        self.thread = None

    def set(self, state, win):
        if state != self.state:
            self.since = time.time()
        self.state, self.win = state, win

    def _emit(self, s):
        with self.lock:
            self.buf += (s + "\r\n").encode()

    def write(self, data):
        cmd = data.decode().strip()
        if cmd.startswith("PUMP"):
            self._emit(f"[Vibration] Pump command accepted: {cmd}")

    def reset(self):
        self.stop.set()
        if self.thread:
            self.thread.join()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def _sleep(self, s):
        end = time.time() + s / self.speed
        while not self.stop.is_set():
            rem = end - time.time()
            if rem <= 0:
                break
            time.sleep(min(rem, 0.002))

    def _run(self):
        self._emit("Pool Plant Monitor starting...")
        self._emit(f"[CFG] dlpf=0x00 baseline=0.7854 caution_mult=1.2 warning_mult=1.5 window=6000 fraction=0.5 sample_delay_ms=20 calibration={self.cal} stopped_std=0.05 stopped_hold_s=60 build=DRYRUN")
        k = 0
        while not self.stop.is_set():
            self._sleep(0.25)
            k += 1
            st, win = self.state, self.win
            held = (time.time() - self.since) * self.speed
            if st == "nosensor":
                if k % 20 == 1:
                    self._emit("[Vibration] SENSOR ERROR: init failed")
                continue
            if st in ("running", "calibration"):
                rms, std, status = 0.78 + self.rng.uniform(-0.05, 0.05), 0.30, "OK"
            elif st == "stopped":
                rms, std = 0.27, 0.031
                status = "STOPPED" if (win == "in" and held > 60) else "OK"
            else:
                rms, std, status = 1.3, 0.5, "CAUTION"
            if win == "out":
                status = "OFF SCHEDULE"
            self._emit(f"[Vibration] Status: {status} | RMS: {rms:.3f} | Magnitude: 10.0 | X: 0.1 Y: 0.2 Z: 10.0")
            if k % 4 == 0:
                self._emit(f"[STD] std={std:.4f} n=50 stopped={1 if status == 'STOPPED' else 0}")
            if k % 240 == 0:
                self._emit("[TIMING] n=2650 mean_ms=22.09 min_ms=20.9 max_ms=34.0 std_ms=1.1 over_50ms=0 read_errors=0")
            if self.cal and k >= 440 and k % 8 == 0:
                self._emit("===== CALIBRATION COMPLETE =====")
                self._emit("Average RMS: 0.7123")

    def read(self, n):
        time.sleep(0.03)
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
        self.cfg = threading.Event()
        self.halt = threading.Event()
        self.cfg_line = ""
        self.last_status = ""
        self.last_std = ""
        self.cal_lines = []
        self.avg_rms = None
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
        if line.startswith("[CFG]"):
            self.cfg_line = line
            self.cfg.set()
        elif "[Vibration] Status:" in line:
            self.last_status = line
        elif line.startswith("[STD]"):
            self.last_std = line
        elif "CALIBRATION COMPLETE" in line or "Average RMS" in line or "[CAL]" in line:
            m = re.search(r"Average RMS:\s*([\d.]+)", line)
            if m:
                self.avg_rms = m.group(1)
            if line not in self.cal_lines:      # the firmware repeats these lines every 2 s
                self.cal_lines.append(line)
                print("  board: " + line, flush=True)
        elif "SENSOR ERROR" in line or "[FAULT-INJECT]" in line or "[EVENT]" in line:
            print("  board: " + line[:100], flush=True)

    def _run(self):
        buf = b""
        while not self.halt.is_set():
            try:
                data = self.ser.read(512)
            except Exception as e:
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


def short_status(line):
    m = re.search(r"Status: ([A-Z ]+?) \| RMS: ([\d.]+)", line)
    return f"{m.group(1)} RMS {m.group(2)}" if m else ""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("scenario", choices=SCENARIO_NAMES)
    ap.add_argument("--port", default="COM5")
    ap.add_argument("--out", default=None)
    ap.add_argument("--note", default="")
    ap.add_argument("--yes", action="store_true")
    ap.add_argument("--force", action="store_true", help="continue even if the [CFG] line is missing or from the wrong build")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--beeps-demo", action="store_true", help="play the VP07-ALL sounds with their meaning and stop (no board needed)")
    ap.add_argument("--speed", type=float, default=1.0)
    a = ap.parse_args()
    if a.beeps_demo:
        beeps_demo()
        return 0

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = a.out or os.path.join(root, "evidence", "vibration")
    if a.dry_run:
        out = os.path.join(out, "_dryrun")
    else:
        a.speed = 1.0
    sc = build(a.scenario)
    steps = sc["steps"]
    total = steps[-1]["t"]

    if a.dry_run:
        banner("DRY RUN: SYNTHETIC DATA. THIS IS NOT EVIDENCE.")
    banner(f"{a.scenario}: {sc['desc']}")
    mins = int((total + 40) // 60)
    print(f"  About {mins} to {mins + 1} minutes in total. Flash the build: {sc['env']}")
    for line in sc["pre"]:
        print("  - " + line)
    if not a.dry_run and serial is None:
        print("  pyserial is missing. Run:  pip install pyserial")
        return 1
    if not (a.yes or a.dry_run):
        input("\n  Done all of the above? Press Enter to start... ")

    folder = os.path.join(out, a.scenario)
    os.makedirs(folder, exist_ok=True)
    n = 1
    while os.path.exists(os.path.join(folder, f"run{n}.log")):
        n += 1
    log_path = os.path.join(folder, f"run{n}.log")

    if a.dry_run:
        ser = FakeSerial(a.speed, sc["cal"])
        if a.scenario == "VP05-NOSENSOR":
            ser.set("nosensor", "none")   # a board booted with no sensor never prints status lines
    else:
        try:
            ser = serial.Serial(a.port, 115200, timeout=0.2)
        except Exception as e:
            print(f"\n  Could not open {a.port}: {e}")
            print("  Close any serial monitor (click in its terminal and press Ctrl+C), then try again.")
            print("  If the board is on a different COM number, add:  --port COM6")
            return 1

    log = Logger(log_path, ser)
    started = datetime.datetime.now().isoformat(timespec="seconds")
    log.write(f"#SCRIPT runner_start scenario={a.scenario} run={n} version={VERSION}" + (" dryrun=1" if a.dry_run else ""))
    log.start()
    aborted, reason = False, ""

    def send(cmd):
        log.mark(f"cmd {cmd}")
        if a.dry_run:
            ser.write((cmd + "\n").encode())
        else:
            ser.write((cmd + "\n").encode())

    try:
        print("\n  Resetting the board...")
        if a.dry_run:
            ser.reset()
        else:
            ser.dtr = False
            ser.rts = True
            time.sleep(0.15)
            ser.rts = False
        if not log.cfg.wait(25 if not a.dry_run else 10):
            beep("in")
            banner("NO [CFG] LINE SEEN. PRESS THE BOARD'S EN (RESET) BUTTON NOW")
            if not log.cfg.wait(30) and not a.force:
                print("  Still no [CFG] line: the board may have the OLD firmware, or the wrong port.")
                print("  Flash " + sc["env"] + " and try again (or add --force).")
                aborted, reason = True, "no [CFG] line"
        if not aborted:
            cal = re.search(r"calibration=(\d)", log.cfg_line)
            if cal and int(cal.group(1)) != sc["cal"] and not a.force:
                print(f"  Wrong build: this scenario needs calibration={sc['cal']} but the board reports calibration={cal.group(1)}.")
                print("  Flash " + sc["env"] + " and try again.")
                aborted, reason = True, "wrong build"
        if not aborted:
            print("  Board: " + log.cfg_line[:140])
            time.sleep(1.0 / a.speed)
            banner("Starting.")
            t0 = time.monotonic()
            last = 0.0
            for s in steps:
                due = t0 + s["t"] / a.speed
                while time.monotonic() < due:
                    time.sleep(0.05)
                    now = time.monotonic()
                    if now - last > 5 / a.speed:
                        left = max(0, total - (now - t0) * a.speed)
                        info = short_status(log.last_status)
                        std = log.last_std.replace("[STD] ", "")
                        print(f"  ... {int(left // 60)}:{int(left % 60):02d} left | {info} | {std}", flush=True)
                        last = now
                if s["cmd"]:
                    send(s["cmd"])
                if s["mark"]:
                    log.mark(s["mark"])
                    if a.dry_run:
                        parts = s["mark"].split()
                        if parts[0] == "state":
                            win = next((p.split("=")[1] for p in parts if p.startswith("win=")), ser.win)
                            ser.set(parts[1] if parts[1] in ("running", "stopped", "calibration", "nosensor") else "brief", win)
                if s["say"]:
                    banner(s["say"])
                if s["beep"] and not a.dry_run:
                    beep(s["beep"])
    except KeyboardInterrupt:
        aborted, reason = True, "stopped by user"
        print("\n  Stopped by you.")
    finally:
        log.mark("end" if not aborted else "aborted")
        time.sleep(0.5)
        log.close()
        ser.close()

    finished = datetime.datetime.now().isoformat(timespec="seconds")
    meta = {"scenario": a.scenario, "run": n, "started": started, "finished": finished, "aborted": aborted,
            "abort_reason": reason, "dry_run": a.dry_run, "port": a.port, "expected_build": sc["env"],
            "cfg_line": log.cfg_line, "commit_PoolPlantMonitor": git_hash(root), "python": sys.version.split()[0],
            "runner_version": VERSION, "note": a.note, "timestamps": "computer clock, not sensor acquisition time",
            "cal_lines": log.cal_lines, "baseline_from_calibration": log.avg_rms}
    with open(os.path.join(folder, f"run{n}_meta.json"), "w") as fh:
        json.dump(meta, fh, indent=2)

    if not aborted:
        banner(f"Saved: {log_path}")
        if log.avg_rms:
            banner(f"BASELINE = {log.avg_rms}   (the calibration's Average RMS). Give this number to Claude Code.")
        elif a.scenario == "VP01-CAL":
            print("  No 'Average RMS' line was seen. Open the log, find CALIBRATION COMPLETE, and copy me the lines around it.")
        print("  Nothing else to do for this run.")
        return 0
    banner("Run NOT completed. The partial log was kept; do not use it as a result.")
    return 2


if __name__ == "__main__":
    sys.exit(main())
