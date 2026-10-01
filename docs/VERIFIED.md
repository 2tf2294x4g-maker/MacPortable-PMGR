# What has been verified

Everything below was measured on a **Macintosh Portable fitted with this replacement**, against a
second, **unmodified Portable** used as the reference. One machine is a small sample: treat these as
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

**Other.**
- Current drawn with the Mac switched off: **about 9–11 mA**.
- A cursor flicker seen under 7.5.3 was traced to a system extension, not the PMGR: it disappears
  with extensions off, under 6.0.8, and appears on the unmodified machine too.
- Eight host-side unit-test suites cover the protocol, PRAM save, ADC handling, calibration and the
  charging state machine.

## ⚠️ Not yet shown — read this before relying on it unattended

- **Charge current has never been measured.** The voltages behave correctly, but the actual current
  profile through a charge is unknown.
- **Float is a single 42-minute observation**, voltage only. In it the pack stayed near 7.23 V
  (about 2.41 V per cell, above the Cyclon float band). Whether that settles lower, and what current
  sustains it, is not known.
- **What switching off fast charge mid-charge does to the current** has not been measured. The design
  assumes it reduces the rate; that is supported by one older observation, on a different battery.
- **A brief battery-reading fault during charging** would not necessarily show in the data that was
  logged; the firmware latches such faults on two test pads (`DBG1`, `DBG2`) for that purpose, but
  they have not yet been watched through a charge.
- **The fast-charge timer's top-off** does not run for exactly as long as the code predicts, by an
  amount that varies between runs. Charging still terminates; the discrepancy is unexplained.
- **Standby current of the original chip** has not been measured for comparison, so whether 9–11 mA
  is acceptable for a machine left unplugged for weeks is open.
- **The PowerBook 100** uses the same power manager and has not been tried.

## About the development machine's hybrid

The development Portable carries a **third-party replacement hybrid** (the power-supply module the
PMGR works with). Compared with an original hybrid, that one cuts the machine off at a higher
battery voltage (about 5.94 V) and holds the fast-charge bypass transistor (Q1) switched on with no
charger connected. Both **appear to come from that hybrid rather than from this PMGR** — every
PMGR-driven line measured the same as on the original machine — but **neither is settled**, and both
are being followed up with the hybrid's maker. They are
noted here because if your machine has a replacement hybrid, its charging and cut-off may differ
from an original's too.
