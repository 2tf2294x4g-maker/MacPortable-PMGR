# What has been verified

Everything below was measured on a **Macintosh Portable M5120 fitted with this replacement**, against
a second, **unmodified Portable** used as the reference. **It has not been installed in any other model**
— not the backlit Portable (M5126), and not the PowerBook 100. One machine is a small sample: treat these as
"shown to work here", not "guaranteed to work everywhere".

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

**Other.**
- Current drawn with the Mac switched off: **about 9–11 mA**.
- A cursor flicker seen under 7.5.3 was traced to a system extension, not the PMGR: it disappears
  with extensions off, under 6.0.8, and appears on the unmodified machine too.
- Eight host-side unit-test suites cover the protocol, PRAM save, ADC handling, calibration and the
  charging state machine.

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
- **The low-battery warnings on a replacement hybrid may come early.** On a real pack, the first
  warning appeared within minutes of starting from a part-charged pack, and the machine then ran
  for about another hour before Mac OS put it to sleep, because the pack's voltage is nearly flat
  around 6.2 V under load. The levels may be lowered in a later version.
- **The fast-charge timer's top-off** does not run for exactly as long as the code predicts, by an
  amount that varies between runs. Charging still terminates; the discrepancy is unexplained.
- **Standby current of the original chip** has not been measured for comparison, so whether 9–11 mA
  is acceptable for a machine left unplugged for weeks is open.
- **The PowerBook 100** uses the same power manager and has not been tried.

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
