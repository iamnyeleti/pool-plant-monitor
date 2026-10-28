"""Shared style and log parsing for the Chapter 5 figures.

Edit the STYLE block to change every figure at once, then re-run the figure scripts.
Every figure is drawn from the raw run logs; nothing is typed in by hand.
"""
import re
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle

# ---------------------------------------------------------------- STYLE
TEXT_WIDTH_IN = 6.3          # A4 text width with 2.5 cm margins (16 cm)
FONT = ["Arial", "Liberation Sans", "Helvetica", "DejaVu Sans"]
FS = 8.5                     # base font size in points (thesis body is 11 pt)

INK = "#0b0b0b"              # primary text
INK2 = "#52514e"             # secondary text, reference lines
MUTED = "#898781"            # axis labels, ticks
GRID = "#e1e0d9"             # hairline grid
AXIS = "#c3c2b7"             # axis spines

DATA = "#2a78d6"             # measured signal (blue, slot 1)
DATA2 = "#eb6834"            # second measured series (orange, slot 2)
DATA3 = "#1baf7a"            # third measured series (aqua, slot 3)

# Monitor states: each always carries a text label, never colour alone.
STATE = {
    "OK":           "#0ca30c",
    "CAUTION":      "#fab219",
    "WARNING":      "#d03b3b",
    "STOPPED":      "#6250d6",
    "OFF SCHEDULE": "#898781",
    "SENSOR ERROR": "#0b0b0b",
}
STATE_TEXT = {"CAUTION": INK}           # dark label on the yellow fill
CAUTION_LINE = "#b47a00"                # darker step of yellow so the line reads on white
WARNING_LINE = "#d03b3b"
SHADE = "#f0efec"                       # neutral wash for test phases


def apply():
    plt.rcParams.update({
        "font.family": "sans-serif", "font.sans-serif": FONT, "font.size": FS,
        "axes.titlesize": FS, "axes.labelsize": FS, "xtick.labelsize": FS - 0.5,
        "ytick.labelsize": FS - 0.5, "legend.fontsize": FS - 0.5,
        "axes.edgecolor": AXIS, "axes.linewidth": 0.6, "axes.labelcolor": INK,
        "axes.spines.top": False, "axes.spines.right": False,
        "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.5, "grid.linestyle": "-",
        "axes.axisbelow": True, "xtick.color": MUTED, "ytick.color": MUTED,
        "xtick.labelcolor": INK2, "ytick.labelcolor": INK2,
        "xtick.major.size": 2.5, "ytick.major.size": 2.5, "xtick.major.width": 0.6, "ytick.major.width": 0.6,
        "legend.frameon": False, "figure.dpi": 150, "savefig.dpi": 300,
        "pdf.fonttype": 42, "ps.fonttype": 42, "lines.linewidth": 0.9,
        "figure.facecolor": "white", "axes.facecolor": "white", "savefig.facecolor": "white",
    })


def save(fig, out, name):
    fig.savefig(f"{out}/{name}.pdf", bbox_inches="tight", pad_inches=0.02)
    fig.savefig(f"{out}/{name}.png", bbox_inches="tight", pad_inches=0.02)
    plt.close(fig)


def panel(ax, letter, x=-0.01, y=1.02):
    ax.text(x, y, f"({letter})", transform=ax.transAxes, ha="right", va="bottom",
            fontsize=FS, fontweight="bold", color=INK)


def hline(ax, y, label, color, ls="-", lw=0.8, xpos=1.005):
    """Reference line with its label in ink just outside the right edge."""
    ax.axhline(y, color=color, ls=ls, lw=lw, zorder=2)
    ax.text(xpos, y, label, transform=ax.get_yaxis_transform(), ha="left", va="center",
            fontsize=FS - 1, color=INK2, clip_on=False)


