# PMGR MCU Board — Schematic Specification

Everything needed to draw the MCU board in KiCad. All library names verified present in KiCad 9.

---

## Components

| Ref | Value | KiCad symbol | Footprint |
|---|---|---|---|
| **U1** | AVR128DB64-I/PT | `MCU_Microchip_AVR_Dx:AVR128DB64x-xPT` | `Package_QFP:TQFP-64_10x10mm_P0.5mm` *(already set in the symbol)* |
| **J1** | Hirose DF12C3.0-50DS-0.5V | `Connector_Generic:Conn_02x25_Odd_Even` | `Connector_Hirose:Hirose_DF12_DF12C3.0-50DS-0.5V_2x25_P0.50mm_Vertical` — **stock KiCad 9, no custom lib** |
| **J2** | UPDI header 2×3 | `Connector_Generic:Conn_02x03_Odd_Even` | `Connector_PinHeader_2.54mm:PinHeader_2x03_P2.54mm_Vertical` |
| C1–C4 | 100 nF | `Device:C` | 0402 or 0603 |
| C5–C7 | 10 nF | `Device:C` | 0402 or 0603 |
| C8 | 10 µF | `Device:C` | 0805 |
| R1 | 1 kΩ | `Device:R` | 0402/0603 — A/D series resistor |
| D1 | BAT54S | `Diode:BAT54S` | SOT-23 — A/D clamp |

---

## AVR128DB64 pin numbers (TQFP-64, from the KiCad symbol)

| Port | Pin numbers |
|---|---|
| **PORTA** | PA0=**62** PA1=**63** PA2=**64** PA3=**1** PA4=**2** PA5=**3** PA6=**4** PA7=**5** |
| **PORTB** | PB0=8 PB1=9 PB2=10 PB3=11 PB4=12 PB5=13 PB6=14 PB7=15 |
| PORTC | PC0=16 PC1=17 PC2=18 PC3=19 PC4=22 PC5=23 PC6=24 PC7=25 |
| **PORTD** | PD0=26 PD1=27 PD2=28 PD3=29 PD4=30 PD5=31 PD6=32 PD7=**33** *(VREFA/PD7)* |
| **PORTE** | PE0=36 PE1=37 PE2=38 PE3=39 PE4=40 PE5=41 PE6=42 PE7=43 |
| PORTF | PF0=**44** PF1=**45** PF2=46 PF3=47 PF4=48 PF5=49 PF6=**50** *(RESET, input only)* |
| **PORTG** | PG0=52 PG1=53 PG2=54 PG3=55 PG4=58 PG5=59 PG6=60 PG7=61 |
| Power | VDD=**6** GND=**7** AVDD=**34** AGND=**35** VDDIO2=**20** |
| Program | UPDI=**51** |

⚠️ **PA0/PA1 (62/63) are the XTALHF pins** and **PF0/PF1 (44/45) are XTAL32K** — all fine as GPIO here since we use the internal oscillator, but don't fit crystals.

---

## Net list

> **Connector positions updated 2026-08-25** for the routing remap (`ROUTING-ANALYSIS.md`). AVR port grouping is unchanged — it is preserved by MCU-board routing, not by connector numbering. Previous version: `MCU-BOARD-SCHEMATIC.md.bak-perimeter`.

### Data bus — PORTA

**All eight on one port — required.** The protocol handshake depends on reading the bus in a single instruction.

| Net | AVR pin | TQFP | J1 |
|---|---|---|---|
| VIAD0 | PA0 | 62 | **30** |
| VIAD1 | PA1 | 63 | **29** |
| VIAD2 | PA2 | 64 | **28** |
| VIAD3 | PA3 | 1 | **27** |
| VIAD4 | PA4 | 2 | **26** |
| VIAD5 | PA5 | 3 | **33** |
| VIAD6 | PA6 | 4 | **34** |
| VIAD7 | PA7 | 5 | **35** |

### Handshake and control — PORTB

| Net | AVR pin | TQFP | J1 |
|---|---|---|---|
| PMREQ_N | PB0 | 8 | **31** |
| PMACK_N | PB1 | 9 | **32** |
| PMINT_N | PB2 | 10 | **1** |
| SYS_RST_N | PB3 | 11 | **5** |
| RESET_N | PB4 | 12 | **6** |
| PMGR_RESET_N | PB5 | 13 | **8** |
| VIA_TEST | PB6 | 14 | **4** |
| ONESEC_N | PB7 | 15 | **2** |

### Power control — PORTG

