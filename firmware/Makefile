# PMGR replacement firmware - AVR128DB64
MCU      = avr128db64
F_CPU    = 24000000UL
TARGET   = pmgr

# Toolchain. Homebrew's avr-gcc@14 is a VERSIONED formula and therefore
# keg-only: it is installed but never symlinked into /opt/homebrew/bin, so a
# bare `avr-gcc` is not on PATH and the build appears to be missing a toolchain
# that is in fact present. Find it automatically, and fall back to PATH so an
# unversioned install or a different toolchain still works.
AVR_BIN  := $(firstword $(wildcard /opt/homebrew/opt/avr-gcc@*/bin) \
                        $(wildcard /usr/local/opt/avr-gcc@*/bin))
ifneq ($(AVR_BIN),)
  PREFIX := $(AVR_BIN)/
endif

CC       = $(PREFIX)avr-gcc
OBJCOPY  = $(PREFIX)avr-objcopy
SIZE     = $(PREFIX)avr-size

# avr-binutils is also keg-only; its objcopy/size live elsewhere. Use whichever
# of the two prefixes actually provides each tool.
ifeq ($(wildcard $(AVR_BIN)/avr-objcopy),)
  BINUTILS := $(firstword $(wildcard /opt/homebrew/opt/avr-binutils/bin) \
                          $(wildcard /usr/local/opt/avr-binutils/bin))
  ifneq ($(BINUTILS),)
    OBJCOPY := $(BINUTILS)/avr-objcopy
    SIZE    := $(BINUTILS)/avr-size
  endif
endif

# EXTRA_CFLAGS / EXTRA_LDFLAGS exist for toolchains that do not ship AVR-Dx
# device support. Ubuntu's packaged avr-gcc does not know avr128db64 and fails
# with "device-specs/specs-avr128db64: No such file or directory"; Microchip's
# own avr8-gnu-toolchain 3.7.0 does not have it either (checked 2026-08-31).
# The fix is the AVR-Dx device family pack:
#
#   make EXTRA_CFLAGS="-B <pack>/gcc/dev/avr128db64/ -I <pack>/include" \
#        EXTRA_LDFLAGS="-B <pack>/gcc/dev/avr128db64/"
#
# See .github/workflows/ci.yml, which does exactly this.
# ⭐ ADB host service is ON by default since baseline 2026-09-14-adb-host-service (MEASUREMENTS 2.195-2.197):
# the command byte of a host request is handshaken during ADB work. Build the previous behaviour, e.g. for an
# A/B control arm, with `make ADB_HOST_SERVICE=0`.
ADB_HOST_SERVICE ?= 1
ifeq ($(ADB_HOST_SERVICE),1)
ADB_HS_FLAG = -DPMGR_ADB_HOST_SERVICE
endif

# ⭐ Low-power park is ON by default since baseline 2026-09-14-park-sleep (MEASUREMENTS 2.199-2.200): POWER-DOWN between 60 Hz
# ticks while parked, ADC reference kept on. Needs FUSE_BODCFG 0x66 (BOD sampled in sleep). `make PARK_SLEEP=0` builds the
# previous behaviour.
PARK_SLEEP ?= 1
ifeq ($(PARK_SLEEP),1)
PARK_FLAG = -DPMGR_PARK_SLEEP -DPMGR_PARK_VREF_ALWAYSON
endif

CFLAGS   = -mmcu=$(MCU) -DF_CPU=$(F_CPU) -Os -std=gnu11 \
           -Wall -Wextra -Werror -Wundef -Wshadow \
           -ffunction-sections -fdata-sections -Iinclude $(ADB_HS_FLAG) $(PARK_FLAG) $(EXTRA_CFLAGS)
LDFLAGS  = -mmcu=$(MCU) -Wl,--gc-sections $(EXTRA_LDFLAGS)

