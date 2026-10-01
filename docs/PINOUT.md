> ⚠️ **2026-08-26:** `FDB` (M50753 pin 27) is **`ADB_In`** — Port 4 bit 1, the ADB
> receive line. ADB is two pins: `ADB_Out` (P40, our `PMGR_ADB`) and `ADB_In`
> (P41, our `FDB`). See `PORT-AUDIT.md`.

# Interposer Pin Mapping

M50753 footprint pin → signal → interposer connector position → AVR port.

Chip pin numbers from Apple schematic 050-0219 sheet 12. Signal names and unused pins confirmed against the PMU firmware disassembly.

> ⚠️ **Verify every chip pin number against sheet 12 before routing.** These were transcribed from a scan.

---

## Design principles

**VIAD0–7 must land on one complete AVR port, in bit order.** The data bus is read and written as a whole byte in the hottest path of the handshake; splitting it across ports costs instructions on every transfer.

**PMREQ must be on an interrupt-capable pin** — the host asserting PMREQ is what drives the whole exchange.

**Group the handshake outputs** (PMACK, PMINT) on one port so they can be updated without read-modify-write races.

**Connector positions are grouped by function**, not by chip pin order — it makes both the interposer routing and the MCU board routing cleaner than a straight 1:1 mapping.

---

> ⛔ **2026-08-28 — THE PER-GROUP TABLES BELOW ARE STALE IN TWO COLUMNS.**
> Only the **Chip pin** and **Signal** columns are current. Both the **Conn** and
> the **AVR** columns are superseded drafts and will send you to the wrong pad and
> the wrong AVR pin:
>
> | Column | Superseded by | Example of the error |
> |---|---|---|
> | **AVR** | the *Revised assignment (final)* table further down this file, and `firmware/include/pmgr_pins.h` | handshake + control is on **PORTB**, not PORTC. PORTC is the **MVIO** port and is left spare. VIA_TEST is **PB6**, not PC6 |
> | **Conn** | `PIN-MAPPING-PERIMETER.md` / `ROUTING-ANALYSIS.md` ("Conn (new)") | VIA_TEST is connector position **4**, not 15 |
>
> The file already warns, below the final table, that *"two earlier drafts of this
> table were wrong."* These are those drafts. They are kept because the chip-pin ↔
> signal transcription against sheet 12 is still the record; the routing is not.
> **`firmware/include/pmgr_pins.h` is authoritative for AVR pins.**

## Signals to route (45 of 60)

### Data bus — 8 lines · AVR PORTA (contiguous, bit order)
⛔ **`Conn` and `AVR` below are STALE DRAFTS** — see the warning at the top. Current: connector = `PIN-MAPPING-PERIMETER.md`, AVR pin = `MCU-BOARD-SCHEMATIC.md`.

| Chip pin | Signal | Conn | AVR |
|---|---|---|---|
| 5 | VIAD0 | 1 | PA0 |
| 4 | VIAD1 | 2 | PA1 |
| 3 | VIAD2 | 3 | PA2 |
| 2 | VIAD3 | 4 | PA3 |
| 1 | VIAD4 | 5 | PA4 |
| 60 | VIAD5 | 6 | PA5 |
| 59 | VIAD6 | 7 | PA6 |
| 58 | VIAD7 | 8 | PA7 |

### Handshake and system control — 8 lines · AVR PORTC
⛔ **`Conn` and `AVR` below are STALE DRAFTS** — see the warning at the top. Current: connector = `PIN-MAPPING-PERIMETER.md`, AVR pin = `MCU-BOARD-SCHEMATIC.md`.

| Chip pin | Signal | Dir | Conn | AVR | Note |
|---|---|---|---|---|---|
| 6 | PMREQ* | in | 9 | **PC0** | **interrupt-capable pin required** |
| 7 | PMACK* | out | 10 | PC1 | |
| 8 | PMINT* | out | 11 | PC2 | pulsed, not held |
| 12 | SYS_RST* | out | 12 | PC3 | releases CPU + gates ROM overlay |
| 13 | RESET* | out | 13 | PC4 | |
| 18 | PMGR_RESET* | in | 14 | PC5 | system can reset the PMGR |
| 11 | VIA_TEST | — | 15 | PC6 | |
| 9 | 1SEC* | out | 16 | PC7 | → VIA CA2 |

### Power control outputs — 8 lines · AVR PORTD
⛔ **`Conn` and `AVR` below are STALE DRAFTS** — see the warning at the top. Current: connector = `PIN-MAPPING-PERIMETER.md`, AVR pin = `MCU-BOARD-SCHEMATIC.md`.

