# Fuse policy — the configuration `pmgr.hex` assumes

> ⛔⛔ **Written 2026-09-07 after review. Nothing here has been programmed yet.**
> `make flash` writes the flash image and **nothing else**, so every board built
> before this document runs on **factory fuse defaults** — which are wrong for
> this design in at least one way that would be hard to diagnose.

⭐ **All values below are taken from the device header shipped with the
toolchain** (`avr/ioavr128db64.h`, avr-gcc 14.3.0), not from memory — the enum
names and shifts are quoted so any reader can re-derive the bytes.

---

## ⛔⛔⛔ `SYSCFG0` WAS `0x08` AND WOULD HAVE BRICKED THE BOARD

> ⛔ **Caught by review 2026-09-07, before any hardware existed to suffer it.**
> The first version of this document specified `SYSCFG0 = 0x08`.

```
  CRCSRC is bits [7:6]      CRCSRC_NOCRC_gc = (0x03<<6) = 0xC0
  so leaving them ZERO   =  CRCSRC_FLASH_gc = CRC over the ENTIRE flash,
                            checked at every reset
```

⛔⛔ **`pmgr.hex` carries no such checksum.** The CRCSCAN would fail and the part
would be held before it ever reached `main()`. ⚠️ **The board would have looked
completely dead** — and on a first article the first suspect is a `DF12` joint or
`U1` alignment, so this would have cost a rework of a perfectly good board.

### ⭐⭐ Root cause, which matters more than the fix

Each byte was composed **from the bits being set**, leaving every other field at
zero on the assumption that zero is the neutral value. ⛔ For `CRCSRC`, zero is
the **enabled** value.

⭐⭐⭐ **The factory default is `0xC0`, which already selects `CRCSRC_NOCRC`.**
This project deliberately changes **only `RSTPINCFG`**, producing **`0xC8`**.

```
  0xC0   factory default   CRCSRC = NOCRC, reset pin = GPIO, EESAVE clear
  0xC8   ours              the same, plus RSTPINCFG = RST
  0x08   what was written  ⛔ CRCSRC CLEARED to 00 = CRC on full flash
```

⛔⛔ **So the defect was writing a byte that silently *undid* a safe default.**
Leaving the fuse completely alone would have booted; the hand-composed
"deliberate" value would not. ⚠️ This document argues that a fuse left at its
default is an unmade decision — still true — and the correction is that a
hand-composed value which clears an unrelated field is worse than either.

> ⛔ **The rule that follows: start from the factory default and change only the
> fields you intend.**

⭐ **`RSTPINCFG = RST` is a real choice, not a leftover.** `PF6` is reserved as
`RESET` on this board and is **not used as a GPIO** — `MCU-BOARD-SCHEMATIC.md`:31
lists it as *"RESET, input only"*. The default would leave it a floating input
pin with no reset function.

⚠️ **This section said the default was `0xC8` until 2026-09-07.** The conclusion
was right and the number was wrong — which is its own small lesson, since the
number was the part that could have been checked and was not. ⭐ The datasheet
states it directly (DS40002247, the `SYSCFG0` fuse table: default `0xC0`), along
with the failure mode: *"Normal code execution does not start. The CPU will hang
executing no code."*

### ✅ The other four were audited when this was found

| fuse | is zero neutral for every unset field? |
|---|---|
| `WDTCFG` | ✅ `PERIOD`/`WINDOW` are both `OFF` at 0 |
| `BODCFG` | ✅ default `SAMPFREQ` (128 Hz) is 0, intended; `SLEEP` is set explicitly (`SAMPLED`, since 2026-09-14) |
| `SYSCFG1` | ✅ only `MVSYSCFG` and `SUT`; both explicitly set |
| `CODESIZE`/`BOOTSIZE` | ✅ 0 means no boot/app split |

⭐⭐ **And the arithmetic is now executed, not trusted.** `tools/check_docs.py`
`check_fuses()` names **every field of every fuse** and recomputes each byte; a
value that does not equal the OR of its named fields fails the build, and so does
a `FUSES.md` that disagrees with the Makefile. ⛔ That check exists because this
defect was arithmetic done in prose.

---

## ⛔⛔ THE ONE THAT WOULD ACTUALLY BITE IN THE FIELD: `MVSYSCFG`

