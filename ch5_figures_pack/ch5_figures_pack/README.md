# Chapter 5 figures pack

Everything needed to regenerate Figures 5.1 to 5.7 of the Chapter 5 results.

## Contents
- `scripts/`: Matplotlib scripts. `vibstyle.py` holds the shared style and the log parser; `wqstyle.py` holds the water-quality helpers.
- `data/`: copies of the exact logs the figures were drawn from:
  - vibration `run1.log` files for VP01-STAB, VP02-ONOFF, VP04-STOP and VP05-FAULT
  - `frequency_sweep.csv` from `vibration_sim.py`
  - the stage 4 and stage 5 pH status captures
- `figures/`: rendered output. Use the PDFs in LaTeX; the PNGs are only for checking.
- `tex/`: Section 5.2 (vibration) and Section 5.3.1 (water quality) LaTeX.

## Regenerate
```
pip install matplotlib
python run_all.py
```

The scripts print these values, which must match the text:

| Figure | Printed value |
|---|---|
| 5.4 | stop prompt to first STOPPED: 69.3 s |
| 5.5 | start to error: 2.05 s; stop to recovered: 1.04 s |
| 5.6 | 538 polls; first hold 30.5 s |

## Use in the report
1. Copy `figures/*.pdf` into the folder your report's `\includegraphics{figures/...}` points to.
2. Copy `tex/sec5_2_vibration.tex` and `tex/sec5_3_water.tex` next to your chapter file, then add `\input{sec5_2_vibration}` and `\input{sec5_3_water}` in Chapter 5.

## Fonts
If Arial is missing, the style falls back to Liberation Sans or DejaVu Sans.