| Net | AVR pin | TQFP | J1 |
|---|---|---|---|
| SYS_PWR_N | PG0 | 52 | **17** |
| MINUS5_EN | PG1 | 53 | **16** |
| SOUND_PWR_N | PG2 | 54 | **46** |
| SERIAL_PWR_N | PG3 | 55 | **45** |
| MODEM_PWR_N | PG4 | 58 | **44** |
| HD_PWR_N | PG5 | 59 | **43** |
| SCC_CNTRL | PG6 | 60 | **42** |
| IWM_CNTRL | PG7 | 61 | **41** |

### Status inputs — PORTE

| Net | AVR pin | TQFP | J1 |
|---|---|---|---|
| AKD | PE0 | 36 | **18** |
| STOP_CLK | PE1 | 37 | **19** |
| CHRG_ON_N | PE2 | 38 | **20** |
| KBD_RST_N | PE3 | 39 | **21** |
| HICHG | PE4 | 40 | **22** |
| RING_DETECT | PE5 | 41 | **15** |
| MODEM_AB | PE6 | 42 | **14** |

### ADB, display, misc — PORTD

| Net | AVR pin | TQFP | J1 |
|---|---|---|---|
| PMGR_ADB | PD1 | 27 | **13** |
| FDB | PD2 | 28 | **12** |
| DISP_BLANK_N | PD3 | 29 | **11** |
| MODEM_INS_N | PD4 | 30 | **10** |
| SOUND_OFF | PD5 | 31 | **3** |
| PMGR_PWM | PD6 | 32 | **36** |

### Analogue and remaining — PORTD/PORTC

> `PMGCLK_F` is **monitor-only**. It is M50753 pin 19 (Xin) and the AVR runs from its internal 24 MHz oscillator. It is wired to PC4 so firmware can observe it, but the AVR accepts an external clock **only on PA0**, which is VIAD0 — so it can never be used as a clock source.

| Net | AVR pin | TQFP | J1 |
|---|---|---|---|
| AD_FILTER | PD0 | 26 | **38** |
| PMGR_IN0 | PD7 | 33 | **37** |
| SOUND_LATCH | PC0 **⚠️ MVIO** | 16 | **39** |
| OFF_HOOK | PC1 **⚠️ MVIO** | 17 | **40** |
| SIXTYHZ | PC2 **⚠️ MVIO** | 18 | **7** |
| PMGCLK_F | PC4 **⚠️ MVIO** | 22 | **9** |

### Debug indicators — PORTF ⭐ ADDED 2026-08-28

| Net | AVR pin | TQFP | J1 |
|---|---|---|---|
| DBG0 | PF5 | 49 | *(test pad)* |
| DBG1 | PF4 | 48 | *(test pad)* |
| DBG2 | PF3 | 47 | *(test pad)* |

⚠️ **These were missing from this document while the firmware already drove
them.** Found by cross-checking `firmware/include/pmgr_pins.h` against this net
list, 2026-08-28. PF3-PF5 are free (only PF0/PF1 and PF6 are reserved), so there
is no conflict — but there were no PADS, and that defeats the purpose.

**They must be brought out to test pads.** The whole design intent is that they
are readable with a DMM the instant the AVR is programmed — no host, no
analyser, no ADB (see the header comment in `firmware/src/pmgr_adc.c`). On a
20 x 23.5 mm board inside a closed Portable, an un-padded TQFP pin cannot be
probed, so the diagnostic path simply does not exist.

What they report in the NORMAL build:

| Pin | HIGH means |
|---|---|
| DBG2 | ADC scale self-check PASSED (`ref5V_Level` ≈ **113**) |
| DBG1 | ⛔ an ADC conversion TIMED OUT — every reading since is a stand-in |
| DBG0 | ⛔ RTC/PIT sync timed out |

⛔ **DBG2 is LOW until the machine has been switched on at least once**, and
that is correct, not a fault. AD_Ref sits on the switched `(5/0)` rail and reads
~0 V while the machine is off, so the scale cannot be verified at power-on — the
check is deferred to the 1 Hz tick and latches its pass the first time the rail
is up. ⚠️ This supersedes the claim that the check is *"measurable the instant
the AVR is programmed"*: with no machine attached, DBG2 stays low forever.
`MEASUREMENTS.md` 2.56.

DBG2 high with DBG1 low is a healthy ADC. The SELFTEST build reuses all three as
a 3-bit result code instead — see `pmgr_selftest.h`.

1 mm round pads on the top side are enough; they are probed with a meter tip,
not clipped.

### Power — plane connections

**GND is a plane on both inner layers** — each pad simply vias down to it. **+5V is routed as ordinary traces** (only five connection points on the interposer).

