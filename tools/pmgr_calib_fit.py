#!/usr/bin/env python3
"""Fit (pack volts -> stored level) from a --volts capture, and judge the fit.

    python3 tools/pmgr_calib_fit.py calib-2026-09-04.csv
    python3 tools/pmgr_calib_fit.py calib.csv --published 100.0 0.0

⭐⭐ THE TRANSFER FUNCTION THIS TOOL CHECKS AGAINST:

        raw = 100 * V          (raw = stored level + 512, V = pack volts)

MEASUREMENTS.md 2.50, swept 5.83-7.20 V in BOTH directions, 253 usable pairs.
LOW / STAY_ASLEEP / DEAD all sit INSIDE that range. The defaults below are this
line; see pmgr_adc.h for why the round number is adopted for adequacy.

⛔ SUPERSEDED PREMISE. This tool was written against an OLD two-pair fit that
2.50 RETRACTED, and its defaults then were that retired line - the one fitted to
just 6.14 V/level 100 and 7.00 V/level 192, with every threshold extrapolated
below the measured range. ⚠️ The retired equation is deliberately NOT displayed
here: it stood as a formatted display equation while its replacement appeared
only as prose, which made the WRONG line the visually authoritative one on a
skim. It is written out once, in context, at pmgr_adc.h's calibration comment.
The argument for the tool is unchanged, and is the reason 2.50 exists:

Two points FIX a line; they cannot TEST one. A discharge logged with
`pmgr_charge_current.py --volts` walks the pack down through exactly that
region, one (volts, level) pair per acquisition.

⛔⛔ THE THING THIS TOOL EXISTS TO PREVENT. This project has produced FIVE
confident wrong answers from too little spread (RIGOL-NOTES.md, "The recurring
methodological error"). A regression over a narrow span will report a beautiful
R^2 and a slope that means nothing: with 16 mV of range and +/-5 mV of noise,
the slope is almost pure noise amplification. So this refuses to quote a slope
until the span earns it, and says why.

⚠️ IT ALSO WILL NOT PROVE LINEARITY. A straight line fitted to a straight-ish
segment looks straight. What a wide span buys is the ability to SEE curvature if
it is there - and to check the published line against data it was not fitted to.
"""
import argparse
import csv
import sys

PUB_M, PUB_B, OFFSET = 100.0, 0.0, 512      # 2.50, superseding 106.98/-44.8
THRESHOLDS = (('LOW', 78), ('STAY_ASLEEP', 70), ('DEAD', 62), ('HICHG knee', 208))

# ⭐ Span needed before a slope is worth quoting. Derived, not guessed: the
# level is an integer, so quantisation alone is +/-0.5 level = +/-4.7 mV, and
# observed row-to-row scatter is comparable. To pin a slope to ~1 %, the span
# must be ~100x that noise.
MIN_SPAN_V = 0.25


def load(path):
    rows = []
    with open(path, newline='') as fh:
        for r in csv.DictReader(fh):
            if r.get('suspect'):                 # framing artefact, see 2.40
                continue
            try:
                v = float(r['pack_V']); lv = int(r['level'])
            except (ValueError, TypeError, KeyError):
                continue
            rows.append((v, lv, r.get('flags', '')))
    return rows


def fit(xs, ys):
    """Least squares y = m*x + c, plus R^2. No numpy dependency."""
    n = len(xs)
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        return None
    m = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx
    c = my - m * mx
    ss_res = sum((y - (m * x + c)) ** 2 for x, y in zip(xs, ys))
    ss_tot = sum((y - my) ** 2 for y in ys)
    r2 = 1 - ss_res / ss_tot if ss_tot else float('nan')
    return m, c, r2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('csv')
    ap.add_argument('--published', nargs=2, type=float, default=[PUB_M, PUB_B],
                    metavar=('M', 'B'))
    a = ap.parse_args()
    pm, pb = a.published

    rows = load(a.csv)
    if len(rows) < 5:
        sys.exit(f'  only {len(rows)} usable rows in {a.csv}')
    vs = [r[0] for r in rows]
    span = max(vs) - min(vs)

    print(f"\n  {len(rows)} usable rows   pack {min(vs):.4f} - {max(vs):.4f} V "
          f"(span {span*1000:.0f} mV)   level {min(r[1] for r in rows)}"
          f"-{max(r[1] for r in rows)}")

    seen = sorted({f for _, _, f in rows if f and f != '-'})
    if seen:
        print(f"  ⭐ flags observed: {', '.join(seen)}")

    # ---- the published line, judged against THIS data ---------------------
    # ⭐ This is the honest test even when the span is small: the published line
    # was NOT fitted to these rows, so its residual here is a real check.
    res = [lv - (pm * v + pb - OFFSET) for v, lv, _ in rows]
    mean = sum(res) / len(res)
    worst = max(abs(r - mean) for r in res)
    print(f"\n  === published line  raw = {pm}*V {pb:+} ===")
    print(f"    bias           {mean:+.2f} level  ({mean/pm*1000:+.1f} mV)")
    print(f"    spread ± {worst:.2f} level about that bias")
    print(f"    ⭐ a consistent BIAS with small spread means the SLOPE is right"
          f" and\n       the offset differs - a sense-point difference, not an error.")

    # ---- our own slope, but only if the span earns it ---------------------
    print(f"\n  === slope fitted to THIS data ===")
    if span < MIN_SPAN_V:
        print(f"    ⛔ REFUSED - span is {span*1000:.0f} mV, need ≥ {MIN_SPAN_V*1000:.0f} mV.")
        print(f"       Level is an integer, so quantisation alone is ±4.7 mV. Over")
        print(f"       {span*1000:.0f} mV a least-squares slope is mostly noise, and it")
        print(f"       would still report a flattering R². This project has been")
        print(f"       wrong five times from exactly this - RIGOL-NOTES.md.")
        print(f"       Keep discharging: LOW (level 78) is the target.")
        return
    m, c, r2 = fit(vs, [lv for _, lv, _ in rows])
    fm, fb = m, c + OFFSET
    print(f"    raw = {fm:.2f}*V {fb:+.1f}      R² = {r2:.5f}")
    print(f"    published:  {pm}*V {pb:+}")
    print(f"    slope differs by {(fm-pm)/pm*100:+.2f} %")
    print(f"\n  === thresholds ===")
    print(f"    {'':14} {'published':>10} {'this fit':>10} {'Δ mV':>8}")
    for name, lvl in THRESHOLDS:
        pv = ((lvl + OFFSET) - pb) / pm
        nv = ((lvl + OFFSET) - fb) / fm
        inside = '' if min(vs) <= pv <= max(vs) else '  (extrapolated)'
        print(f"    {name:14} {pv:9.3f}V {nv:9.3f}V {(nv-pv)*1000:+8.0f}{inside}")


if __name__ == '__main__':
    main()