def state_ribbon(ax, segs, t_end, y0=0.0, h=1.0, min_label_s=12):
    """segs: list of (t_start, state). Draws contiguous coloured blocks with labels."""
    for i, (t0, s) in enumerate(segs):
        t1 = segs[i + 1][0] if i + 1 < len(segs) else t_end
        ax.add_patch(Rectangle((t0, y0), t1 - t0, h, facecolor=STATE.get(s, MUTED),
                               edgecolor="white", linewidth=0.4))
        if t1 - t0 >= min_label_s:
            ax.text((t0 + t1) / 2, y0 + h / 2, s, ha="center", va="center", fontsize=FS - 1.5,
                    color=STATE_TEXT.get(s, "white"), fontweight="bold")


def runs_of(status):
    """Collapse per-line states into (start time, state) runs."""
    out = []
    for t, s, _ in status:
        if not out or out[-1][1] != s:
            out.append((t, s))
    return out


# ---------------------------------------------------------------- LOG PARSING
TS = re.compile(r'^(\d\d):(\d\d):(\d\d)\.(\d{3}) > ')
RE_STATUS = re.compile(r'Status: ([A-Z ]+?) \| RMS: ([\d.]+)')
RE_STD = re.compile(r'\[STD\] std=([\d.]+) n=(\d+) stopped=(\d)')
RE_EVENT = re.compile(r'\[EVENT\] .*? (\w+) duration_ms=(\d+) peak_rms=([\d.]+) warning=(\d)')
RE_TIMING = re.compile(r'\[TIMING\] n=(\d+) mean_ms=([\d.]+) min_ms=([\d.]+) max_ms=([\d.]+) '
                       r'std_ms=([\d.]+) over_50ms=(\d+) read_errors=(\d+)')
RE_CFG = re.compile(r'(\w+)=([^\s]+)')
RE_RTC = re.compile(r'^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d')


def parse(path):
    """Same parsing rules as analyse_vib.py. Times are seconds from the first log line (laptop clock)."""
    rows = []
    for raw in open(path, errors="replace"):
        raw = raw.rstrip("\r\n")
        m = TS.match(raw)
        if m:
            h, mi, s, ms = map(int, m.groups())
            rows.append((h * 3600 + mi * 60 + s + ms / 1000.0, raw[m.end():]))
    t0 = rows[0][0]
    d = {"status": [], "std": [], "markers": [], "events": [], "timing": [], "rtc": [], "msgs": [], "cfg": {}}
    for t, x in ((t - t0, x) for t, x in rows):
        m = RE_STATUS.search(x)
        if m:
            d["status"].append((t, m.group(1).strip(), float(m.group(2)))); continue
        m = RE_STD.match(x)
        if m:
            d["std"].append((t, float(m.group(1)), int(m.group(2)))); continue
        if x.startswith("#SCRIPT"):
            d["markers"].append((t, x[len("#SCRIPT "):])); continue
        m = RE_EVENT.search(x)
        if m:
            d["events"].append((t, int(m.group(2)), float(m.group(3)), int(m.group(4)))); continue
        m = RE_TIMING.search(x)
        if m:
            d["timing"].append((t,) + tuple(float(g) for g in m.groups())); continue
        if RE_RTC.match(x):
            d["rtc"].append(t); continue
        if x.startswith("[CFG]") and "dlpf" in x:
            d["cfg"] = dict(RE_CFG.findall(x))
        if "[FAULT-INJECT]" in x or "SENSOR ERROR" in x or "sensor recovered" in x:
            d["msgs"].append((t, x.strip()))
    return d


def spans(markers):
    """Segments between '#SCRIPT state' markers: (kind, window, start, end)."""
    st_m = [(t, m) for t, m in markers if m.startswith("state")]
    end_t = next((t for t, m in markers if m.startswith("end")), None)
    out = []
    for i, (t, m) in enumerate(st_m):
        b = st_m[i + 1][0] if i + 1 < len(st_m) else end_t
        if b is None:
            continue
        parts = m.split()
        win = next((p.split("=")[1] for p in parts if p.startswith("win=")), "")
        out.append((parts[1], win, t, b))
    return out