| Net | J1 positions | How carried |
|---|---|---|
| +5V | **47, 48** | routed traces |
| GND | **23, 24, 25, 49** | In1.Cu + In2.Cu planes |
| *spare* | **50** | — |

---

## Power supply

```
+5V (always-on rail, from J1 pins 47/48)
 ├── VDD    (pin 6)   ── C1 100nF ── GND    + C5 10nF
 ├── AVDD   (pin 34)  ── C2 100nF ── GND    + C6 10nF
 ├── VDDIO2 (pin 20)  ── C3 100nF ── GND    + C7 10nF
 └── bulk             ── C8 10µF  ── GND
GND ── AGND (pin 35), GND (pin 7)
```

**Datasheet requirements (§4.2, §5.3):**
- All VDD/AVDD pins at the **same voltage**; AVDD must **ramp with VDD**
- In single-supply mode **VDDIO2 must rise with VDD** — tie it to +5V even though PORTC is lightly used
- One decoupling pair **per supply pin**, same side of the board, shortest possible traces, capacitors first in the power chain

---

## A/D clamp — the one improvement over Apple's design

The M50753 dies when the battery-sense line exceeds Vcc+0.3 V. Protect the input:

```
J1 pin 38 (AD_FILTER) ──[ R1 1k ]──┬── PD0 (pin 26, ADC)
                                    │
                                 D1 BAT54S
                              (dual Schottky)
                     pin 1 GND · pin 2 +5V · pin 3 ADC_IN
                     (one diode GND→ADC_IN, one ADC_IN→+5V)
```

R1 limits fault current; the BAT54S clamps to the rails. ⛔ Corrected 2026-09-14: this drawing said "anode→+5V, cathode→GND",
which is the reverse of a clamp; the layout was always right (MEASUREMENTS 2.192 correction). Measured the same day: pin 40 and the
reported level agree within 1 level, so R1 and D1 cost nothing. Sized so a hybrid swinging toward battery voltage can't damage the ADC input.

---

## UPDI header (J2, 2×3)

Datasheet §4.4.1 recommends a 2×3 100-mil header even though only three pins are used, because programming tools ship with 2×3 connectors.

| J2 pin | Net |
|---|---|
| 1 | UPDI → U1 pin 51 |
| 2 | +5V |
| **3** | ⭐ **`DBG0`** → U1 pad **49** (`PF5`) |
| **4** | ⭐ **`DBG1`** → U1 pad **48** (`PF4`) |
| **5** | ⭐ **`DBG2`** → U1 pad **47** (`PF3`) |
| 6 | GND |

> ⛔⛔ **CORRECTED 2026-09-09 FROM THE FABRICATED BOARD FILE.** This table said
> *"3, 4, 5 — no connect"*. **It was wrong.** `mcu-board/pmgr_mcu.kicad_pcb`
> routes all three to the `DBG` nets, pad for pad.
>
> ⚠️ **The error propagated the wrong way.** `ASSEMBLY.md` correctly said *"`J2`
> carries UPDI plus `DBG0/1/2`"*, and on 2026-09-08 that correct statement was
> **overridden** on the strength of this table — sending the operator to hunt for
> test pads that do not exist. ⭐ There are **no `TP` footprints on this board**;
> the header *is* the debug access.
>
> ⛔ **The lesson: the board file outranks the prose.** Both were available; the
> prose was believed.

Pins 2 and 6 are the standard VCC/GND positions on the AVR 6-pin header. **Confirm pin 1 for UPDI against your programmer** — Atmel-ICE and SNAP differ, and some tools expect UPDI on pin 5 (the RESET position).

---

## Not fitted

- **No crystal.** Internal oscillator does 24 MHz. Leave PA0/PA1 and PF0/PF1 as GPIO or unused.
- **No external RESET pull-up** — internal pull-up is sufficient (§4.3). Add a switch only if wanted: 330 Ω in series, 100 nF to GND.
- **PF6 (50) is RESET, input only** — leave it.

---

## J1 — board-to-board connector (RESOLVED 2026-08-25)

**Hirose DF12(3.0) series, 0.5 mm pitch, 50 position, 3.0 mm mated stack height.**

| Board | Part | KiCad footprint |
|---|---|---|
| **MCU board** (J1) | **DF12C3.0-50DS-0.5V** — socket | `Connector_Hirose:Hirose_DF12_DF12C3.0-50DS-0.5V_2x25_P0.50mm_Vertical` |
| **Interposer** (J1) | **DF12E3.0-50DP-0.5V** — header | `Connector_Hirose:Hirose_DF12_DF12E3.0-50DP-0.5V_2x25_P0.50mm_Vertical` |