| Chip pin | Signal | Conn | AVR |
|---|---|---|---|
| 37 | **SYS_PWR*** | 17 | PD0 |
| 38 | −5_EN | 18 | PD1 |
| 39 | SOUND_PWR* | 19 | PD2 |
| 40 | SERIAL_PWR* | 20 | PD3 |
| 41 | MODEM_PWR* | 21 | PD4 |
| 42 | HD_PWR* | 22 | PD5 |
| 43 | SCC_CNTRL | 23 | PD6 |
| 44 | IWM_CNTRL | 24 | PD7 |

### Status inputs — 7 lines · AVR PORTE/F
⛔ **`Conn` and `AVR` below are STALE DRAFTS** — see the warning at the top. Current: connector = `PIN-MAPPING-PERIMETER.md`, AVR pin = `MCU-BOARD-SCHEMATIC.md`.

| Chip pin | Signal | Conn | Note |
|---|---|---|---|
| 35 | AKD | 25 | any key down |
| 34 | STOP_CLK | 26 | |
| 33 | CHRG_ON* | 27 | charger present |
| 32 | KBD_RST* | 28 | resets keyboard M50740 |
| 31 | HICHG | 29 | |
| 30 | RING_DETECT | 30 | |
| 29 | MODEM_A/B | 31 | |

### ADB, display, misc — 6 lines
⛔ **`Conn` and `AVR` below are STALE DRAFTS** — see the warning at the top. Current: connector = `PIN-MAPPING-PERIMETER.md`, AVR pin = `MCU-BOARD-SCHEMATIC.md`.

| Chip pin | Signal | Conn |
|---|---|---|
| 28 | PMGR_ADB (out) | 32 |

> ⛔⛔ **MEASURED 2026-09-05 (`MEASUREMENTS.md` 2.48): `PMGR_ADB` is INVERTED,
> ~1 V, and OUTPUT-ONLY.**
>
> | `PMGR_ADB` | ADB bus |
> |---|---|
> | **LOW (0 V)** | **RELEASED** (high) — also the state while listening |
> | **HIGH (~0.6–1.0 V)** | **PULLED LOW** |
>
> ⭐ The ~1 V maximum is not a valid CMOS high: pin 28 drives a **transistor base
> or open-drain stage**, not a logic input. Confirmed output-only — when the
> keyboard drives the bus low, pin 28 stays at **0.00 V** rather than following
> it.
>
> ⛔ **Driving this push-pull with active-high = bus-high holds the ADB line low
> permanently.** That presents as a dead keyboard and mouse, with no obvious
> electrical fault to find.
| 27 | FDB (ADB in) | 33 |
| 26 | DISP_BLANK* | 34 |
| 25 | MODEM_INS* | 35 |
| 10 | SOUND_OFF | 36 |
| 57 | PMGR_PWM | 37 |

### Analogue and clocks — 6 lines
⛔ **`Conn` and `AVR` below are STALE DRAFTS** — see the warning at the top. Current: connector = `PIN-MAPPING-PERIMETER.md`, AVR pin = `MCU-BOARD-SCHEMATIC.md`.

| Chip pin | Signal | Conn | Note |
|---|---|---|---|
| 52 | **A/D_FILTER** | 38 | **ADC pin. Clamp on MCU board.** |
| 54 | PMGR_IN0 | 39 | |
| 51 | SOUND_LATCH | 40 | |
| 50 | OFF_HOOK | 41 | |
| 15 | 60HZ | 42 | → INT1 |
| 19 | PMGCLK_F | 43 | original Xin — **not needed**, AVR uses internal osc |

### Power — 2 lines
| Signal | Conn |
|---|---|
| VCC (always-on 5V) | 44, 45 |
| GND | 46, 47, 48 |

Spare / unrouted: 49, 50

---

## Pins deliberately NOT routed (15)

| Chip pin | Why |
|---|---|
| 36 (P10) | Firmware comments it **"NC — not connected, probably for testing"** |
| 49 (IN4) | PMGR_IN4 — unused by firmware |
| 48, 47, 46 (IN5–7) | unused |
| 20 (Xout) | crystal out — AVR uses internal oscillator |
| 22 (RSTout) | not needed |
| 14 (CNTR), 21, 24 (2PH) | timer/counter pins, unused in this application |
| 16 (INT2) | tied to RESET*, already routed as pin 13 |
| 23, 45, 53, 55, 56 | Vcc/Vss duplicates and no-connects — **verify against sheet 12** |

