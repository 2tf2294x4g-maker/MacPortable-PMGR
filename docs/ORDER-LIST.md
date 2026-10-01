# PMGR Recreation — What To Order

Everything needed to build the PMGR replacement — two boards for the machine, plus an optional bench breakout. Quantities assume **3 assemblies** (one for the PowerBook 100 bring-up, one for the Portable, one spare).

---

## 1. PCBs — PCBWay

| Board | Spec | File |
|---|---|---|
| **Interposer** | 4 layer · 1.6 mm · ENIG · 16.3 × 16.3 mm · **castellated plating on all 60 half-holes** | `interposer-v2/pmgr_interposer_v2_gerbers.zip` |
| **MCU board** | 4 layer · 1.6 mm · ENIG · 20.0 × 23.5 mm · two-sided stencil | `mcu-board/pmgr_mcu_gerbers.zip` |
| **Debug breakout** | **2 layer** · 1.6 mm · **HASL** · 27 × 40 mm · two-sided stencil | `breakout/pmgr_breakout_gerbers.zip` |

The breakout is a **bench tool** — it sits between the interposer and the MCU board so a logic analyser can reach all 50 signals, and comes out before the machine is closed. Optional, but it is the only way to see the bus.

Minimum order is usually 5 of each.

### ⭐ Do not copy this order verbatim — ENIG only matters on two of the three

**The breakout above is specified HASL, and the 2026-08-27 order got this wrong.**
Ordering it in ENIG cost **$42.41** instead of **$5.00** — an 8× jump, and not
because ENIG is expensive on a 2-layer board. Specifying it simply dropped the
board out of PCBWay's $5-for-5 promotional tier.

| Choice | Boards line |
|---|---|
| All three ENIG (what was actually paid) | $175.91 |
| **Breakout in HASL** | **~$138.50** |
| Breakout omitted entirely | ~$133.50 |

⭐ **So the breakout costs about $5 if you order it in HASL — omitting it saves
almost nothing.** Order it. It has no fine-pitch parts, only through-hole
1.27 mm headers, and no long-term contact-reliability requirement, so ENIG buys
it nothing.

⛔ **ENIG is NOT optional on the interposer.** 0.40 mm castellated half-holes at
the fab's stated minimum, alongside a 0.5 mm-pitch TQFP-64 pad ring, is exactly
what ENIG exists for. This is the one board in the set with no process margin.

**Building just one set?** You still pay for 5 of each — the minimum dominates,
the marginal board is nearly free. Budget **~$140–150 of boards**, before
freight and duty. Duty is jurisdictional (it was ~25%, $48.38, on this order)
and is the single largest cost nobody estimates.

### ⚠️ Two questions to send with the interposer

1. **The 0.40 mm castellated hole sits exactly at PCBWay's stated minimum** — no margin. Most likely thing to come back as an engineering query.
2. **How do they want traces approaching castellation pads handled?** Three were rerouted perpendicular to satisfy edge clearance; worth confirming that matches their preference.

The MCU board needs no such conversation — nothing on it is near a process limit.

---

## 2. Semiconductors

| Qty | Part | Package | Note |
|---|---|---|---|
| **4** | **AVR128DB64-I/PT** | 64-TQFP 10×10 | DigiKey **12807611**. 1.8–5.5 V native, so no level shifters. |
| **3** | **BAT54S** | SOT-23 | Dual Schottky, ADC clamp. Any manufacturer. |

Order a spare AVR — it is the one part that is awkward to rework off a 0.5 mm TQFP if a joint goes bad.

---

## 3. Connectors — the mated pair

**These must be ordered together and they are not interchangeable.**

| Qty | Part | Goes on |
|---|---|---|
| **4** | **Hirose DF12E(3.0)-50DP-0.5V(81)** — header | **Interposer** top face ×3, **breakout** top face ×1 |
| **4** | **Hirose DF12C(3.0)-50DS-0.5V(81)** — socket | **MCU board** bottom ×3, **breakout** bottom ×1 |

The breakout needs one of each — a socket underneath to mate the interposer, a header on top to receive the MCU board.

