"""Figure 5.5: VP05-FAULT. Firmware-injected sensor loss: SENSOR ERROR, recovery and an unbroken clock.

Data: <root>/VP05-FAULT/run1.log (build esp32doit-bench-fault, injection 90 s after boot for 5000 ms).
Run:  python fig_5_5_fault.py <root> <output folder>
"""
import sys
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle
import vibstyle as S

S.apply()
root, out = sys.argv[1], sys.argv[2]
d = S.parse(f"{root}/VP05-FAULT/run1.log")
first = lambda key: next(t for t, m in d["msgs"] if key in m)
t_start, t_stop = first("[FAULT-INJECT] start"), first("[FAULT-INJECT] stop")
t_err, t_rec = first("SENSOR ERROR"), first("recovered")
x0, x1 = t_start - 6, t_rec + 9

fig, (a, c, k) = plt.subplots(3, 1, figsize=(S.TEXT_WIDTH_IN, 2.9), sharex=True,
                              gridspec_kw={"height_ratios": [1.6, 0.42, 0.32], "hspace": 0.18})
# (a) RMS, broken across the gap where no reading was accepted
xs, ys, prev = [], [], None
for t, s, r in d["status"]:
    if prev is not None and t - prev > 1.0:
        xs.append(float("nan")); ys.append(float("nan"))
    xs.append(t); ys.append(r); prev = t
a.plot(xs, ys, color=S.DATA, lw=0.8, zorder=3)
a.axvspan(t_start, t_stop, color=S.SHADE, lw=0, zorder=0)
a.text((t_start + t_stop) / 2, 0.97, "reads rejected\nby firmware", transform=a.get_xaxis_transform(), ha="center",
       va="top", fontsize=S.FS - 1.5, color=S.INK2)
a.set_ylabel("RMS (m/s²)"); a.set_ylim(0, 1.0)
for t0, t1, lab, y, ha, xt in ((t_start, t_err, f"{t_err - t_start:.2f} s to error", 0.30, "left", t_start + 0.1),
                                (t_stop, t_rec, f"{t_rec - t_stop:.2f} s to recovery", 0.30, "left", t_rec + 0.45)):
    a.annotate("", xy=(t1, y), xytext=(t0, y), arrowprops=dict(arrowstyle="<->", color=S.INK, lw=0.7, shrinkA=0, shrinkB=0))
    a.text(xt, y + 0.05, lab, ha=ha, va="bottom", fontsize=S.FS - 1.5, color=S.INK)
for t, lab in ((t_start, "injection start"), (t_stop, "injection stop")):
    a.axvline(t, color=S.INK2, lw=0.6, ls="--")

# (b) reported state: status lines plus the SENSOR ERROR interval from the event lines
c.set_ylim(0, 1); c.set_yticks([]); c.grid(False); c.spines["left"].set_visible(False)
win = [x for x in d["status"] if x0 <= x[0] <= x1]
before = S.runs_of([x for x in win if x[0] < t_err]); before[0] = (x0, before[0][1])
after = S.runs_of([x for x in win if x[0] >= t_rec])
S.state_ribbon(c, before, t_err, min_label_s=4)
c.add_patch(Rectangle((t_start, 0), t_err - t_start, 1, facecolor="none", edgecolor="white", hatch="////", lw=0))
a.text(t_start + 0.1, 0.03, "hatched below: last state\nstill shown", ha="left", va="bottom", fontsize=S.FS - 2.5,
       color=S.INK2)
c.add_patch(Rectangle((t_err, 0), t_rec - t_err, 1, facecolor=S.STATE["SENSOR ERROR"], edgecolor="white", lw=0.4))
c.text((t_err + t_rec) / 2, 0.5, "SENSOR\nERROR", ha="center", va="center", fontsize=S.FS - 2, color="white",
       fontweight="bold", linespacing=0.95)
S.state_ribbon(c, after, x1, min_label_s=4)
c.text(-0.012, 0.5, "State", transform=c.transAxes, ha="right", va="center", fontsize=S.FS, color=S.INK)

# (c) clock lines printed by the RTC module
k.set_ylim(0, 1); k.set_yticks([]); k.grid(False); k.spines["left"].set_visible(False)
k.vlines([t for t in d["rtc"] if x0 <= t <= x1], 0.15, 0.85, color=S.INK2, lw=0.9)
k.text(-0.012, 0.5, "Clock", transform=k.transAxes, ha="right", va="center", fontsize=S.FS, color=S.INK)
k.set_xlim(x0, x1); k.set_xlabel("Time from start of run (s)")
from matplotlib.ticker import MultipleLocator
k.xaxis.set_major_locator(MultipleLocator(2))
S.save(fig, out, "fig5_5_fault")
gaps = [b - a_ for a_, b in zip(d["rtc"], d["rtc"][1:])]
print(f"start->error {t_err-t_start:.3f}; stop->recovered {t_rec-t_stop:.3f}; injection {t_stop-t_start:.3f}; max clock gap {max(gaps):.2f}")
