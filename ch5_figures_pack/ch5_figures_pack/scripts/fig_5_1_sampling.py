"""Figure 5.1: V-SIM-01. Effect of sampling, filter model and vibration direction on the RMS indicator.

Data: <sim>/results/frequency_sweep.csv written by vibration_sim.py.
Run:  python fig_5_1_sampling.py <sim folder> <output folder>
"""
import csv
import sys
import matplotlib.pyplot as plt
import vibstyle as S

S.apply()
sim, out = sys.argv[1], sys.argv[2]
rows = list(csv.DictReader(open(f"{sim}/results/frequency_sweep.csv")))
fs = float(rows[0]["assumed_sample_rate_hz"])
nyq = fs / 2


def series(direction, cutoff, col):
    r = [x for x in rows if x["vibration_direction"] == direction and float(x["model_filter_cutoff_hz"]) == cutoff]
    r.sort(key=lambda x: float(x["input_frequency_hz"]))
    return [float(x["input_frequency_hz"]) for x in r], [float(x[col]) for x in r], r


fig, (a, b) = plt.subplots(1, 2, figsize=(S.TEXT_WIDTH_IN, 2.55), gridspec_kw={"wspace": 0.42})

# (a) sampled RMS relative to the ideal input, vertical vibration, two filter models
for cutoff, col, lab in ((260.0, S.DATA, "260 Hz filter model"), (21.0, S.DATA2, "21 Hz filter model")):
    f, y, _ = series("vertical", cutoff, "sampled_vs_ideal_ratio")
    a.plot(f, y, color=col, marker="o", ms=3.2, lw=1.1, label=lab, zorder=3)
a.axvspan(nyq, 105, color=S.SHADE, zorder=0, lw=0)
a.axvline(nyq, color=S.INK2, lw=0.7)
a.text(nyq + 4, 0.80, f"above Nyquist ({nyq:.1f} Hz):\ncomponents alias to\nlower frequencies", fontsize=S.FS - 1.5,
       color=S.INK2, va="top")
a.set_xlim(0, 105); a.set_ylim(0, 1.12)
a.set_xlabel("Input vibration frequency (Hz)")
a.set_ylabel("Sampled RMS ÷ true RMS")
a.legend(loc="center left", bbox_to_anchor=(0.42, 0.40), handlelength=1.6)
S.panel(a, "a")

# (b) indicator RMS for the same 1 m/s^2 amplitude, along and across gravity (260 Hz model)
for direction, col, lab in (("vertical", S.DATA, "along gravity"), ("horizontal", S.DATA3, "across gravity")):
    f, y, _ = series(direction, 260.0, "sampled_indicator_rms_m_s2")
    b.plot(f, y, color=col, marker="o", ms=3.2, lw=1.1, zorder=3)
    b.text(102, y[-1], lab, fontsize=S.FS - 1, color=S.INK2, va="center", ha="left", clip_on=False)
    b.text(f[0], y[0] * 1.35, f"{y[0]:.3f} m/s²", fontsize=S.FS - 1, color=S.INK, va="bottom", ha="left")
b.set_yscale("log"); b.set_ylim(0.01, 2)
from matplotlib.ticker import FixedLocator, FixedFormatter, NullFormatter
b.yaxis.set_major_locator(FixedLocator([0.01, 0.1, 1])); b.yaxis.set_major_formatter(FixedFormatter(["0.01", "0.1", "1"])); b.yaxis.set_minor_formatter(NullFormatter())
b.set_xlim(0, 100)
b.set_xlabel("Input vibration frequency (Hz)")
b.set_ylabel("Indicator RMS (m/s²)\nfor 1 m/s² peak input")
S.panel(b, "b")
S.save(fig, out, "fig5_1_sampling")
