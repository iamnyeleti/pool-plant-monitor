"""Figure 5.6: W-SW pH decision logic under the firmware's test mode (stage 5 firmware).

(a) scripted pH input, with the values of the boundary steps printed; (b) instant zone, reported status after
the 30 s hold, and the possible-breach flag.
Data: <wq>/T4-revised/status_P4_ph.jsonl, one /api/status reply per second with a laptop time stamp.
Run:  python fig_5_6_ph_logic.py <evidence/water-quality folder> <output folder>
"""
import sys
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
import vibstyle as S

S.apply()
root, out = sys.argv[1], sys.argv[2]
from wqstyle import load, ribbon, BREACH, GREY

rows = load(f"{root}/T4-revised/status_P4_ph.jsonl")
idx = [k for k, (t, p) in enumerate(rows) if p["simulated"]]
rows = rows[idx[0]: idx[-1] + 1]
t0 = rows[0][0]
T = [(t - t0).total_seconds() for t, p in rows]
P = [p for t, p in rows]
val = [p["value"] if p["value"] is not None else float("nan") for p in P]
zone = [p["zoneNow"] if p["state"] == "live" else None for p in P]
stat = [p["status"] for p in P]
dirs = [p["direction"] for p in P]
brk = ["ON" if p["possibleBreach"] else "OFF" for p in P]

fig = plt.figure(figsize=(S.TEXT_WIDTH_IN, 3.9))
gs = fig.add_gridspec(2, 1, height_ratios=[1.7, 1.25], hspace=0.22)
a = fig.add_subplot(gs[0]); b = fig.add_subplot(gs[1], sharex=a)

# (a) input
for lo, hi, s in ((6.9, 7.2, "WARNING"), (7.2, 7.4, "CAUTION"), (7.4, 7.6, "OK"), (7.6, 7.8, "CAUTION"), (7.8, 8.2, "WARNING")):
    a.axhspan(lo, hi, color=S.STATE[s], alpha=0.12, lw=0, zorder=0)
for y in (7.2, 7.8):
    a.axhline(y, color=S.WARNING_LINE, lw=0.8, zorder=1)
a.step(T, val, where="post", color=S.INK, lw=1.2, zorder=3)
for y, lab in ((7.05, "WARNING"), (7.3, "CAUTION"), (7.5, "OK"), (7.7, "CAUTION"), (8.0, "WARNING")):
    a.text(1.01, y, lab, transform=a.get_yaxis_transform(), fontsize=S.FS - 1.5, color=S.INK2, va="center", clip_on=False)
# print the value of each boundary step so 7.19 / 7.20 / 7.21 can be told apart
k = 0
while k < len(val):
    j = k
    while j + 1 < len(val) and val[j + 1] == val[k]:
        j += 1
    v = val[k]
    if v == v and v not in (7.5,) and T[j] - T[k] > 20:
        above = 7.15 < v < 7.5 or v > 7.85
        a.text((T[k] + T[j]) / 2, v + (0.07 if above else -0.07), f"{v:.2f}", ha="center",
               va="bottom" if above else "top", fontsize=S.FS - 2, color=S.INK)
    k = j + 1
spike = next(k for k in range(1, len(val)) if val[k] == 7.9 and val[k - 1] == 7.5)
spike_end = next(k for k in range(spike, len(val)) if val[k] != 7.9)
a.annotate(f"{T[spike_end] - T[spike]:.0f} s spike", xy=(T[spike] + 2, 7.92), xytext=(T[spike] - 6, 8.1),
           ha="right", va="center", fontsize=S.FS - 2, color=S.INK,
           arrowprops=dict(arrowstyle="-", color=S.INK2, lw=0.6))
gap = [k for k, p in enumerate(P) if p["state"] == "unavailable"]
a.axvspan(T[gap[0]], T[gap[-1] + 1], color=S.INK2, alpha=0.18, lw=0, zorder=2)
a.text(T[gap[0]] - 2, 8.1, "invalid input", ha="right", va="center", fontsize=S.FS - 2, color=S.INK2)
a.set_ylim(6.95, 8.2); a.set_xlim(0, T[-1]); a.set_ylabel("Simulated pH input")
plt.setp(a.get_xticklabels(), visible=False)
S.panel(a, "a", x=-0.075)

# (b) ribbons
b.set_ylim(0, 3.55); b.grid(False)
ribbon(b, T, zone, 2.1)
ribbon(b, T, stat, 1.1, labels={k: (dirs[k] if stat[k] in ("CAUTION", "WARNING") else stat[k]) for k in range(len(stat))})
ribbon(b, T, brk, 0.1)
# mark the first hold so the delay between the two rows is explicit
z1 = next(k for k in range(1, len(zone)) if zone[k] != zone[k - 1] and zone[k] is not None)
s1 = next(k for k in range(z1, len(stat)) if stat[k] != stat[z1 - 1])
b.annotate("", xy=(T[s1], 3.15), xytext=(T[z1], 3.15), arrowprops=dict(arrowstyle="<->", color=S.INK, lw=0.7,
                                                                           shrinkA=0, shrinkB=0))
b.text(T[s1] + 3, 3.15, f"{T[s1] - T[z1]:.1f} s hold before the new zone is reported", va="center",
       fontsize=S.FS - 2, color=S.INK)
b.set_yticks([2.5, 1.5, 0.5]); b.set_yticklabels(["instant zone", "reported status", "possible breach"])
b.tick_params(axis="y", length=0); b.spines["left"].set_visible(False)
b.set_xlabel("Time from start of simulated scenario (s)")
S.panel(b, "b", x=-0.075, y=0.93)

fig.legend(handles=[Patch(fc=S.STATE["OK"], label="OK"), Patch(fc=S.STATE["CAUTION"], label="CAUTION"),
                    Patch(fc=S.STATE["WARNING"], label="WARNING"), Patch(fc=BREACH, label="possible breach flagged"),
                    Patch(fc=GREY, hatch="////", ec="white", label="unavailable")],
           loc="lower center", ncol=5, bbox_to_anchor=(0.5, -0.06), handlelength=1.4, columnspacing=1.1)
S.save(fig, out, "fig5_6_ph_logic")
print("scenario s", round(T[-1], 1), "polls", len(T), "first hold", round(T[s1] - T[z1], 1))
