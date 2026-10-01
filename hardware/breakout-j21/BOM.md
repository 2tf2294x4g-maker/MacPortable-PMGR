# breakout-j21 — BOM

Build **one**. It is a bench jig and comes out before the machine is closed.

| Ref | Value | Package | Side | Qty | Note |
|---|---|---|---|---|---|
| J1 | **DF12NC(3.0)-50DS-0.5V(51)** | Hirose DF12N 50-pos, 0.5 mm | **Bottom** | 1 | Socket — receives the interposer |
| J2 | **DF12NC(3.0)-50DP-0.5V(51)** | Hirose DF12N 50-pos, 0.5 mm | Top | 1 | Header — receives the MCU board |
| J5 | 1×25 **2.54 mm** pin header | THT | Top | 1 | J21 **ODD** 1–49 |
| J6 | 1×25 **2.54 mm** pin header | THT | Top | 1 | J21 **EVEN** 2–50 |
| H1, H2 | 2.2 mm mounting hole | — | — | — | Not parts |

⚠️ **The footprints say `DF12C` / `DF12E`; the parts to buy are `DF12N`.** The
KiCad footprint names are inherited from `../breakout`, and `design/BOM.md`
records that **the original DF12C is obsolete** — the DF12N series is the
replacement. Same 0.5 mm pitch, same 50 positions, same 3.0 mm stack height, so
the footprint is unchanged; only the ordering part number differs. ⛔ Confirm
the `(51)` suffix matches on BOTH halves before ordering, and verify the pair
physically mates — `design/ORDER-LIST.md` raises the same warning.

⭐ The 2.54 mm headers are the whole point of this board: `../breakout` uses
1.27 mm, which will not take Dupont jumpers. Ordinary 0.1 in strip works; you
can cut two 25-way lengths from a 40-way strip.

## Board spec

```
  2 layer · 1.6 mm · HASL · 27 × 88 mm · two-sided stencil
```

⭐ **2 layers is what shipped** (ordered from PCBWay 2026-09-28, quoted at the
$5.00 promo PCB tier, which confirms the 2-layer entry). The board routed on two
layers with 594 segments, 89 vias and DRC 0/0. Its one deviation is the via
annular ring — see README.md's SHIPPING section. ⚠️ **This block previously said
4 layer**, describing the alternative in README.md's "⬜ The 4-layer alternative
— available, NOT built" rather than the board that was actually cut and ordered.

⚠️ **HASL, not ENIG.** `design/ORDER-LIST.md` records that specifying ENIG on
the original breakout cost **$42.41 instead of $5.00**, purely by dropping out
of PCBWay's promotional tier. Nothing here needs ENIG: no castellations, and no
long-term contact-reliability requirement.

## Assembly

```
  J1   BOTTOM   0.5 mm pitch, 50 pads   <- machine placement earns its money here
  J2   TOP      0.5 mm pitch, 50 pads   <- and here
  J5   TOP      THT, 25 pins            <- trivial by hand
  J6   TOP      THT, 25 pins            <- trivial by hand
```

⚠️ J1 and J2 are on **opposite faces**, so machine assembly of both is
**double-sided** — two reflow passes, and the dominant cost. The stencil data is
present and correct: `F.Paste` and `B.Paste` carry 50 apertures each, for the
two DF12s only. The THT headers correctly have no paste.

Files for a PCBA quote are in `gerbers/`:

```
  pmgr_breakout_j21_gerbers.zip     fabrication
  pmgr_breakout_j21-cpl.csv         placement (centroid), both sides
  this file                         BOM
```