Both footprints ship with KiCad 9 — nothing to source or draw.

### Land pattern — verified against the Hirose datasheet (doc A306, p.10)

Read directly from the *Recommended PCB Footprint* drawing and cross-checked against the KiCad footprints; **every value agrees**:

| Dimension | Datasheet | KiCad |
|---|---|---|
| Pitch (P) | 0.5 ± 0.02 | 0.5 ✓ |
| Pad width | 0.3 ± 0.02 | 0.3 ✓ |
| Pad length | 1.6 ± 0.02 | 1.6 ✓ |
| Gap between rows | 2.0 ± 0.05 | (3.6 − 1.6) = 2.0 ✓ |
| Row centre-to-centre | 3.6 (derived) | 3.6 ✓ |
| Contact span (B) | 12.0 | 12.0 ✓ |
| Body length (A) | 14.6 (DS) / 14.7 (DP) | — |
| Boss holes | ⌀0.6 +0.05/−0.03, D = 14.1 apart | not present — **these variants have no guide boss** |

### Pin numbering — confirmed

**Pins 1–25 run along one row, 26–50 along the other** (not odd/even staggered). The DP footprint is mirrored relative to DS, so when the two boards are stacked the pins mate **1↔1 … 50↔50** with no renumbering. The net list above can be used verbatim on both boards.

### Height budget — OK

DF12(3.0) adds **3.0 mm** between the two board faces. Total stack above the logic board:

```
interposer PCB (0.8–1.6) + 3.0 mm connector + MCU PCB (0.8–1.6)  ≈ 4.6–6.2 mm
```

The M50753 it replaces is ~2.0 mm tall, so this adds ~3–4 mm over stock.

**Available clearance = 15 mm — measured.** Datum: **logic board surface → the underside of the keyboard / top plastic** on the Macintosh Portable. That is the same datum the stack above is built from, so the comparison is direct:

```
                    Portable   PowerBook 100
available            15.0 mm      10.4 mm     <- measured 2026-09-06
needed                6.2 mm       6.2 mm     (worst case, 1.6 mm boards)
margin                8.8 mm       4.2 mm
                       2.4x         1.7x
```

⭐ **Both machines measured.** The PowerBook 100 is the **binding** one vertically
and still clears by 1.7×.

**Height is not a constraint on this design.** DF12(3.0) confirmed **on both
machines**; no need to consider the shorter stack heights. ⭐⭐ That matters beyond
height: `CLEARANCE-MEASUREMENTS.md` notes the Portable's 4.1 mm VIA clears by only
0.5 mm at DF12(3.0) and **collides at every shorter stack**, so a PB100 that had
needed a 2.5 or 2.0 mm connector would have forced the outline in on that side.
It does not.

*(If a future revision ever does need to come down, DF12 is made in 2.0 and 2.5 mm stack heights with an **identical land pattern** — part number only, never the layout.)*

---

## Checks before ordering

1. ~~Verify **J1 pin numbering**~~ — **done**, see above
2. Confirm the **AIN channel number for PD0** in the ADC chapter (needed for firmware, not layout)
3. ~~Re-check M50753 pin numbers on the interposer side against sheet 12~~ —
   ✅ **ALREADY DONE 2026-08-26, on two independent sources.** Castellation
   numbering validated against the **PowerBook 100 silkscreen**; signal names
   from the annotated photograph of the actual M50753 **cross-checked against
   Apple sheet 12** and the firmware disassembly. See `PIN-MAPPING-PERIMETER.md`
   and `PIN-AUDIT.md`.
   ⚠️ This box sat unticked and on 2026-08-28 sent someone to re-verify it. See
   the warning in `PORT-AUDIT.md`: **a document can be stale even when the
   project's knowledge is not — before treating an item here as open, check
   whether another file has since closed it.**
5. ⭐ **Add test pads for DBG0/DBG1/DBG2 (PF5/PF4/PF3)** — see the net list
   above. Without them the power-on diagnostics cannot be read on an assembled
   board, which is the only reason they exist.
6. ✅ **Firmware pin map cross-checked against this net list 2026-08-28** — all
   43 shared nets agree on port, bit and TQFP number, and all 39 J1 positions
   agree with `PIN-MAPPING-PERIMETER.md`. Re-run that check if either file
   changes; it is the mapping the firmware silently depends on
4. ~~Measure vertical clearance over the U15H footprint~~ — **done: 15 mm measured vs 6.2 mm needed**, 2.4× margin
