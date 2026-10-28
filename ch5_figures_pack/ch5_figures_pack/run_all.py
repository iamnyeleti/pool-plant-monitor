"""Regenerate every Chapter 5 figure from the bundled data.
Run from this folder:  python run_all.py
Output: figures/  (PDF for LaTeX, 300 dpi PNG for checking)."""
import subprocess
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
s, d, out = here / "scripts", here / "data", here / "figures"
out.mkdir(exist_ok=True)
jobs = [("fig_5_1_sampling.py", d / "vsim"),
        ("fig_5_2_stability.py", d / "vibration"),
        ("fig_5_3_spread.py", d / "vibration"),
        ("fig_5_4_vp04.py", d / "vibration"),
        ("fig_5_5_fault.py", d / "vibration"),
        ("fig_5_6_ph_logic.py", d / "water-quality"),
        ("fig_5_7_ph_recovery.py", d / "water-quality")]
for script, data in jobs:
    print(f"--- {script}")
    r = subprocess.run([sys.executable, script, str(data), str(out)], cwd=s)
    if r.returncode:
        sys.exit(f"{script} failed")
print("All figures written to", out)
