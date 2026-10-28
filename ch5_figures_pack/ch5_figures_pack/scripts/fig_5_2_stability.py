"""Figure 5.2: VP01-STAB undisturbed RMS, and running level relative to the baseline in each run.

Data: <root>/VP01-STAB, VP02-ONOFF, VP04-STOP run1.log (baseline 0.5356 m/s^2 from VP01-CAL).
Running segments exclude the first 15 s after each switch (protocol allowance), as in derive_vib_extras.py.
Run:  python fig_5_2_stability.py <root> <output folder>
"""
import statistics as st
import sys
import matplotlib.pyplot as plt
import vibstyle as S

S.apply()
root, out = sys.argv[1], sys.argv[2]
LAG = 15.0
stab = S.parse(f"{root}/VP01-STAB/run1.log")
base = float(stab["cfg"]["baseline"])
cau, warn = base * float(stab["cfg"]["caution_mult"]), base * float(stab["cfg"]["warning_mult"])


def running(d, whole=False):
    if whole:
        return [r for _, _, r in d["status"]]
    v = []
    for kind, win, t0, t1 in S.spans(d["markers"]):
        if kind == "running":
            v += [r for t, _, r in d["status"] if t0 + LAG <= t < t1]
    return v


fig, (a, b) = plt.subplots(1, 2, figsize=(S.TEXT_WIDTH_IN, 2.6),
                           gridspec_kw={"width_ratios": [1.9, 1], "wspace": 0.55})

# (a) VP01-STAB trace
t = [x[0] for x in stab["status"]]; r = [x[2] for x in stab["status"]]
a.plot(t, r, color=S.DATA, lw=0.6, zorder=3)
ct = [(x[0], x[2]) for x in stab["status"] if x[1] == "CAUTION"]
a.scatter([x for x, _ in ct], [y for _, y in ct], s=9, color=S.STATE["CAUTION"], edgecolor=S.INK, linewidth=0.3,
          zorder=4, label=f"CAUTION line ({len(ct)} of {len(r)})")
a.axhline(base, color=S.INK2, lw=0.8, label=f"baseline {base:.4f}")
a.axhline(cau, color=S.CAUTION_LINE, lw=0.8, ls="--", label=f"CAUTION {cau:.3f} (1.2×)")
a.axhline(warn, color=S.WARNING_LINE, lw=0.8, ls="--", label=f"WARNING {warn:.3f} (1.5×)")
a.set_xlim(0, max(t)); a.set_ylim(0.2, 0.9)
a.set_xlabel("Time from start of run (s)"); a.set_ylabel("RMS (m/s²)")
h, l = a.get_legend_handles_labels()
order = [1, 2, 3, 0]
a.legend([h[i] for i in order], [l[i] for i in order], loc="lower left", bbox_to_anchor=(0, 1.0), ncol=2,
         handletextpad=0.4, columnspacing=1.2, borderaxespad=0.2, fontsize=S.FS - 1)
S.panel(a, "a", y=1.16)

# (b) mean running RMS / baseline, with +/- 1 standard deviation of the printed values
runs = [("VP01-STAB", stab, True), ("VP02-ONOFF", S.parse(f"{root}/VP02-ONOFF/run1.log"), False),
        ("VP04-STOP", S.parse(f"{root}/VP04-STOP/run1.log"), False)]
labels = []
for i, (name, d, whole) in enumerate(runs):
    v = [x / base for x in running(d, whole)]
    m, s = st.mean(v), st.pstdev(v)
    y = len(runs) - 1 - i
    b.errorbar(m, y, xerr=s, fmt="o", color=S.DATA, ms=4.5, elinewidth=1.0, capsize=2.5, zorder=3)
    b.text(m, y - 0.2, f"{m:.2f}", ha="center", va="top", fontsize=S.FS - 1, color=S.INK, zorder=4,
           bbox=dict(facecolor="white", edgecolor="none", pad=0.6))
    labels.append(f"{name}\nn = {len(v)}")
b.set_yticks(range(len(runs))); b.set_yticklabels(labels[::-1], fontsize=S.FS - 1)
for x, lab, c, ls in ((1.0, "baseline", S.INK2, "-"), (1.2, "CAUTION", S.CAUTION_LINE, "--"), (1.5, "WARNING", S.WARNING_LINE, "--")):
    b.axvline(x, color=c, ls=ls, lw=0.8, zorder=2)
    b.text(x - 0.012, 0.98, lab, transform=b.get_xaxis_transform(), ha="right", va="top", rotation=90,
           fontsize=S.FS - 1.5, color=S.INK2)
b.set_xlim(0.5, 1.6); b.set_ylim(-0.7, len(runs) + 0.35)
b.grid(axis="y", visible=False)
b.set_xlabel("Running RMS / baseline")
S.panel(b, "b", x=-0.42, y=1.16)
S.save(fig, out, "fig5_2_stability")
