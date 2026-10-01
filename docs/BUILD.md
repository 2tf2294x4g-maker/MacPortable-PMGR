# Building one

This is the route from bare boards to a working machine. Each step points to the detailed
document; **[`ASSEMBLY.md`](ASSEMBLY.md) is the bench-tested procedure and wins wherever the two
disagree.** Some detailed documents mention entries in the development log, which is not part of
this repository.

Read the safety notes in the top-level [`README`](../README.md) first.

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

1. **Collect pairs**: the pack voltage (DMM, at the pack terminals) against the level the PMGR
   reports, across roughly 5.8–7.2 V, in both directions.
2. **Fit and judge it**: `tools/pmgr_calib_fit.py <pairs.csv>`. It rejects a fit whose worst residual
   exceeds 2 counts, or whose span is too narrow to test the line.
3. **Build the record**:
   `tools/pmgr_calib_image.py --a <slope> --b <offset> --resid <worst> --span <volts> --rail <+5V> --id <n> > calib.args`
4. **Flash with it**:
   `tools/pmgr_flash_with_pram.py firmware/pmgr.hex <md5-prefix> - calib.args`
   — it refuses a hex whose MD5 doesn't match, writes the record and reads it back.

⛔ **Every flash erases EEPROM.** Always flash through the tool with the record, or the board
silently returns to "no fast charge".
As after any programmer operation, **unplug the programmer and power-cycle** before starting the
machine (step 4).

⚠️ **Honest limitation:** step 1 currently needs the level the PMGR reports, which in development
was read by decoding the PMGR's battery reply with a logic analyser. A simpler way to read it is
an open problem — contributions welcome.

## 8. First charge — supervised

Pack voltage on a DMM, the machine in sight, and a limit you will act on. The original 3-cell Cyclon
pack should stay below **7.5 V**; abort at 7.6 V. See [`VERIFIED.md`](VERIFIED.md) for what charging
behaviour has and has not been measured.
