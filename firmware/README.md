# Firmware — AVR128DB64

Open replacement firmware for the Macintosh Portable's M50753 power manager. It serves the Mac's
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

## Host-side tests

The protocol, PRAM save, ADC handling, calibration and charging logic are compiled natively and
tested on your computer — no hardware needed:

```bash
make -C test run_save run_latch run_adb_latch run_deadline run_adc run_charging run_calib run_calib_cross
```

## Layout

```
src/            main.c and one file per subsystem: bus, proto, handlers, adb, adc, calib,
                pram, pwm, rom, selftest
include/        headers. pmgr_pins.h is GENERATED from the MCU board by gen_pins.py
test/           native test harness and the eight suites
gen_pins.py     regenerates include/pmgr_pins.h from hardware/mcu-board/pmgr_mcu.kicad_pcb
tools_rom2h.py  converts YOUR OWN ROM dump to a header, for the byte-exact build (below)
FUSES.md        the fuse configuration pmgr.hex assumes, and why
```

## The ROM image

The Mac can read back the M50753's program ROM. **The default build uses a synthetic image; there
are no Apple bytes in this repository.** For a byte-exact build, supply your own dump:

```bash
python3 tools_rom2h.py /path/to/pmuv1.bin > include/pmgr_rom_image.h
make CFLAGS="$(CFLAGS) -DPMGR_FAITHFUL_ROM"
```

⛔ `include/pmgr_rom_image.h` **is** the ROM image. It is gitignored; never commit or publish it.

## Debug pads

| pad | meaning when high |
|---|---|
| `DBG2` (PF3) | the ADC scale self-check passed. ⚠️ Low until the machine has been switched on once |
| `DBG1` (PF4) | an ADC conversion **timed out** — latched, never cleared until reset |
| `DBG0` (PF5) | the RTC/PIT synchronisation timed out |

`DBG2` high with `DBG1` low is a healthy ADC.
