# PMGR Recreation — Bill of Materials

Generated from the board files, not from the design docs — `pmgr_mcu.kicad_pcb`, `pmgr_breakout.kicad_pcb`, `pmgr_interposer_v2.kicad_pcb`. Reference designators, packages and side-of-board are what will actually be fabricated.

Quantities are **per board** and **for 3 assemblies** (PowerBook 100 bring-up, Portable, spare).

---

## Board 1 — MCU board (`pmgr_mcu`)

4 layer · 1.6 mm · ENIG · 20.0 × 23.5 mm · two-sided assembly

| Ref | Value | Package | Side | Qty/bd | Qty ×3 | Note |
|---|---|---|---|---|---|---|
| U1 | AVR128DB64-I/PT | TQFP-64 10×10 P0.5 | Top | 1 | **4** | DigiKey 12807611. Order a spare — awkward to rework. |
| J1 | **DF12NC(3.0)-50DS-0.5V(51)** | Hirose DF12N 50-pos | **Bottom** | 1 | **3** | Socket. Mates the interposer header. Original DF12C is obsolete — see Connectors below. |
| J2 | 2×3 header 2.54 mm THT | PinHeader_2x03 | Top | 1 | **3** | UPDI + DBG0/1/2. **Trim pins after soldering.** |
| D1 | BAT54S | SOT-23 | **Bottom** | 1 | **3** | Dual Schottky, ADC clamp. |
| R1 | 1 kΩ | 0402 | **Bottom** | 1 | 3 | |
| C1, C2, C3 | 100 nF X7R | 0402 | Top | 3 | 9 | Decoupling |
| C4 | 100 nF X7R | 0402 | **Bottom** | 1 | 3 | Decoupling |
| C5, C6, C7 | 10 nF X7R | 0402 | Top | 3 | 9 | Decoupling |
| C9 | 10 nF X7R | 0402 | **Bottom** | 1 | 3 | Added after the pin audit found pins 56/57 unconnected |
| C8 | 10 µF X5R | 0805 | Top | 1 | 3 | Bulk |

**14 placements per board, 5 of them on the bottom side** — the board needs a two-sided stencil and two reflow passes, or bottom-side hand work. C4, C9, D1, R1 and the DF12 socket are all underneath.

**Four decoupling pairs, not three.** The AVR128DB64 has two VDD/GND pairs (6/7 and 56/57) plus AVDD and VDDIO2 — four supply pins.

---

## Board 2 — Interposer (`pmgr_interposer_v2`)

4 layer · 1.6 mm · ENIG · 16.3 × 16.3 mm · **castellated plating on all 60 half-holes**

| Ref | Value | Package | Side | Qty/bd | Qty ×3 | Note |
|---|---|---|---|---|---|---|
| J1 | **DF12NC(3.0)-50DP-0.5V(51)** | Hirose DF12N 50-pos | Top | 1 | **3** | Header. Mates the MCU board socket. |
| U1 | — (land pattern only) | PMGR_Castellation_60 | Top | 0 | 0 | Not a part. The 60 castellations that solder to the Portable's PMGR pads. |

> ℹ️ **Reference designator fixed 2026-08-26.** It was `PMGR Chip` (invalid — contains a space) and is now `J1`. The change was made AFTER the boards were ordered, so **boards from this run are silkscreened `PMGR Chip`**; a future run will read `J1`. Nothing electrical differs. The as-fabricated files are frozen in `interposer-v2/as-built-2026-08-26/`.

Only **one part** is placed on the interposer. Everything else about it is copper.

---

## Board 3 — Debug breakout (`pmgr_breakout`) — bench tool, optional

**2 layer** · 1.6 mm · **HASL** · 27 × 40 mm

