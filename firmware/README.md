# Firmware — AVR128DB64

Open replacement firmware for the M50753 power manager of the Macintosh Portable and PowerBook 100. It serves the Mac's
host protocol, runs the battery and charging logic autonomously, handles sleep, wake and reset, and
keeps PRAM in EEPROM.

## Build

```bash
brew trust osx-cross/avr                       # Homebrew gates third-party taps
brew install osx-cross/avr/avr-gcc@14
export PATH="/opt/homebrew/opt/avr-gcc@14/bin:$PATH"   # keg-only
make
```

`avr128db64` is supported natively by avr-gcc 14 — no Microchip device pack needed. The image size
depends on the compiler version (avr-gcc 14.3.0 builds 10715 bytes); never treat a size difference
between toolchains as a regression.

| target | |
|---|---|
| `make` | build `pmgr.hex` |
| `make fuses` | write and verify the fuses — **once per board, before the first flash** (see `FUSES.md`) |
| `make flash PORT=/dev/tty.…` | flash over SerialUPDI. ⚠️ This does **not** write the calibration record; prefer `../tools/pmgr_flash_with_pram.py` (see `../docs/BUILD.md` §7) |
| `make clean` | |

For wiring the programmer and flashing step by step, see [`../docs/PROGRAMMING.md`](../docs/PROGRAMMING.md).

## Which hybrid is fitted? — chosen in the calibration record

One firmware image serves both. The **calibration record** says which hybrid is fitted
(`tools/pmgr_calib_image.py --hybrid original|recreation`, see `docs/BUILD.md` §7), and the
firmware picks its battery thresholds at boot:

| Record says | Low-battery warning | PMGR sleeps the machine | Level reported to the Mac |
|---|---|---|---|
| `original` (or no valid record) | 5.90 V | 5.74 V | as measured — Apple's own values |
| `recreation` | 6.20 V | 6.05 V | remapped onto Apple's scale |

A recreation hybrid cuts the power at about 5.92 V, above Apple's warning and sleep points, so with
Apple's values the machine just dies. With `recreation` the Mac shows its own low-battery warnings,
puts itself to sleep, and resumes normally when power is restored. A board calibrated before this
field existed keeps working, as `original`. (`make HYBRID=` from an earlier build no longer exists.)

## Host-side tests

The protocol, PRAM save, ADC handling, calibration, charging logic and PWM output are compiled
natively and tested on your computer — no hardware needed. All eleven suites:

```bash
make -C test
```

## Layout

```
src/            main.c and one file per subsystem: bus, proto, handlers, adb, adc, calib,
                pram, pwm, rom, selftest
include/        headers. pmgr_pins.h is GENERATED from the MCU board by gen_pins.py
test/           native test harness and the eleven suites
gen_pins.py     regenerates include/pmgr_pins.h from hardware/mcu-board/pmgr_mcu.kicad_pcb
tools_rom2h.py  converts YOUR OWN ROM dump to a header, for the byte-exact build (below)
FUSES.md        the fuse configuration pmgr.hex assumes, and why
```

## The ROM image

The Mac can read back the M50753's program ROM. **The default build uses a synthetic image; there
are no Apple bytes in this repository.** For a byte-exact build, supply your own dump:

```bash
python3 tools_rom2h.py /path/to/pmuv1.bin > include/pmgr_rom_image.h
make EXTRA_CFLAGS=-DPMGR_FAITHFUL_ROM
```

⛔ `include/pmgr_rom_image.h` **is** the ROM image. It is gitignored; never commit or publish it.

## Debug pads

| pad | meaning when high |
|---|---|
| `DBG2` (PF3) | the ADC scale self-check passed: the ADC reads two internal levels derived from the AVR's 2.048 V reference correctly. Set shortly after power is applied, the same on a Portable and a PowerBook 100 |
| `DBG1` (PF4) | an ADC conversion **timed out** — latched, never cleared until reset |
| `DBG0` (PF5) | the RTC/PIT synchronisation timed out |

`DBG2` high with `DBG1` low is a healthy ADC.
