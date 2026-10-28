"""Figure 5.7: recovery after an invalid pH input, stage 4 firmware (before the fix) against stage 5 (after).

Data: <wq>/raw/status_T3p2.jsonl (stage 4) and <wq>/T4-revised/status_P4_ph.jsonl (stage 5).
Run:  python fig_5_7_ph_recovery.py <evidence/water-quality folder> <output folder>
"""
import sys
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
import vibstyle as S
from wqstyle import load, ribbon, GREY

S.apply()
root, out = sys.argv[1], sys.argv[2]


def after_gap(path):
    r = load(path)
    for k in range(1, len(r)):
        if r[k - 1][1]["state"] == "unavailable" and r[k][1]["state"] == "live" and r[k][1]["simulated"]:
            g0 = k
            while g0 > 0 and r[g0 - 1][1]["state"] == "unavailable":
                g0 -= 1
            seq = [x for x in r[g0: k + 45] if (x[0] - r[k][0]).total_seconds() <= 39.5]
            tt = [(t - r[k][0]).total_seconds() for t, p in seq]
            zn = [p["zoneNow"] if p["state"] == "live" else None for t, p in seq]
            st = [p["status"] if p["state"] == "live" else None for t, p in seq]
            dr = [p["direction"] for t, p in seq]
            vv = [p["value"] for t, p in seq]
            return tt, zn, st, dr, vv


fig, ax = plt.subplots(figsize=(S.TEXT_WIDTH_IN, 2.75))
ax.set_ylim(0, 5.6); ax.grid(False)
rows = ((3.05, f"{root}/raw/status_T3p2.jsonl", "Stage 4 firmware (before the fix)"),
        (0.2, f"{root}/T4-revised/status_P4_ph.jsonl", "Stage 5 firmware (after the fix)"))
for base, path, title in rows:
    tt, zn, st, dr, vv = after_gap(path)
    first = next(i for i, t in enumerate(tt) if t >= 0)
    lab_z = {i: z for i, z in enumerate(zn) if z}
    lab_s = {i: (f"{s} {dr[i]}" if s in ("CAUTION", "WARNING") else s) for i, s in enumerate(st) if s and s != "UNAVAILABLE"}
    ribbon(ax, tt, zn, base + 0.85, h=0.75, labels=lab_z, min_w=4)
    ribbon(ax, tt, st, base, h=0.75, labels=lab_s, min_w=4)
    ins = sorted({v for v in vv[first:] if v is not None}, key=vv[first:].index)
    ax.text(tt[0], base + 2.2, f"{title}: input returns at pH " + ", then ".join(f"{v:.1f}" for v in ins),
            fontsize=S.FS - 1, color=S.INK, fontweight="bold", va="bottom")
    if "4" in title:
        ok = next(t for t, s_ in zip(tt, st) if t > 0 and s_ == "OK")
        ax.annotate("", xy=(ok, base + 1.72), xytext=(0, base + 1.72),
                    arrowprops=dict(arrowstyle="<->", color=S.INK, lw=0.7, shrinkA=0, shrinkB=0))
        ax.text(ok / 2, base + 1.76, f"stale WARNING LOW still reported for {ok:.0f} s", ha="center", va="bottom",
                fontsize=S.FS - 2, color=S.INK)
    else:
        zc = next(t for t, z in zip(tt, zn) if t > 0 and z == "OK")
        sc = next(t for t, s_ in zip(tt, st) if t > 0 and s_ == "OK")
        ax.annotate("", xy=(sc, base + 1.72), xytext=(zc, base + 1.72),
                    arrowprops=dict(arrowstyle="<->", color=S.INK, lw=0.7, shrinkA=0, shrinkB=0))
        ax.text((zc + sc) / 2, base + 1.76, f"normal {sc - zc:.0f} s hold after the zone returns to OK", ha="center", va="bottom",
                fontsize=S.FS - 2, color=S.INK)
ax.set_yticks([3.05 + 1.225, 3.05 + 0.375, 1.425, 0.575])
ax.set_yticklabels(["instant zone", "reported status", "instant zone", "reported status"])
ax.tick_params(axis="y", length=0); ax.spines["left"].set_visible(False)
for base, _, _ in rows:
    ax.plot([0, 0], [base - 0.08, base + 1.68], color=S.INK, lw=0.9)

ax.set_xlim(-10.5, 40.5)
ax.set_xlabel("Time from the first valid reading after the invalid input (s)")
fig.legend(handles=[Patch(fc=S.STATE["OK"], label="OK"), Patch(fc=S.STATE["WARNING"], label="WARNING"),
                    Patch(fc=GREY, hatch="////", ec="white", label="invalid input (unavailable)")],
           loc="lower center", ncol=3, bbox_to_anchor=(0.5, -0.13), handlelength=1.4)
S.save(fig, out, "fig5_7_ph_recovery")