⚠️ **Route the castellation pads for these anyway** (mechanically they still land on the Mac's footprint) — just leave them unconnected on the interposer. Do **not** omit the physical pads.

---

## Connector

**50 positions used of 50.** Suggests the **0.4 mm Hirose DF40** (10 mm long) over the 0.5 mm part, since it opens the routing channel from 1.9 mm to 3.15 mm — enough for 15 traces per side on two layers at standard 6/6 mil rules.

Order positions so each functional group is contiguous, as above. That keeps interposer routing short and makes the MCU board's port assignment fall out naturally.

---

## AVR port assignment — REVISED against the datasheet (2026-08-24)

Verified from AVR128DB28/32/48/64 datasheet DS40002247B.

**AVR128DB64 ports:** PA[7:0], PB[7:0], PC[7:0], PD[7:0], PE[7:0], PF[6:0], PG[7:0] — **55 GPIO** (54 usable as outputs).

### Three datasheet findings that change the plan

**1. "External interrupt on all general purpose pins."** Any pin can take PMREQ — no constraint at all. My earlier note about needing an interrupt-capable pin was wrong.

**2. PF7 is dedicated UPDI, not a GPIO.** The port list is PF[**6**:0], so PF7 never appears as I/O. Nothing to "reserve" — it simply isn't available. **PF6 is RESET and is input-only.**

**3. ⚠️ PORTC is the MVIO port** — powered from a separate **VDDIO2** pin. If VDDIO2 is not connected, **PORTC does not work.** The datasheet requires it to be treated as a full supply pair with its own decoupling.

**4. ⚠️ The ADC is on PORTD.** The Power Domain Overview (datasheet Figure 5-1, §5.1) shows **PD[7:0] → ADC**. PE[7:0] feeds the DAC / AC / OPAMP block; PF[1:0] goes to XOSC32K. So the analogue input must land on **PORTD**, not PORTF.

### Revised assignment (final)

| AVR port | Use | Notes |
|---|---|---|
| **PORTA[7:0]** | **VIAD0–7** | contiguous, bit order — whole-byte bus access |
| **PORTB[7:0]** | **handshake + control** | PMREQ, PMACK, PMINT, SYS_RST, RESET, PMGR_RESET, VIA_TEST, 1SEC. VDDIO domain — no MVIO dependency |
| **PORTG[7:0]** | power control outputs | written as a group |
| **PORTE[7:0]** | status inputs | AKD, STOP_CLK, CHRG_ON, KBD_RST, HICHG, RING_DETECT, MODEM_A/B |
| **PD0** | **A/D_FILTER** | **ADC input — PORTD is the ADC port.** Confirm the AIN channel number in the ADC chapter |
| PD1–PD7 | ADB, display, misc | PMGR_ADB, FDB, DISP_BLANK, MODEM_INS, SOUND_OFF, PMGR_PWM |
| PORTC | **spare** | MVIO port — usable, but requires VDDIO2 |
| PF[6:0] | mostly unused | PF[1:0] = XOSC32K, PF6 = RESET (input only) |
| PF7 | UPDI | not GPIO |

⚠️ Two earlier drafts of this table were wrong: the handshake was on PORTC (MVIO) and then on PORTD (the ADC port). **This version is the one to build from.**

**45 signals into 55 GPIO** — comfortable margin, and PORTC stays free as spare.

### Hardware requirements from §4 of the datasheet

- **Decoupling per supply pin pair:** 100 nF primary + 1–10 nF HF, optional 1 µF. Same side of the board as the MCU, traces as short as possible, capacitors first in the power chain.
- **All VDD pins are internally connected** and must all see the same voltage.
- **AVDD** is internally tied to VDD on this device but still needs its own decoupling.
- **VDDIO2** — tie to VDD (5V) and decouple it, even if PORTC is unused. The datasheet is explicit (§5.3): *"If the device is configured in single-supply mode, the VDDIO2 voltage must also rise closely to VDD."*
- **AVDD must ramp with VDD** (§5.3): *"The AVDD voltage must ramp up closely with the VDD voltage during power-up to ensure proper operation."* Tie them together at the board level.
- GND is common to VDD, AVDD and VDDIO2.
- **RESET** has an internal pull-up; an external one is not required. If you fit a reset switch, use 330 Ω in series with the switch and 100 nF to ground.
- ⚠️ **UPDI can be enabled via a high-voltage pulse on RESET** — so keep anything fragile off that pin.

### UPDI header

The datasheet recommends **UPDI Connection v1: a 2×3 100-mil header**, even though only three pins are used, because most programming tools ship with 2×3 connectors. On a 25–30 mm MCU board that fits easily — use it rather than bare pads.

(UPDI v2 adds RESET to the connection; v1 is fine for this.)