SRC      = $(wildcard src/*.c)
OBJ      = $(SRC:.c=.o)

# ⛔⛔ CONFIGURATION STAMP (review, MEASUREMENTS 2.201). The objects used to depend only on their sources, so changing a
# build switch - `make PARK_SLEEP=0 ADB_HOST_SERVICE=0` after a default build - printed "Nothing to be done" and left the
# previous image in place. Two A/B arms could silently be the same firmware. The effective CC/CFLAGS/LDFLAGS are now recorded
# in .build-config; when they differ from the last build, the objects and images are deleted so everything is rebuilt.
# ⭐ Still verify the md5 of anything flashed for an experiment - this removes one way to get it wrong, not the need to check.
.DEFAULT_GOAL := all
CONFIG_STAMP = .build-config
# ⚠️ Done at PARSE time, not in a recipe. Two recipe-based versions failed while testing this fix: (1) a stamp prerequisite
# alone - macOS's make 3.81 compares mtimes to the SECOND, so objects recompiled in the same second as the old .elf were
# "not newer" and the image was not relinked; (2) deleting the objects inside the stamp recipe - make had already stat'ed
# them, and the link then failed with the hex missing. Comparing here, before make builds its dependency graph, and
# deleting the objects and images on a change, forces a full rebuild regardless of clock resolution.
CONFIG_LINE := $(strip $(CC) $(CFLAGS) | $(LDFLAGS))
CONFIG_OLD  := $(strip $(shell cat $(CONFIG_STAMP) 2>/dev/null))
ifneq ($(CONFIG_LINE),$(CONFIG_OLD))
CONFIG_RESET := $(shell rm -f $(SRC:.c=.o) $(TARGET).elf $(TARGET).hex; printf '%s\n' '$(CONFIG_LINE)' > $(CONFIG_STAMP); echo changed)
$(info   build configuration changed - full rebuild)
endif

all: $(TARGET).hex

# ⛔ BENCH SELF-TEST BUILD - DELIBERATELY DRIVES PD1 HIGH.
#
# This image VIOLATES the PD1 rule in ADB-TIMING.md 3a: in a machine, PD1 sits
# on a node that clamps at ~0.74 V, and driving it push-pull high sources
# current bounded only by the AVR's output impedance. That impedance was never
# measured, so the "170 mA, 8x the 20 mA pin limit" figures this comment used to
# carry are RETRACTED - and 20 mA is the point at which output levels are
# SPECIFIED, not a limit; the absolute maximum is +/-50 mA. It is safe ONLY on
# the bench breakout, where PD1 is
# jumpered to PD2, a high-impedance input with no clamp to source into.
#
# It therefore builds to its OWN FILENAME. Until 2026-08-28 it overwrote
# pmgr.hex, so a self-test image was indistinguishable from a shippable one by
# name - the single most likely way this reaches a machine is someone flashing
# the wrong file.
selftest:
	$(MAKE) clean
	$(MAKE) CFLAGS="$(CFLAGS) -DPMGR_SELFTEST" TARGET=$(TARGET)-SELFTEST-BENCH-ONLY all
	@echo
	@echo "  ⛔ $(TARGET)-SELFTEST-BENCH-ONLY.hex - NEVER flash this into a machine."
	@echo "     It drives PD1 high. In a machine that sources current bounded"
	@echo "     only by the AVR output impedance into a 0.74 V clamp, far"
	@echo "     past the 20 mA spec point. Bench breakout only."
	@echo
	@echo "  Jumper PD1<->PD2, flash, read DBG0..DBG2:"
	@echo "    000 OK            - EDGE=1 measures LOW, pmgr_adb.c is correct"
	@echo "    001 EDGE_INVERTED - clear TCB_EDGE_bm in pmgr_adb_init()"
	@echo "    010 NO_LOOPBACK   - jumper missing"
	@echo "    011 NO_CAPTURE    - EVSYS/TCB not triggering"
	@echo "    100 TIMEBASE      - clock divider or F_CPU wrong"

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^
	$(SIZE) $@

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex -R .eeprom $< $@

# Program over UPDI. Adjust the port for your programmer.
# Deliberately depends on $(TARGET).hex ONLY - there is no `flash` path that
# can pick up the self-test image by accident.
# ⛔ THE PORT NAME DEPENDS ON THE ADAPTER'S USB CHIP. An earlier version
# hard-coded /dev/tty.usbserial-*, which does NOT match a CH340/CH340E - macOS
# enumerates those as /dev/tty.wchusbserial*. The flash then failed in a way
# that looks like a dead programmer rather than a wrong path.
#   FTDI            -> /dev/tty.usbserial-XXXX
#   CH340 / CH340E  -> /dev/tty.wchusbserial-XXXX     <- the ordered adapter
#   CP210x          -> /dev/tty.SLAB_USBtoUART
#   native CDC      -> /dev/tty.usbmodemXXXX
# Override with:  make flash PORT=/dev/tty.something
PORT ?= $(firstword $(wildcard /dev/tty.wchusbserial* /dev/tty.usbserial-* \
                               /dev/tty.SLAB_USBtoUART* /dev/tty.usbmodem*))

flash: $(TARGET).hex check-pymcuprog
	@test -n "$(PORT)" || { \
	  echo "  ⛔ no USB-serial adapter found."; \
	  echo "     Looked for tty.wchusbserial* (CH340), tty.usbserial-* (FTDI),"; \
	  echo "     tty.SLAB_USBtoUART* (CP210x), tty.usbmodem* (CDC)."; \
	  echo "     Plug it in, or pass PORT=/dev/tty.yourdevice"; \
	  echo "     Present now:"; ls /dev/tty.* 2>/dev/null | sed 's/^/       /'; \
	  exit 1; }
	@echo "  flashing via $(PORT)"
	pymcuprog write -t uart -u $(PORT) -d $(MCU) -f $< --erase --verify

# ── fuses ────────────────────────────────────────────────────────────────────
# ⛔⛔ ADDED 2026-09-07 after review found there was NO reproducible fuse step:
# `flash` wrote pmgr.hex and nothing else, so every board would have run on
# factory defaults. See FUSES.md for the derivation of every byte.
#
# ⛔⛔ SYSCFG0 WAS 0x08 AND WOULD HAVE BRICKED THE BOARD. Caught by review
# 2026-09-07, before any hardware existed to suffer it.
#
# CRCSRC is bits [7:6] and CRCSRC_NOCRC_gc is (0x03<<6) = 0xC0 - so LEAVING
# THOSE BITS ZERO SELECTS CRCSRC_FLASH_gc, a CRC check over the entire flash on
# every reset. pmgr.hex carries no such checksum, so the scan fails and the part
# is held before it ever reaches main(). The board would have looked DEAD, and
# the first suspect would have been a DF12 joint.
#
# ⭐⭐ ROOT CAUSE, worth more than the fix: each byte was composed from the bits
# that were being SET, leaving every other field at zero - on the assumption that
# zero is the neutral value. For CRCSRC it is the ENABLED value. The factory
# default here is 0xC8, so a hand-written "deliberate" value was strictly WORSE
# than leaving the fuse alone.
#
# ⛔ The rule that follows: START FROM THE FACTORY DEFAULT and change only the
# fields you intend. check_docs.py now recomputes every byte from named fields,
# so the arithmetic in this comment is executed rather than trusted.
# ⭐ Audited the other four when this was found: WDTCFG, BODCFG, SYSCFG1,
# CODESIZE/BOOTSIZE are all correct - zero really is the neutral value in each.
#
# ⭐ The one that would actually bite in the FIELD is MVSYSCFG. The board is single-supply
# (VDDIO2 tied to +5V, MCU-BOARD-SCHEMATIC.md:187) but the factory fuse says
# DUAL - and PORTC is not spare here: PC2 is SIXTYHZ, the 60 Hz heartbeat behind
# the 1 Hz tick and the whole battery path. The firmware falls back to a nominal
# period if SIXTYHZ is absent, so the symptom is not a dead machine - it is a
# silently degraded timebase. No solder inspection finds a fuse.
#
#   WDTCFG  0x00  watchdog OFF - a WDT reset turns the MACINTOSH off
#   BODCFG  0x66  LVL 2.85V | ACTIVE continuous | SLEEP SAMPLED (the park sleeps since 2026-09-14, MEASUREMENTS 2.199)
#   SYSCFG0 0xC8  CRCSRC=NOCRC | RSTPINCFG=RST, EESAVE clear (reflash IS the
#                 PRAM zap). ⛔⛔ THE 0xC0 IS NOT DECORATION - see below.
#   SYSCFG1 0x14  MVSYSCFG=SINGLE | SUT=8ms (rail settles before we drive it)
#   CODESIZE/BOOTSIZE 0x00  single application section
FUSE_WDTCFG   ?= 0x00
FUSE_BODCFG   ?= 0x66
FUSE_SYSCFG0  ?= 0xC8
FUSE_SYSCFG1  ?= 0x14
FUSE_CODESIZE ?= 0x00
FUSE_BOOTSIZE ?= 0x00

# offset : value : name   - OSCCFG (0x02) is deliberately absent; the design
# configures the oscillator in software and depends on no fuse value there.
FUSE_LIST = 0:$(FUSE_WDTCFG):WDTCFG 1:$(FUSE_BODCFG):BODCFG \
            5:$(FUSE_SYSCFG0):SYSCFG0 6:$(FUSE_SYSCFG1):SYSCFG1 \
            7:$(FUSE_CODESIZE):CODESIZE 8:$(FUSE_BOOTSIZE):BOOTSIZE

# ⚠️ pip installs the pymcuprog script into a per-user bin that is NOT on PATH
# by default (on this Mac: ~/Library/Python/3.11/bin, and pip SAYS SO in a
# warning most people scroll past). Without this check both targets fail with a
# bare "command not found" that reads like a broken Makefile rather than a
# missing PATH entry. Found 2026-09-07 by actually installing it.
check-pymcuprog:
	@command -v pymcuprog >/dev/null 2>&1 || { \
	  echo "  ⛔ pymcuprog not on PATH."; \
	  echo "     Install:  pip3 install --user pymcuprog"; \
	  echo "     Then add its script dir to PATH - pip prints the location;"; \
	  echo "     on this Mac it was ~/Library/Python/3.11/bin"; \
	  echo "     Check with:  pymcuprog --version"; \
	  exit 1; }

fuses: check-pymcuprog
	@test -n "$(PORT)" || { echo "  ⛔ no USB-serial adapter found - see 'make flash'"; exit 1; }
	@echo "  fuses via $(PORT)"
	@for f in $(FUSE_LIST); do \
	  off=$${f%%:*}; rest=$${f#*:}; val=$${rest%%:*}; name=$${rest##*:}; \
	  echo "    write  $$name (offset $$off) = $$val"; \
	  pymcuprog write -t uart -u $(PORT) -d $(MCU) -m fuses -o $$off -l $$val \
	    >/dev/null || exit 1; \
	done
	@echo "  ⭐ verifying - a fuse that did not take is otherwise SILENT"
# ⛔⛔ USE pymcuprog's OWN verify, do NOT parse `read` output.
# The first version of this target scraped `pymcuprog read` with
# grep -oE '0x[0-9A-Fa-f]{2}'. That is broken: showdata() prints
#     0x001280: xx 64 xx xx ...
# where the ADDRESS carries the 0x prefix and the DATA BYTE does not - so the
# regex matched 0x00 out of the address and every fuse would have reported a
# mismatch. Caught 2026-09-07 by rendering showdata() directly, because there is
# no hardware here to run it against.
# ⭐ `verify -l` reads the device and compares in-tool: utils.compare() raises
# ValueError on mismatch, pymcuprog returns a non-zero exit code, and no output
# parsing exists to be wrong.
	@rc=0; for f in $(FUSE_LIST); do \
	  off=$${f%%:*}; rest=$${f#*:}; val=$${rest%%:*}; name=$${rest##*:}; \
	  if pymcuprog verify -t uart -u $(PORT) -d $(MCU) -m fuses -o $$off -l $$val \
	       >/dev/null 2>&1; then echo "    ✅ $$name = $$val"; \
	  else echo "    ⛔ $$name did NOT take (wanted $$val)"; rc=1; fi; \
	done; \
	test $$rc -eq 0 || { echo "  ⛔⛔ FUSE VERIFY FAILED - do not flash this board"; exit 1; }
	@echo "  ⭐ all fuses as read from the device, for the bring-up record:"
	@pymcuprog read -t uart -u $(PORT) -d $(MCU) -m fuses -o 0 -b 9 2>/dev/null \
	  | sed 's/^/      /'
	@echo "  ✅ all fuses verified - now run 'make flash'"

clean:
	rm -f $(OBJ) $(TARGET).elf $(TARGET).hex $(CONFIG_STAMP)
	rm -f $(TARGET)-SELFTEST-BENCH-ONLY.elf $(TARGET)-SELFTEST-BENCH-ONLY.hex

check:
	@sh test/hostcheck.sh

.PHONY: all flash fuses check-pymcuprog clean check
