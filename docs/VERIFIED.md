# What has been verified

Unless a section says otherwise, everything below was measured on a **Macintosh Portable M5120 fitted
with this replacement**, against a second, **unmodified Portable** used as the reference. Since 0.1.15
it also runs a **PowerBook 100** — see [PowerBook 100](#powerbook-100) for what has been shown there.
**It has not been installed in a backlit Portable (M5126).** One machine of each is a small sample:
treat these as "shown to work here", not "guaranteed to work everywhere".

## ✅ Shown on hardware

**The Mac runs normally.**
- Cold boot, sleep and wake (by keypress), the reset switch, and shutdown — all behave as on the
  original, under **System 6.0.8 and 7.5.3**.
- The soft-reset command parks the machine exactly as the original does; a keypress then boots it.
- The ROM read-back the Mac performs answers correctly (with the synthetic image).
- PRAM is read, written and saved. Host writes to the battery thresholds are stored but not acted
  on — which is what the original chip does too, and was reproduced deliberately.

**Battery sensing.**
- Each machine's hybrid reads the battery slightly differently, so each unit gets a **16-byte
  calibration record** in EEPROM. On the development machine the fitted line held to about
  1.2 ADC counts across a 0.9 V sweep, and to about 0.6 counts on a later, independent battery test.
- **Without a valid record, fast charge is disabled** — confirmed: no fast-charge assertion in 36 of
  36 samples on a board with the record removed. Charging still continues on the slow path.

**Charging.**
- A supervised fast charge on a 3-cell Cyclon pack **entered fast charge, crossed the knee at about
  7.20 V, and terminated** — shown twice, once before and once after the battery-reading interlock
  was added.
- With the interlock, across a full charge and 42 minutes of float that followed, fast charge
  switched off **once**, at the expected point, with no unexpected transitions in 702 decoded samples.
- **Charge current**, with a clamp meter on the battery lead (one pack, single runs): about **0.30 A**
  at the start of fast charge, tapering to about 0.21 A by the knee with the Mac running; about
  **0.73 A** with the Mac asleep, which leaves the charger's whole output for the pack. In float, about
  0.08–0.10 A once settled.
- The two battery-reading fault pads (`DBG1`, `DBG2`) were read after a complete charge: **no fault**
  had occurred during it.

**Sleep with the charger.** *(fixed in 0.1.12 — see below)*
- A fast charge that **ends while the Mac is asleep stays off** after the Mac wakes, and a charger
  **plugged in while the Mac is asleep still fast-charges** after it wakes. Both were checked on the
  fast-charge line itself (hybrid pin 39) as well as in the PMGR's flags.

**Low battery on a real pack** (development machine, replacement hybrid, board set as `recreation`).
- Mac OS showed its three low-battery warnings, then put the machine to sleep itself, well above the
  hybrid's cut-off. With the charger connected, a keypress **resumed** it rather than restarting.

**The PWM output (contrast on the Portable).** *(fixed in 0.1.15 — see below)*
- M50753 pin 57 carries a **926–935 Hz** square wave at every slider position, with the duty cycle
  following the original's contrast table, (2 + 2X)/66: 96.3 / 48.1 / 15.0 / 5.6 % measured. No hang
  through repeated slider moves. The visible effect on a Portable LCD has not been seen: the
  development machine's original LCD is not connected.

**Other.**
- Current drawn with the Mac switched off: **about 9–11 mA**.
- A cursor flicker seen under 7.5.3 was traced to a system extension, not the PMGR: it disappears
  with extensions off, under 6.0.8, and appears on the unmodified machine too.
- Eleven host-side unit-test suites cover the protocol, PRAM save, ADC handling, calibration, the
  charging state machine and the PWM output (including the silicon erratum below).

## PowerBook 100

One PowerBook 100 (System 7.5.3, a BlueSCSI as its disk), with **the same boards and the same
firmware** as the Portable, on a bench supply at the battery contacts. Its original M50753 was measured
in place first, then removed and replaced by the stack at U22.

**✅ Shown**
- It **boots to the Finder** and its battery icon reads the same as with the original chip at the
  same voltage (4 of 8 bars at 6.40 V).
- The **brightness dial works** — dial → the PMGR's analogue input → Mac OS → the PMGR's PWM →
  the backlight. The contrast dial works too (it does not go through the PMGR).
- **Sleep and wake** by keypress.
- Mac OS's **whole low-battery sequence** — the bars stepping down, "reserve power", "very little
  remaining", then sleep — and a keypress resumes it afterwards.
- The **battery sense is the Portable's circuit and scale**: with the original chip, the reading at
  the PMGR's input matched the Portable's line within about 1 count at 6.20 and 6.40 V. So the
  Portable's calibration method and both `--hybrid` settings apply unchanged.
- The stack **fits under the CPU board**, on the same footprint as the Portable's. The supply groups
  and unused pads checked on the bare footprint match the Portable's.

**⚠️ Uncalibrated, it reads the battery about 0.05 V low.** Stepping a bench supply down 0.02 V a
minute, the same machine with its original chip and then with the replacement:

| event (Mac OS) | original M50753 | replacement, uncalibrated |
|---|---|---|
| 2 of 8 bars | 6.30 V | 6.28 V |
| "reserve power" | 6.24 V | 6.28 V |
| "very little remaining" | 6.14 V | 6.20 V |
| Mac OS sleeps | 6.06 V | 6.12 V |

The direction is the safe one (warnings come early). A calibration record
removes the offset, but that needs a battery to calibrate against — not done yet. Without a record
the firmware deliberately **does not fast-charge**.

**⬜ Not shown yet**
- **Charging** of any kind — there was no battery for it.
- **SCSI Disk Mode** with the HDI-30 SCSI Disk Adapter — **under review, with a likely fix.**
  - **How the original does it:** the adapter grounds pin 1 of the HDI-30 connector (Apple's
    *PowerBook 100 Developer Note*). On this logic board that pin reaches the PMGR's **pin 48**
    through about 91 Ω. The PMGR reports the pin as bit 5 of its register `$EE`.
  - **The ROM:** the PowerBook 100's ROM reads `$EE` at startup and enters disk mode when bit 5 is 0.
    Both are shown: in the ROM's code, and on the machine, where 0 gave disk mode and 1 a normal boot.
  - **On this board:** pin 48 is not connected to the AVR, so the replacement always reports 1. With
    the adapter plugged in, the machine boots normally instead of entering disk mode.
  - **Likely fix:** one wire from the interposer's pin 48 to a spare AVR pin, and a firmware change
    to read it. Boards without the wire keep booting normally. Not yet built or tested.
  - ⚠️ On **0.1.14 and earlier**, a PowerBook 100 *always* starts in disk mode. It shows a SCSI
    icon, or, at lower battery voltages, a battery that alternates with a warning-triangle battery.
- **A closed case** with the stack fitted, and long running.

**Bench notes.** The battery-bay plunger is the PowerBook 100's battery switch: with a bench supply
on the battery contacts, nothing powers up unless the plunger is held in. It drew 0.7–1.0 A at 6.2–6.4 V
with the Mac running; a 1.5 A limit was used. ⛔ With an **original M50753** still fitted, stay at or below **6.5 V**.

## Deliberate differences from the original

**PRAM survives power removal.** The original keeps PRAM in the chip's RAM, so taking all power away
clears it. This replacement keeps PRAM in the AVR's EEPROM, so it **comes back after the battery has
been pulled.** The clock still behaves like the original: a power loss zeroes it, and the Mac forgets
the date. (Keeping the clock in EEPROM was not an option — writing it every second would wear the
EEPROM out in about a day.)

| PRAM recovery | works? |
|---|---|
| `Cmd-Opt-P-R` at startup | ✅ unchanged |
| blank or corrupt PRAM — the Mac reinitialises it | ✅ unchanged |
| **pulling the battery / removing all power** | ⛔ **no longer clears PRAM** |

The case that is lost is narrow: PRAM that *looks* valid but holds settings that stop the Mac
booting. The fix is then the programmer, not the battery — see "Resetting PRAM" in
[`PROGRAMMING.md`](PROGRAMMING.md).

## ⛔ Fixed in 0.1.15 — update if you run 0.1.14 or earlier

**The PWM output was never driven.** M50753 pin 57 (the Portable's LCD contrast, the PowerBook 100's
backlight brightness) stayed low on every board. The AVR128DB has a silicon erratum (Microchip
DS80000915, revisions A4 and A5): the whole CCL peripheral must be disabled before a lookup table's
configuration changes, and the datasheet separately requires the table itself to be disabled when its
output enable changes. Earlier firmware broke the first rule, so the write that switches the output on
was silently ignored. 0.1.15 obeys both, and a unit test models both rules.

**Register `$EE` read as 0.** The Mac reads some of the PMGR's registers directly. `$EE` was not
answered, and on the PowerBook 100 a 0 in its bit 5 means "the SCSI Disk Adapter is plugged in:
start in SCSI Disk Mode". It now reads as on
the original (bit 5 = 1, bits 2 and 3 from the sound-latch and off-hook lines). The original's fixed
port-direction registers (`$E1 $E3 $E9 $EB`), the VIA bus and its direction (`$E4 $E5`) and the PWM
control bit (`$F5`) are answered too. No change was seen on the Portable.

**The battery-reading self-check uses an internal reference.** `DBG2` used to check the ADC against
an external line that depends on the machine; it now converts the AVR's own 2.048 V reference, so the
verdict means the same on both machines and is valid shortly after power is applied.

## ⛔ Fixed in 0.1.12 — update if you run 0.1.11 or earlier

**Fast charge was mishandled across sleep.** The fast-charge line shares a port with other lines
that the firmware restores when the Mac wakes, and the restore also put fast charge back to what it
was *before* the Mac slept:

- a charger plugged in while the Mac was asleep **did not fast-charge** after it woke (it still
  charged on the hybrid's slower path); and
- ⛔ a fast charge that **finished while the Mac was asleep was switched back on** when it woke, and
  stayed on until the charger was unplugged.

The original chip avoids this by updating its saved copy whenever it switches fast charge; 0.1.12
does the equivalent. **On 0.1.11 or earlier, do not let the Mac sleep during a fast charge.**

## ⚠️ Not yet shown — read this before relying on it unattended

- **The float level depends on the hybrid.** On the development machine's replacement hybrid the pack
  floats at about **7.21 V** (about 2.40 V per cell), set by the hybrid's own float regulator — a
  little above Apple's 6.94–7.16 V. An original hybrid floated about 0.23 V lower on the same pack.
  How the PMGR behaves over long float periods with an original hybrid has not been logged.
- **What switching off fast charge mid-charge does to the current** has not been measured. The design
  assumes it reduces the rate; that is supported by one older observation, on a different battery.
- **Low-battery timing on a replacement hybrid is set mostly by Mac OS.** 0.1.14 lowers the
  recreation levels to about 6.10 V (warning) and 6.02 V (sleep), from 6.20 / 6.05 V, which on a real
  pack warned with hours of use left. With the new levels, on two different packs, Mac OS warned and
  then put the machine to sleep itself about 25 minutes later, well before the PMGR's own sleep point
  and the hybrid's cut-off, and the machine resumed and charged normally afterwards. On one pack
  Mac OS warned before the PMGR's level was reached — so the warning can still come with plenty of
  running time left. Only these two packs, at a light load, have been tested.
- **The fast-charge timer's top-off** does not run for exactly as long as the code predicts, by an
  amount that varies between runs. Charging still terminates; the discrepancy is unexplained.
- **Standby current of the original chip** has not been measured for comparison, so whether 9–11 mA
  is acceptable for a machine left unplugged for weeks is open.
- **The PowerBook 100**: charging, calibration, SCSI Disk Mode and a closed case — see
  [PowerBook 100](#powerbook-100).

## About the development machine

The development Portable carries a **third-party replacement hybrid** (the power-supply module the
PMGR works with), made by Androda. It differs from an original hybrid in two ways, **both by its
design** and neither caused by this PMGR:

- **It cuts the machine off at a higher battery voltage — about 5.92 V** (an original: about
  5.65 V), to protect the lead-acid pack. That is above Apple's warning point, so with Apple's
  thresholds the machine would switch off with no warning. The calibration record therefore says
  which hybrid is fitted, and with `recreation` the PMGR moves its warning and sleep points above
  the cut-off (see [`REFERENCE-MEASUREMENTS.md`](REFERENCE-MEASUREMENTS.md)).
- **It regulates the float itself**, holding the fast-charge transistor (Q1) partly on, at about
  7.21 V at the pack.

If your machine has a replacement hybrid, set the calibration record to match it.
