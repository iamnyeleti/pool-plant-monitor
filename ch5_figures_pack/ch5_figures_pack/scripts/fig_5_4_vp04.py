"""Figure 5.4: VP04-STOP. RMS, spread and reported state through the three pump-window phases.

Phase A: window includes the current time; B: window excludes it; C: no window set.
Data: <root>/VP04-STOP/run1.log (threshold 0.102 frozen from VP02-ONOFF).
Run:  python fig_5_4_vp04.py <root> <output folder>
"""
import sys
import matplotlib.pyplot as plt
import vibstyle as S

S.apply()
root, out = sys.argv[1], sys.argv[2]
d = S.parse(f"{root}/VP04-STOP/run1.log")
cfg = d["cfg"]
base = float(cfg["baseline"]); thr = float(cfg["stopped_std"])
sp = S.spans(d["markers"])
t_end = max(t for t, _, _ in d["status"])

fig, (a, b, c) = plt.subplots(3, 1, figsize=(S.TEXT_WIDTH_IN, 3.9), sharex=True,
                              gridspec_kw={"height_ratios": [1.5, 1.5, 0.45], "hspace": 0.12})
for kind, win, t0, t1 in sp:
    if kind == "stopped":
        for ax in (a, b):
            ax.axvspan(t0, t1, color=S.SHADE, lw=0, zorder=0)

# phase labels across the top
phase = {"in": "A  window includes now", "out": "B  window excludes now", "none": "C  no window"}
seen = []
for kind, win, t0, t1 in sp:
    if win not in seen:
        seen.append(win)
        ends = [x[3] for x in sp if x[1] == win]
        a.text((t0 + max(ends)) / 2, 1.18, phase.get(win, win), ha="center", va="bottom", fontsize=S.FS - 1,
               color=S.INK, transform=a.get_xaxis_transform())
        a.annotate("", xy=(t0 + 2, 1.14), xytext=(max(ends) - 2, 1.14), xycoords=a.get_xaxis_transform(),
                   arrowprops=dict(arrowstyle="<->", color=S.MUTED, lw=0.6))

# (a) RMS
a.plot([x[0] for x in d["status"]], [x[2] for x in d["status"]], color=S.DATA, lw=0.55, zorder=3)
a.axhline(base, color=S.INK2, lw=0.8)
a.axhline(base * float(cfg["caution_mult"]), color=S.CAUTION_LINE, lw=0.8, ls="--")
a.text(t_end + 4, base, "baseline", fontsize=S.FS - 1.5, color=S.INK2, va="center", clip_on=False)
a.text(t_end + 4, base * float(cfg["caution_mult"]), "CAUTION", fontsize=S.FS - 1.5, color=S.INK2, va="center", clip_on=False)
a.set_ylabel("RMS (m/s²)"); a.set_ylim(0, 1.25)
stopped_rms = [r for t, s_, r in d["status"] if s_ == "STOPPED"]
s0 = next(x for x in sp if x[0] == "stopped")
a.text((s0[2] + s0[3]) / 2, 0.16, f"motor off: RMS stays near {sum(stopped_rms) / len(stopped_rms):.2f}, not 0",
       ha="center", va="center", fontsize=S.FS - 2, color=S.INK)
a.text(0.995, 0.04, "shaded: motor switched off", transform=a.transAxes, ha="right", va="bottom",
       fontsize=S.FS - 2, color=S.INK2)
S.panel(a, "a", y=0.92, x=-0.075)

# (b) spread
b.semilogy([x[0] for x in d["std"]], [x[1] for x in d["std"]], color=S.DATA, lw=0.7, zorder=3)
b.axhline(thr, color=S.INK, lw=0.9)
b.axhline(2 * thr, color=S.INK2, lw=0.8, ls="--")
b.text(t_end + 4, thr, f"STOPPED {thr:.3f}", fontsize=S.FS - 1.5, color=S.INK, va="center", clip_on=False)
b.text(t_end + 4, 2 * thr, f"clear {2 * thr:.3f}", fontsize=S.FS - 1.5, color=S.INK2, va="center", clip_on=False)
b.set_ylabel("Spread (m/s²)")
from matplotlib.ticker import FixedLocator, FixedFormatter, NullFormatter
b.yaxis.set_major_locator(FixedLocator([0.03, 0.1, 0.3, 1])); b.yaxis.set_major_formatter(FixedFormatter(["0.03", "0.1", "0.3", "1"]))
b.yaxis.set_minor_formatter(NullFormatter()); b.set_ylim(0.012, 1.5)
S.panel(b, "b", y=0.92, x=-0.075)

# (c) reported state
c.set_ylim(0, 1); c.set_yticks([]); c.grid(False)
for side in ("left", "bottom"):
    c.spines[side].set_visible(side == "bottom")
S.state_ribbon(c, S.runs_of(d["status"]), t_end, min_label_s=25)
c.text(-0.012, 0.5, "State", transform=c.transAxes, ha="right", va="center", fontsize=S.FS, color=S.INK)
c.set_xlim(0, t_end); c.set_xlabel("Time from start of run (s)")

# timing annotations from the markers and the first/last STOPPED line
stop_in = next(t0 for kind, win, t0, t1 in sp if kind == "stopped" and win == "in")
run_in2 = next(t0 for kind, win, t0, t1 in sp if kind == "running" and win == "in" and t0 > stop_in)
stp = [t for t, s, _ in d["status"] if s == "STOPPED"]
b.annotate("", xy=(stp[0], 0.0175), xytext=(stop_in, 0.0175), arrowprops=dict(arrowstyle="<->", color=S.INK, lw=0.7, shrinkA=0, shrinkB=0))
b.text(stp[0] + 3, 0.0175, f"{stp[0] - stop_in:.1f} s, stop prompt to first STOPPED", ha="left", va="center",
       fontsize=S.FS - 1.5, color=S.INK)
S.save(fig, out, "fig5_4_vp04")
print(f"stop->first STOPPED {stp[0]-stop_in:.3f} s; switch-on->last STOPPED {stp[-1]-run_in2:.3f} s; STOPPED lines {len(stp)}")