3.0 mm mated stack height. Both footprints came from KiCad's stock library and the land pattern was verified against Hirose datasheet A306.

⚠️ **Verify the pair physically mates before assembling anything.** The `(81)` suffix is the variant KiCad's footprint cites; confirm the distributor's suffix matches on both halves.

| Qty | Part | Note |
|---|---|---|
| **3** | 2×3 pin header, 2.54 mm, through-hole | UPDI + 3 debug outputs. **Trim the pins after soldering** — 1.6 mm clearance untrimmed vs 3.6 mm trimmed. |
| **2** | 2×13 pin header, **1.27 mm**, through-hole | Breakout debug headers (positions 1–26 and 27–50) |
| — | 1.27 mm → 0.1″ IDC cables | for the analyser |

---

## 4. Passives — MCU board

Per board; buy in 10s or 100s, they cost nothing.

| Qty/board | Value | Package | Ref |
|---|---|---|---|
| 4 | 100 nF | 0402 | C1–C4 |
| 4 | 10 nF | 0402 | C5–C7, C9 |
| 1 | 10 µF | 0805 | C8 |
| 1 | 1 kΩ | 0402 | R1 |

X7R or better for the decoupling. C8 can be X5R.

**Four pairs, not three.** The AVR128DB64 has **two** VDD/GND pairs (pins 6/7 and 56/57) plus AVDD and VDDIO2 — four supply pins, so four 100 nF + 10 nF pairs. C9 exists because pins 56/57 were found unconnected during the pin audit.

---

## 5. Programming — ✅ ORDERED

**USB UPDI programming adapter, CH340E** — ordered 2026-08-27, **$18.35**,
in transit. (Shenzhen Module Studio; ref 8213565528431578.)

⛔ **It enumerates as `/dev/tty.wchusbserial*`, not `/dev/tty.usbserial-*`.**
The `flash` target originally hard-coded the FTDI pattern, which would have
failed in a way that looks like a dead programmer. `firmware/Makefile` now
auto-detects CH340, FTDI, CP210x and CDC names and prints what it can see if
none match. Override with `make flash PORT=/dev/tty.something`.

⚠️ macOS 11+ carries a built-in CH34x driver, so no vendor kext should be
needed. If the device never appears, that is the first thing to check.

Alternatives, if it disappoints: MPLAB SNAP (official, UPDI capable),
Atmel-ICE (more than needed), or an AVR128DB48 Curiosity Nano — which would
also allow firmware work without the PCBs.

⚠️ **Confirm your programmer's UPDI pin position** against the board's J2: pin 1 = UPDI, pin 2 = +5V, pin 6 = GND. Pins 2 and 6 are standard, but tools differ on where UPDI sits — some expect pin 5.

---

## 6. ✅ On hand — needed before first fit

**Kapton tape** — already in the collection (confirmed 2026-09-01). Cut to the
interposer outline. This is the third of the three mitigations against shorting to the logic board, and the only one not solved in the PCB design itself. The Portable's U15H footprint has live traces and vias inside it.

---

## Total new spend

**ACTUAL, paid 2026-08-27: $325.69** — PCBWay $241.30 (incl. $48.38 duty, $10.97 bank fee), Mouser $84.39.

The earlier "$80-150" estimate was wrong: it omitted ~25% duty, the international bank fee, and the ENIG correction. See `BOM.md` for the breakdown. **Budget ~2x raw board+parts cost on future orders.**

---

## Elsewhere in the collection (separate orders, unrelated to PMGR)

- **PowerBook 100 recap — second order for the SMD positions.** The through-hole order (DigiKey #100885534) is complete, but several board positions are surface-mount. Two values still need a live parametric search: **10 µF 16 V SMD** (the obvious Nichicon part is obsolete) and **3.3 µF 35 V low-profile**. See `project_powerbook100_repair` memory.
- **Newark NFS-26A-0110BF** — LCD cable, logic-board end. Ordered; shipped 2026-09-15 (qty 4), ahead of the ~Nov 2026 estimate. The scarce part.