```
  MVSYSCFG_DUAL_gc   = (0x01<<3)     <- FACTORY DEFAULT
  MVSYSCFG_SINGLE_gc = (0x02<<3)     <- what this board is
```

⭐ **This board is single-supply.** `MCU-BOARD-SCHEMATIC.md`:187 already says so
and ties `VDDIO2` (pin 20) to +5 V with its own decoupling pair. ⛔ **The
hardware was done right and the fuse was never chosen** — it is still at the
factory `DUAL` value, which tells the MVIO block to treat `VDDIO2` as an
independent supply with its own detection.

⛔⛔ **`PORTC` is not spare on this board.** It carries four real signals:

| pin | signal | why it matters |
|---|---|---|
| **PC2** | ⛔⛔ **`SIXTYHZ`** | **the 60 Hz tick** — the heartbeat behind `pmgr_tick_1s()`, the battery logic, and the two-edge wait in `pmgr_system_release()` |
| **PC4** | `PMGCLK_F` | the host clock input |
| PC0 | `SOUND_LATCH` | |
| PC1 | `OFF_HOOK*` | |

⚠️⚠️ **The failure mode is "boots but subtly wrong", which is the worst kind.**
The firmware has a *nominal-period fallback if `SIXTYHZ` is absent*, so a
`PORTC` problem does not stop the machine — it silently degrades the timebase,
and the 1 Hz tick that drives all battery behaviour goes with it. ⛔ No amount of
solder inspection finds a fuse.

⭐ **Set it deliberately.** Even if `DUAL` happens to work with `VDDIO2` tied to
`VDD`, leaving a supply-topology fuse at a value that contradicts the schematic
is an unmade decision on the port carrying the heartbeat.

---

## The full policy

| fuse | offset | value | why |
|---|---|---|---|
| `WDTCFG` | `0x00` | **`0x00`** | ⛔ **watchdog OFF, deliberately** — see below |
| `BODCFG` | `0x01` | **`0x66`** | BOD on at **2.85 V while active**, and **sampled (128 Hz) in sleep** - the park sleeps since 2026-09-14 (MEASUREMENTS 2.199). ⛔ Was `0x64` (disabled in sleep) until then |
| `OSCCFG` | `0x02` | *leave* | the design runs from the internal high-frequency oscillator configured in software; no fuse dependency |
| `SYSCFG0` | `0x05` | **`0xC8`** | ⛔⛔ **`CRCSRC = NOCRC`** (the `0xC0`), reset pin = `RST`, ⭐ `EESAVE` **clear** on purpose |
| `SYSCFG1` | `0x06` | **`0x14`** | ⛔ **`MVSYSCFG = SINGLE`** + **8 ms startup** |
| `CODESIZE` | `0x07` | **`0x00`** | no boot/app split; single application section |
| `BOOTSIZE` | `0x08` | **`0x00`** | same |

### How each byte is built

```
  BODCFG  0x66 = LVL_BODLEVEL3_gc (0x03<<5 = 0x60)   2.85 V
               | ACTIVE_ENABLE_gc  (0x01<<2 = 0x04)   continuous while running
               | SLEEP_SAMPLED_gc  (0x02<<0 = 0x02)   sampled while the park sleeps - see the note below

  SYSCFG0 0xC8 = CRCSRC_NOCRC_gc   (0x03<<6 = 0xC0)  ⛔⛔ NO startup CRC scan
               | RSTPINCFG_RST_gc  (0x02<<2 = 0x08)   PF6 is RESET, not GPIO
               | CRCSEL            (bit 5)            don't-care once NOCRC
               | EESAVE clear      (bit 0 = 0)        chip erase WIPES EEPROM

  SYSCFG1 0x14 = MVSYSCFG_SINGLE_gc (0x02<<3 = 0x10)  single supply
               | SUT_8MS_gc        (0x04<<0 = 0x04)   8 ms startup delay
```

---

## ⛔ Watchdog: OFF, and this is a decision, not a default

⭐⭐ **A watchdog reset turns the Macintosh off.** `SYS_PWR*` is released on
reset, so a WDT firing while the machine is running and healthy is
indistinguishable to the user from the power switch being pulled.

