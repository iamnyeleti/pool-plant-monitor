"""Shared helpers for the water-quality status figures (5.6 and 5.7)."""
import json
import datetime as dt
from matplotlib.patches import Rectangle
import vibstyle as S

BREACH = "#6250d6"
GREY = "#c3c2b7"
COL = {"OK": S.STATE["OK"], "CAUTION": S.STATE["CAUTION"], "WARNING": S.STATE["WARNING"],
       "UNAVAILABLE": GREY, None: GREY, "OFF": S.SHADE, "ON": BREACH}


def load(path):
    rows = []
    for line in open(path, encoding="utf-8-sig"):
        a = line.strip().split(" ", 1)
        if len(a) < 2:
            continue
        try:
            rows.append((dt.datetime.fromisoformat(a[0]), json.loads(a[1])["water"]["ph"]))
        except (ValueError, KeyError):
            pass
    return rows


def ribbon(ax, T, seq, y, h=0.8, labels=None, min_w=18):
    i = 0
    while i < len(seq):
        j = i
        while j + 1 < len(seq) and seq[j + 1] == seq[i]:
            j += 1
        t0, t1 = T[i], T[j + 1] if j + 1 < len(T) else T[j] + 1
        v = seq[i]
        ax.add_patch(Rectangle((t0, y), t1 - t0, h, facecolor=COL.get(v, GREY), edgecolor="white", lw=0.4,
                               hatch="////" if v in (None, "UNAVAILABLE") else None))
        if labels and labels.get(i) and t1 - t0 > min_w:
            ax.text((t0 + t1) / 2, y + h / 2, labels[i], ha="center", va="center", fontsize=S.FS - 2,
                    color=S.STATE_TEXT.get(v, "white"), fontweight="bold")
        i = j + 1


