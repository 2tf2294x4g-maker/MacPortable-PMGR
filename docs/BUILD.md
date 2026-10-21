# Building one

This is the route from bare boards to a working machine. Each step points to the detailed
document; **[`ASSEMBLY.md`](ASSEMBLY.md) is the bench-tested procedure and wins wherever the two
disagree.** Some detailed documents mention entries in the development log, which is not part of
this repository.

Read the safety notes in the top-level [`README`](../README.md) first.

> **PowerBook 100?** The same boards and the same `pmgr.hex` are used — there is no separate build.
> The differences are in fitting and testing: the PMGR is **U22**, its test points are all on the
> **underside** of the logic board, the stack sits **under the CPU board** (it fits), and the
> battery-bay **plunger** must be held in for a bench supply on the battery contacts to power anything.
> It has no separate power hybrid; its battery sense reads on the Portable's scale, so calibrate it as
> below with `--hybrid original`. Calibration and charging have **not yet been tried** on a PowerBook
> 100 — see [`VERIFIED.md`](VERIFIED.md#powerbook-100) and the probe points in
> [`REFERENCE-MEASUREMENTS.md`](REFERENCE-MEASUREMENTS.md#powerbook-100).
>
> **SCSI Disk Mode needs one extra wire** (0.1.16 or later). The disk-mode signal arrives on the
> M50753's **pin 48**, which these boards don't connect. Route it through the DF12's spare
> **position 50** (unconnected on both boards), so the stack still comes apart:
> 1. **Interposer:** fine magnet wire (about 0.1 mm) from **castellation 48** to the foot of the DF12
>    header's **pin 50**. Pin 50 is the corner pin of the row nearer castellations 46–60, at the end
>    nearer castellation 48.
> 2. **MCU board:** from the foot of the DF12 socket's **pin 50** (underside), around the board edge,
>    to the AVR's **pin 46 (PF2)**: the third leg from the corner on the side carrying legs 33–48,
>    next to leg 47 (`DBG2`).
>
> ⛔ **Pin 49, next to pin 50, is ground on both boards**, and leg 47 is `DBG2`, an output. Before power:
> pin 50 (or leg 46) to ground must read **open**, leg 46 to leg 47 **open**, and, stacked, HDI-30
> pin 1 to leg 46 about **91 Ω**. Keep solder on the lead feet, out of the connector's contacts.
> Without the wire everything else works; the machine just can't enter disk mode. On a Portable the
> wire is not needed.

## 0. Skills and tools

This is **fine-pitch surface-mount work on an irreplaceable machine.** Be comfortable with:

| part | where | what it takes |
|---|---|---|
| AVR128DB64, TQFP-64, **0.5 mm** pitch | MCU board, top | drag soldering with plenty of flux; inspect every side for bridges |
| Hirose DF12, 50 pins, **0.5 mm** pitch | one on each board — the MCU board's is on the **underside** | no mechanical key: verify rotation with a meter before soldering |
| **0402** passives (×8) and a SOT-23 diode | MCU board, both sides | tweezers and magnification |
| the original M50753, 60 leads | the Mac's logic board | **removal** with hot air and preheat, without lifting pads or overheating the board |
| interposer, 60 castellated edges, 0.8 mm | onto that footprint | aligning and soldering all four sides in place |

**Tools:** hot-air station with preheat, fine-tip iron, flux, solder wick, tweezers, a multimeter,
and magnification — a stereo microscope is strongly recommended.

⭐ **Ordering the MCU board assembled** (two-sided, from the files in `hardware/mcu-board/`) takes the
0.5 mm and 0402 work off your bench. Removing the original chip and fitting the interposer can't be
outsourced. If you haven't removed a fine-pitch QFP from a board you care about before, practise on
scrap first.

## 1. Order the boards

| Board | Specification | Files |
|---|---|---|
| Interposer | 4 layer · 1.6 mm · **ENIG** · 16.3 × 16.3 mm · **castellated plating on all 60 half-holes** | `hardware/interposer-v2/` |
| MCU board | 4 layer · 1.6 mm · ENIG · 20.0 × 23.5 mm · two-sided stencil | `hardware/mcu-board/` |
| J21 breakout *(optional)* | 2 layer · 1.6 mm · HASL · 27 × 88 mm | `hardware/breakout-j21/` |

- ⛔ **ENIG is not optional on the interposer** — its 0.4 mm castellated half-holes need it.
- The breakout is only for development. Ordering it in HASL keeps it in most fabs' cheapest tier.
- ⚠️ The breakout's vias are 0.20 mm drill with a 0.125 mm annular ring. PCBWay builds that on their
  standard order; **JLCPCB's standard process does not** (0.30 mm / 0.13 mm minimum).

## 2. Order the parts

[`BOM.md`](BOM.md) and [`ORDER-LIST.md`](ORDER-LIST.md). Three things catch people out:

- **The two halves of each DF12 connector are not interchangeable.** The socket (`DS`) and header
  (`DP`) go on specific boards — see "Which part goes where" in [`ASSEMBLY.md`](ASSEMBLY.md).
- **Both halves must be the 3.0 mm stack height.** Every height shares the same footprint, so a
  wrong-height part solders down perfectly and then won't mate. **Mate a pair by hand before
  soldering either.**
- Order a spare AVR128DB64. Reworking a 0.5 mm TQFP-64 is the hardest repair on this board.

## 3. Assemble the MCU board — [`ASSEMBLY.md`](ASSEMBLY.md), Gate 0 and Step 1

- ⛔ **Resolve orientation (Gate 0) before any solder.**
- **Read the AVR's device ID before fitting the programming header `J2`** (step 4 below). A dead
  part and a bad joint look identical; this separates them while rework is still easy.
- `J1` (the DF12) has **no mechanical key** — verify its rotation with a meter.
- **Three adjacent pin pairs are supposed to be shorted.** Don't "fix" them when hunting bridges.

## 4. Program the AVR — before it goes anywhere near a machine

**Full step-by-step guide: [`PROGRAMMING.md`](PROGRAMMING.md).** The short version:

1. Build: `make -C firmware`.
2. Programmer: any USB-UART adapter used as SerialUPDI, with a **1 kΩ series resistor** on the
   data line.
3. ⭐ **First, read the device ID** — not a flash:
   `pymcuprog ping -t uart -u <port> -d avr128db64` must answer `1E970B`.
   ⚠️ If it reports "UPDI initialisation failed", check the board is actually powered: a programmer
   alone back-feeds only ~1.65 V, below the AVR's brown-out, so it cannot be programmed that way.
4. **Fuses, once per board, before the first flash:** `make -C firmware fuses`
   (see [`../firmware/FUSES.md`](../firmware/FUSES.md)).
5. Flash — see step 7 for why it should go through the flash tool once a record exists.
6. ⛔ **Then unplug the programmer and do a real power cycle** — supply or battery disconnected.
   This applies after **any** programmer operation, even a device-ID read with nothing written:
   without it, a machine that is shut down failed to wake from the keyboard every time it was
   tried (7 of 7). A power-on start always works. The cause has not been established.

⛔ **`J2` pin 2 is +5 V and it reaches the machine.** Mind it when wiring the programmer.

## 5. Fit the interposer — [`ASSEMBLY.md`](ASSEMBLY.md), Steps 2 and 4

The original M50753 has to come off the logic board first. **Keep it** — it is irreplaceable, and
the reference for everything this replaces. Then solder the interposer to the footprint by its
castellated edges, and check it before stacking.

## 6. Stack and first power-on — [`ASSEMBLY.md`](ASSEMBLY.md), Steps 5 and 6

Interposer on the logic board, then (optionally) the J21 breakout, then the MCU board.
Bench power first, at a modest voltage, with the battery out.
Check the voltages and pin references in [`REFERENCE-MEASUREMENTS.md`](REFERENCE-MEASUREMENTS.md).

## 7. Calibrate the battery reading

Each machine's hybrid reads the battery slightly differently. The firmware uses a **per-unit
calibration record** (16 bytes, EEPROM address 464) to correct for it, and **refuses to fast-charge
without a valid one.** The machine works without a record — it just charges slowly.

1. **Collect pairs from an UNCALIBRATED board.** Flash the firmware **without** a record first
   (leave `calib.args` off — see [`PROGRAMMING.md`](PROGRAMMING.md)). Only then is the level the PMGR
   reports the raw, uncorrected reading. ⛔ Readings taken from a board that already has a record are
   corrected values, and fitting them would undo the correction.
   Record the pack voltage (DMM, at the pack terminals) against that level, across **at least
   0.80 V** — roughly 5.8–7.2 V — in both directions.
2. **Fit and judge it**: `python3 tools/pmgr_calib_fit.py <pairs.csv>`. It **refuses**, with a
   non-zero exit, a fit whose worst residual exceeds **2 counts** or whose span is under **0.80 V** —
   the same limits the firmware enforces. When it passes, it prints the exact command for step 3,
   with the slope, intercept, residual and span filled in.
3. **Build the record** with that command, adding the +5 V rail you measured at J21-1, any
   pairing number for this hybrid/board pair, and **which hybrid is fitted**:
   `python3 tools/pmgr_calib_image.py --a <slope> --b <offset> --resid <worst> --span <volts> --rail <+5V> --id <n> --hybrid <original|recreation> > calib.args`

   **`--hybrid` is required and has no default.**

   | Fitted hybrid | Choose | Low-battery warning | The Mac sleeps | Cut-off (the hybrid's own) |
   |---|---|---|---|---|
   | Apple's **original** | `original` | 5.90 V | 5.74 V | 5.65 V |
   | a **recreation** (e.g. Androda's) | `recreation` | ~6.10 V | ~6.02 V | ~5.92 V on the board tested |

   A recreation hybrid cuts the power at a higher voltage than Apple's, to protect the battery.
   With `recreation` the board warns and sleeps above that cut-off, and reports the battery level
   to the Mac on Apple's scale, so the Mac's own bars and warnings follow. ⛔ Choosing
   `original` with a recreation hybrid lets the machine die without warning; choosing
   `recreation` with an original hybrid sleeps it early.
4. **Flash with it**:
   `tools/pmgr_flash_with_pram.py firmware/pmgr.hex <md5-prefix> - calib.args`
   — it refuses a hex whose MD5 doesn't match, writes the record and reads it back.

⛔ **Every flash erases EEPROM.** Always flash through the tool with the record, or the board
silently returns to "no fast charge".

> ⛔ **Upgrading from 0.1.11 or earlier? Rebuild your `calib.args` first.** Records made before 0.1.12
> are **version 1** and do not say which hybrid is fitted; the firmware treats them as `original`.
> **With a recreation hybrid that loses the recreation thresholds — the machine then dies at the
> hybrid's cut-off with no warning.** Re-run step 3 with the same numbers plus `--hybrid recreation`
> (or `--hybrid original`). The flash tool now **refuses a version-1 record**; if your board really
> has an original hybrid you may instead add `--v1-is-original` to the flash command.
As after any programmer operation, **unplug the programmer and power-cycle** before starting the
machine (step 4).

⚠️ **Honest limitation:** step 1 currently needs the level the PMGR reports, which in development
was read by decoding the PMGR's battery reply with a logic analyser. A simpler way to read it is
an open problem — contributions welcome.

## 8. First charge — supervised

Pack voltage on a DMM, the machine in sight, and a limit you will act on. The original 3-cell Cyclon
pack should stay below **7.5 V**; abort at 7.6 V. See [`VERIFIED.md`](VERIFIED.md) for what charging
behaviour has and has not been measured.
