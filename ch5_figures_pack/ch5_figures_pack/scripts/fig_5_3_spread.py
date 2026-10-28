"""Figure 5.3: per-second spread of acceleration (50-sample window) with the motor stopped and running.

VP02-ONOFF sets the threshold by the rule fixed in Chapter 3 (T = sqrt(a b)); VP04-STOP is a separate run
on the frozen threshold. The first 15 s after each switch are excluded (protocol allowance).
Data: <root>/VP02-ONOFF and VP04-STOP run1.log ([STD] lines, one per second).
Run:  python fig_5_3_spread.py <root> <output folder>
"""
import math
import sys
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.ticker import FixedLocator, FixedFormatter, NullFormatter
import vibstyle as S

S.apply()
root, out = sys.argv[1], sys.argv[2]
LAG = 15.0


def groups(d):
    run, stop = [], []
    for kind, win, t0, t1 in S.spans(d["markers"]):
        tgt = run if kind == "running" else stop if kind == "stopped" else None
        if tgt is not None:
            tgt += [s for t, s, n in d["std"] if t0 + LAG <= t < t1]
    return run, stop


v2 = groups(S.parse(f"{root}/VP02-ONOFF/run1.log"))
d4 = S.parse(f"{root}/VP04-STOP/run1.log")
v4 = groups(d4)
a, b = max(v2[1]), min(v2[0])
rule = math.sqrt(a * b)
frozen = float(d4["cfg"]["stopped_std"])

rows = [("VP02-ONOFF, stopped", v2[1], S.STATE["STOPPED"]), ("VP02-ONOFF, running", v2[0], S.DATA),
        ("VP04-STOP, stopped", v4[1], S.STATE["STOPPED"]), ("VP04-STOP, running", v4[0], S.DATA)]
rng = np.random.default_rng(7)
fig, ax = plt.subplots(figsize=(S.TEXT_WIDTH_IN, 2.5))
for i, (lab, vals, col) in enumerate(rows):
    y = len(rows) - 1 - i - (0.35 if i >= 2 else 0)
    ax.scatter(vals, y + rng.uniform(-0.17, 0.17, len(vals)), s=5, color=col, alpha=0.55, linewidth=0, zorder=3)
    ax.text(1.02, y, f"n = {len(vals)}", transform=ax.get_yaxis_transform(), fontsize=S.FS - 1.5,
            color=S.MUTED, va="center", ha="left", clip_on=False)
ys = [len(rows) - 1 - i - (0.35 if i >= 2 else 0) for i in range(len(rows))]
ax.set_yticks(ys); ax.set_yticklabels([r[0] for r in rows])
ax.grid(axis="y", visible=False)
ax.axhline((ys[1] + ys[2]) / 2, color=S.GRID, lw=0.8)

ax.axvline(frozen, color=S.INK, lw=1.0, zorder=2)
ax.axvline(2 * frozen, color=S.INK2, lw=0.8, ls="--", zorder=2)
top = ys[0] + 0.85
ax.text(frozen * 0.96, top, "STOPPED threshold " + r"$\sqrt{ab}$" + f" = {rule:.4f}, set to {frozen:.3f}", ha="right",
        va="center", fontsize=S.FS - 1.5, color=S.INK)
ax.text(2 * frozen * 1.04, top, f"clear level 2 × {frozen:.3f} = {2 * frozen:.3f}", ha="left", va="center", fontsize=S.FS - 1.5, color=S.INK2)
# a and b, marked on the threshold-setting run only
for val, yy, txt, ha in ((a, ys[0], f"a = {a:.4f} (largest stopped)", "right"),
                        (b, ys[1], f"b = {b:.4f} (smallest running)", "left")):
    ax.plot([val, val], [yy - 0.26, yy + 0.26], color=S.INK, lw=1.0, zorder=4)
    ax.text(val, yy + 0.32, txt, ha=ha, va="bottom", fontsize=S.FS - 1.5, color=S.INK)
ax.text(0.007, (ys[0] + ys[1]) / 2, "sets threshold", fontsize=S.FS - 1.5, color=S.INK2, rotation=90,
        va="center", ha="center")
ax.text(0.007, (ys[2] + ys[3]) / 2, "verifies it", fontsize=S.FS - 1.5, color=S.INK2, rotation=90,
        va="center", ha="center")

ax.set_xscale("log"); ax.set_xlim(0.005, 1.2); ax.set_ylim(ys[-1] - 0.5, top + 0.25)
ax.xaxis.set_major_locator(FixedLocator([0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1]))
ax.xaxis.set_major_formatter(FixedFormatter(["0.01", "0.02", "0.05", "0.1", "0.2", "0.5", "1"]))
ax.xaxis.set_minor_formatter(NullFormatter())
ax.set_xlabel("Spread: standard deviation of acceleration over the 50-sample window (m/s², log scale)")
S.save(fig, out, "fig5_3_spread")