> ⭐ **Order this one HASL, not ENIG.** The 2026-08-27 order specified ENIG and
> paid **$42.41** instead of **$5.00** — specifying it dropped the board out of
> PCBWay's $5-for-5 promotional tier. Nothing on this board needs ENIG: no
> fine-pitch parts, only through-hole 1.27 mm headers. See `ORDER-LIST.md`.

| Ref | Value | Package | Side | Qty/bd | Qty ×1 | Note |
|---|---|---|---|---|---|---|
| J1 | **DF12NC(3.0)-50DS-0.5V(51)** | Hirose DF12N 50-pos | **Bottom** | 1 | **1** | Socket — receives the interposer |
| J2 | **DF12NC(3.0)-50DP-0.5V(51)** | Hirose DF12N 50-pos | Top | 1 | **1** | Header — receives the MCU board |
| J3 | **FTS-113-01-L-D** | 2×13 1.27 mm **THT** | Top | 1 | **1** | Signals at positions 1–26 |
| J4 | **FTS-113-01-L-D** | 2×13 1.27 mm **THT** | Top | 1 | **1** | Signals at positions 27–50 (24 used, 2 spare) |
| H1 | 2.2 mm mounting hole | — | — | 1 | — | Not a part |

Build **one**. It comes out before the machine is closed.

---

## Consolidated order

### Semiconductors
| Qty | Part | Package |
|---|---|---|
| 4 | AVR128DB64-I/PT | 64-TQFP |
| 3 | BAT54S | SOT-23 |

### Connectors — order the mated pair together
| Qty | Part | Goes on |
|---|---|---|
| **4-5** | Hirose **DF12NC(3.0)-50DP-0.5V(51)** — header | Interposer ×3, breakout ×1 |
| **4-5** | Hirose **DF12NC(3.0)-50DS-0.5V(51)** — socket | MCU board ×3, breakout ×1 |
| 3-5 | **PRPC003DAAN-RC** 2×3, 2.54 mm THT (DK `35-PRPC003DAAN-RC-ND`) | MCU board J2 |
| 2 | **FTS-113-01-L-D** 2×13, 1.27 mm THT (Mouser `200-FTS11301LD`) | Breakout J3, J4 |
| — | 1.27 mm → 0.1″ IDC cables | Logic analyser |

> ⚠️ **Stack height must match on both halves, and must be (3.0).** The land pattern is identical across 3.0/3.5/4.0/5.0, so a wrong-height part solders down perfectly and silently fails to mate. **Mate one socket to one header by hand before reflowing anything** — while they are still two loose parts.

### Passives — buy in 100s, they cost nothing
| Qty | Value | Package |
|---|---|---|
| 12 | 100 nF X7R | 0402 |
| 12 | 10 nF X7R | 0402 |
| 3 | 10 µF X5R | 0805 |
| 3 | 1 kΩ | 0402 |

### Not a part, but required before first fit
**Kapton tape**, cut to the interposer outline — the third mitigation against shorting to the logic board, and the only one not solved in the PCB itself. The Portable's U15H footprint has live traces and vias inside it.

### Programming
One of: USB-serial adapter + 4.7 kΩ (cheapest, `Makefile` flash target already uses `pymcuprog`), MPLAB SNAP, or Atmel-ICE.

> ⚠️ Confirm your programmer's UPDI pin position against J2: **pin 1 = UPDI, pin 2 = +5 V, pin 6 = GND.** Pins 2 and 6 are standard; tools differ on UPDI — some expect pin 5.

---

## ⚠️ Connectors — RESOLVED 2026-08-26, do not re-derive

The original Hirose parts are **obsolete**. Ordering these three, and only these three:

| Role | Part | Distributor | Qty |
|---|---|---|---|
| Socket — MCU ×3, breakout J1 | **DF12NC(3.0)-50DS-0.5V(51)** | Mouser | 4-5 |
| Header — interposer ×3, breakout J2 | **DF12NC(3.0)-50DP-0.5V(51)** | Mouser | 4-5 |
| Breakout J3, J4 | **FTS-113-01-L-D** (Mouser 200-FTS11301LD) | Mouser | 2 |

