# Reference measurements

Known-good values to check your machine against. Everything here was **measured on our own two
Portables**: one fitted with this replacement, and one **unmodified** (original hybrid, original
M50753), which is the reference for anything the hybrid produces.

⚠️ One unmodified machine is a small sample. Expect your readings to differ by tens of millivolts;
a difference of tenths of a volt is worth looking into.

⛔ **On a machine with the ORIGINAL M50753, keep the battery input at or below 6.5 V while probing.**
Its battery-sense pin is unprotected.

## Pin references

**M50753 (U15H) — all 60 leads.** Pin 1 is the filled white dot, lowest lead on the right;
numbering runs counter-clockwise, confirmed by continuity on the board. Each lead is labelled with
its signal and the J21 pin it reaches.

![M50753 pinout](../images/m50753-pinout.jpg)

**J21 — the 50-pad diagnostic connector** (2 × 25, 1 mm pitch). Bottom row odd, top row even;
**pin 1 is the square pad at the left.** ⚠️ The silkscreen "1" below the pads belongs to J22, not
J21. Each pad shows its signal and the M50753 pin it comes from. The J21 breakout board brings all
of these out to 2.54 mm headers with the same numbers.

![J21 pinout](../images/j21-pinout.jpg)

**The bare footprint, for fitting the interposer.** The three circles are an **orientation key, not
pin 1** — the chip carries the same three marks, so they fix rotation only. Pin 1 is the white dot.

![Footprint orientation](../images/footprint-orientation.jpg)

The full pin-by-pin mapping, through the interposer to the AVR, is in [`PINOUT.md`](PINOUT.md).

### Footprint checks — power OFF, before fitting anything
```
  M50753 pins 45, 55, 56  ->  J21-1   +5 V always-on      ~0.1 Ω
  M50753 pins 17, 23, 53  ->  J21-3   ground              ~0.0-0.1 Ω
  M50753 pin 54           ->  R17, the end nearer J21-48  ~0.2 Ω   (confirms the pin numbering)
```

## Healthy voltages — unmodified machine

Battery input set to **6.41 V at the pack connection** (by DMM), machine **off and not booted**,
charger and programmer disconnected, **ground at battery negative**.

```
  hybrid pin 16, 17           battery input path      6.41 V   (= the input)
  hybrid pin 19               charger status          5.15 V
  hybrid pin 22               Q1 control              6.33 V   (Q1 gate 6.32 V -> Q1 OFF)
  hybrid pin 37               -5 V enable             ~4 mV
  hybrid pin 39               HICHG                   ~3 mV
  J21-1                       +5 V always-on rail     5.2 V    (measured with 5.70 V in)
  J21-40                      battery sense (A/D)     ~2.61 V  (from the line below)
```

⚠️ **Hybrid pin numbers are their own numbering** — not J21 numbers and not M50753 pins. Match
points by signal name. For where the hybrid's pins are, and for readings from a **replacement
hybrid**, see the documentation from **Androda**, who makes one.

**Q1** is the IRF9Z30 P-channel MOSFET beside the charger jack: the **fast-charge bypass** across
R10 (100 Ω). On, it gives the low-resistance charge path; off, charging continues through R10.
Its gate is driven by hybrid pin 22.

## Battery sense — what the PMGR sees

On the unmodified machine, the voltage at the PMGR's battery-sense input follows the pack:

```
  A/D_FILTER = (V_pack - 5.1249) x 2.0324        11 pairs, scatter 8 mV

  pack 5.90 V  ->  ~1.58 V      pack 6.41 V  ->  ~2.61 V      pack 7.20 V  ->  ~4.22 V
```

Hybrids differ: another machine read **tenths of a volt** lower at the same pack voltage. That
spread is why each board gets its own calibration record ([`BUILD.md`](BUILD.md) §7).

## The firmware's battery levels

The same levels as the original, applied to the calibrated reading:

```
  knee         7.20 V   below it, fast charge runs; crossing it starts the top-off timer
  LOW          5.90 V   the low-battery condition the Mac warns about
  STAY_ASLEEP  5.82 V   below it, the machine can no longer be woken
  DEAD         5.74 V   the PMGR puts the machine to sleep itself
```

## Cut-off

The unmodified machine's hybrid **cuts off between 5.61 and 5.70 V** at the battery input, and
comes back on at the next step up — essentially no hysteresis. If your machine dies well above
this, suspect the hybrid or the battery wiring before the PMGR.

## Charging — measured on the development machine

A 3-cell Cyclon pack, the machine's own charger, two supervised charges:

```
  fast charge crosses the knee at      7.198 V / 7.220 V
  fast charge terminates at            7.211 V / 7.240 V
  highest pack voltage seen            7.252 V / 7.269 V
  float, 42 min after termination      ~7.23 V  (one observation, voltage only)
```

A DMM at the pack agreed with the logged readings to about 15 mV.

## Board checks

```
  AVR device ID (pymcuprog ping)       1E970B
  standby current, Mac switched off    ~10 mA  (13 mA from cold)
  DBG2 high, DBG1 low                  healthy ADC   (see ../firmware/README.md)
```