⚠️ And the firmware's parked loop is **an intentional busy loop polling 60 Hz
edges**, so a watchdog would have to be petted from inside it — meaning any
legitimate long operation risks a reset that costs the user their session.

⛔ **The trade is bad in this application.** A hung PMGR leaves a machine that
will not respond; a spurious WDT reset powers off a machine mid-use. ⭐ Revisit
only with a specific hang in evidence, and then with the pet points chosen
against a captured trace, not by inspection.

## ⭐⭐ Brownout: ON — and here is why it is NOT the same trade as the watchdog

⚠️ The obvious objection is that a BOD reset also turns the machine off. ⭐ **It
does not, in the case that matters**, and the distinction is clean:

```
  WATCHDOG fires   while VDD is perfectly healthy   -> causes a shutdown that
                                                       would NOT have happened
  BOD fires        only when VDD < 2.85 V           -> the machine's own 5 V
                                                       rail has already failed
```

⭐⭐⭐ **At 2.85 V the Macintosh is going off regardless.** What BOD buys is that
the AVR stops executing rather than running undefined code while driving
`SYS_PWR*`, the rail-enable lines and `PMGR_ADB` — outputs that reach transistor
bases directly (2.58). ⛔ **Undefined execution on a collapsing rail is the one
state where this chip can do something the original never would.**

⚠️ **What BOD does NOT buy, stated so nobody over-reads it:** 24 MHz operation
requires **VDD ≥ 4.5 V**. A BOD at 2.85 V is far below that, so there is a band
between 2.85 V and 4.5 V where the part is out of spec and BOD is silent. ⛔ BOD
is protection against *collapse*, not a guarantee of valid operation.

✅ **REVISITED 2026-09-14 (MEASUREMENTS 2.199):** the park now sleeps in POWER-DOWN between 60 Hz ticks, so `BODCFG` is
`0x66` - `SLEEP_SAMPLED`. Written and verified on Portable #1's board the same day (was `0x64`); the text below is the original
reasoning, kept. ⚠️ A board still carrying `0x64` runs the sleeping park with no brownout detection while parked - reprogram the fuse.

~~⭐ `SLEEP_DISABLE` is chosen because **the firmware does not yet sleep**~~ — the
parked loop ran at full speed. ⛔⛔ **This is the fuse to revisit the moment real
sleep lands** (see the parked-current stage in `BRING-UP.md`): with a genuine
Power-Down mode, `SLEEP_SAMPLE_gc` (`0x02`) trades a little current for keeping
brownout coverage while parked, and leaving `DISABLE` there would remove
protection during the state the PMGR spends most of its life in.

## ⭐ Startup delay: 8 ms, and it is board-specific

⛔ Factory default is `SUT_0MS`. ⭐ At power-on this AVR starts driving the
Macintosh's control lines as soon as it executes, and `pmgr_handlers_init()`
establishes rail states immediately. **8 ms gives the +5 V always-on rail time to
settle before the PMGR begins asserting anything into the machine.**

⚠️ Not a large number and not a magic one — it is one oscillator-settling
multiple above the smallest useful value. ⛔ **If first power-on shows anything
odd in the first instants, this is a knob**, and a captured `SYS_PWR*` trace
should set it rather than taste.

## ⭐ `EESAVE`: clear, on purpose

⚠️ Clearing it means **a chip erase takes EEPROM with it**, so `make flash`
wipes PRAM. ⭐ That is deliberate and already reasoned in `ROADMAP-V2.md`:
because our PRAM lives in EEPROM rather than volatile RAM, **"reflash the
firmware" is the PRAM-zap of last resort** — the replacement for pulling the
battery, which no longer clears anything.

⛔ Setting `EESAVE` would preserve settings across a firmware update and
**remove the only recovery path** from a PRAM state that prevents booting. The
two cannot both be true; this is the side chosen.

---

## Programming, with verification

⛔⛔ **Write-then-verify, never write-and-assume.** A fuse that did not take is
silent, and every consequence above is a silent one.

### ⛔⛔ IF YOU PROGRAM WITH THE XGecu / Xgpro INSTEAD, EVERY GUARD HERE IS BYPASSED