### Why DF12**NC** and not DF12**NB**

`DF12C(3.0)-50DS-0.5V(81)` and `DF12E(3.0)-50DP-0.5V(81)` both went obsolete. General sources name **DF12NB** as the DF12 successor. **That is wrong for these boards.**

- **NB = *with* metal fitting** — its recommended pattern adds two lands (`0.8`, `1.8`). Our footprint has 50 copper pads and nothing else, so the fitting legs would land on bare soldermask.
- **NC = *without* metal fitting** — matches exactly. The obsolete parts were specified "W/O BOS,**FIT**" (without boss, without fitting), so NC is the true equivalent.

Verified against the DF12N catalog's *Recommended PCB mounting pattern*, "without metal fitting" variant:

| | Board (from `pmgr_mcu.kicad_pcb`) | DF12N w/o fitting |
|---|---|---|
| Pitch | 0.50 mm | 0.5 ±0.02 |
| Pad size | 0.30 × 1.60 mm | 0.3 ±0.02 × 1.6 ±0.02 |
| Row spacing | 3.60 mm | 2 + 1.6 = 3.6 |
| Span | 12.00 mm | B = 12.0 |
| Extra lands | none | none |

Keep **(3.0)** stack height — every clearance figure in `CLEARANCE-MEASUREMENTS.md` assumes it. 4.0 mm would also fit vertically (and improves VIA clearance from 0.5 to 1.5 mm) but invalidates the measured numbers.

### Why FTS-113-01-**L-D**

J3/J4 are **through-hole** (0.65 mm drills; the 113 PTH holes in the drill file are 61 vias + 26 + 26). Two wrong turns worth recording:

- **FTSH series is SMT-only** — its catalog is titled *Surface Mount Micro Header*. `-DV`/`-DH`/`-MT` are tail *orientations*, not mounting types.
- **In FTS, the Row Option decides mounting**: `-D` = Double **Through-hole**, `-DV` = Double Vertical **SMT**. `-P` (Pick & Place Pad) is an SMT-assembly option and indicates a surface-mount part.

Lead style matters too: **-01 = 3.05 mm post, mates FFSD** (the 1.27 mm IDC ribbon socket a logic analyser uses). `-03` = 1.65 mm mates CLP, a board-mounted socket strip — too short to hold a cable.

> Confirm your analyser's cable is an FFSD-style IDC socket. If it uses flying leads with micro-clips, post length is irrelevant and `-03` would do.

---

## Actual spend — PAID 2026-08-27

Not an estimate. What it cost.

| | |
|---|---|
| Boards (5 pcs each, 3 designs, ENIG 1U) | $175.91 |
| Freight | $6.04 |
| Bank handling | $10.97 |
| **Duty & tax** | **$48.38** |
| **PCBWay total** (order YH1796480) | **$241.30** |
| Parts — Mouser, 9 lines | $84.39 |
| **PROJECT TOTAL** | **$325.69** |

### Why the original "$80-150" estimate was wrong

It counted board and part prices only. Three things it missed:

1. **Duty & tax ~25%** — $48.38. The single largest omission.
2. **Bank handling fee** — $10.97 on an international order.
3. **ENIG** — the finish was originally quoted HASL. Correcting it added ~$94 to the
   products line. The breakout jumped 8x ($5.00 to $42.41) not because ENIG is expensive
   on 2 layers, but because specifying it dropped the board out of PCBWay's $5-for-5
   promotional tier.

**For future boards: budget roughly 2x the raw board+parts figure** once ENIG, duty,
freight and bank fees are included.

### What the money bought

5 pcs of each board rather than 3 — enough for the Portable, the PowerBook 100, a spare,
and two assembly failures without re-ordering. Worth having, given a hand-placed 0.5 mm
pitch TQFP-64 is the hardest step in the build.