⭐ The `T48` is a legitimate second path and is the **known-good** programmer of
the two — worth having when a Tigard-as-UPDI rig is improvised. ⚠️ But it does not
go through `make fuses`, and therefore not through **any** of the protection this
project built:

| guard | protects the Makefile path | protects Xgpro |
|---|---|---|
| `check_docs.py` `check_fuses()` — recomputes every byte from named fields | ✅ | ⛔ **no** |
| `make fuses` write-then-**verify** with a non-zero exit | ✅ | ⛔ **no** |
| `FUSES.md` and the Makefile forced to agree | ✅ | ⛔ **no** |

⛔⛔ **AND THE P0 LIVES IN A FUSE FIELD A GUI WILL PRESENT AS A DROPDOWN.**
`SYSCFG0`'s `CRCSRC` must be **`NOCRC`**. Left at the value a tool happens to
default to, it selects **CRC over the entire flash**, `pmgr.hex` carries no such
checksum, and the part **hangs before `main()`** — a board that looks completely
dead while every joint is perfect.

⭐⭐ **So if you set fuses in Xgpro, read back the raw bytes and compare them to
this table by eye**, whatever the GUI calls the fields:

```
  WDTCFG   0x00        SYSCFG0  0xC8      <- the 0xC0 is CRCSRC=NOCRC. NOT optional.
  BODCFG   0x66        SYSCFG1  0x14
  CODESIZE 0x00        BOOTSIZE 0x00
```

⚠️ A GUI that shows "CRC: enabled/disabled" is doing the same arithmetic this
document does — but nothing checks that your reading of it matches.

### First, the tool

```bash
pip3 install --user pymcuprog
```

⚠️ **pip installs the script somewhere that is usually NOT on `PATH`** — on this
Mac, `~/Library/Python/3.11/bin` — and says so in a warning most people scroll
past. ⭐ `make fuses` and `make flash` both check for it first and print the fix,
because a bare *"command not found"* reads like a broken Makefile.

```bash
pymcuprog --version        # confirm before you need it
```

### Then

```bash
make -C firmware fuses PORT=/dev/tty.wchusbserial-XXXX
make -C firmware flash PORT=/dev/tty.wchusbserial-XXXX
```

⭐ `fuses` writes each value, then verifies **each one against the device** and
fails loudly on any mismatch, then prints all nine fuse bytes for the record.
Run it **once per board, before `make flash`**, and paste the output into that
board's bring-up notes.

⚠️ **Order matters:** fuses first, then flash. `make flash` performs a chip erase,
which does not clear fuses — but doing fuses first means the very first code the
part ever runs is running under the intended configuration.

### ⛔ How the verification works, and one way it was already wrong

⭐⭐ Verification uses **`pymcuprog verify -l`**, which reads the device and
compares in-tool — `utils.compare()` raises on mismatch and the process exits
non-zero. ⛔ **No output parsing is involved, deliberately.**

⚠️ **The first version of this target parsed `pymcuprog read` output and was
broken.** `showdata()` prints:

```
  0x001280: xx 64 xx xx xx xx ...
```

⛔ The **address** carries the `0x` prefix; the **data byte does not**. A regex
looking for `0x` followed by two hex digits matches `0x00` out of the *address*,
so every fuse would have reported a mismatch — a verify step that always fails is
as useless as one that always passes. ⭐ Found 2026-09-07 by rendering
`showdata()` directly, since there is no hardware here to run it against.

---

## ⛔ What has and has not been executed

| | |
|---|---|
| `pymcuprog` installed, runs, version reported | ✅ 3.19.4.61 |
| `-m fuses`, `-o`, `-l`, `-b` are real flags | ✅ checked against its own `--help` |
| non-zero exit on failure, so `\|\| exit 1` works | ✅ verified |
| `verify -l` raises on a wrong value | ✅ verified against `utils.compare()` |
| the target aborts on a failed write | ✅ verified with an absent device |
| both guard paths (no tool, no adapter) | ✅ verified |
| ⛔ **the happy path, against a real AVR** | ⛔⛔ **NEVER RUN — there is no hardware yet** |

⚠️ **Treat the first real `make fuses` as part of bring-up, not as a tested
step.** Everything above is verified as far as it can be without a chip on the
end of the wire, and no further.
