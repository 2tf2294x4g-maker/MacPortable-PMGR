# Native unit test for the PRAM/time logic - runs on the Mac, not the AVR.
# Compiles pmgr_pram.c with the EEPROM calls stubbed out, so the checksum
# rules and the addressing can be checked without hardware.
#
#   make -C test
#
# The key case is the captured reference from a real boot:
#   3A 02 46 01  ->  3A 01 00     (xPRAM read, location $46)
# recorded in MEASUREMENTS.md 2.9.

all: run run_save run_latch run_adb_latch run_deadline run_adc run_charging run_calib run_calib_cross

# ⭐ pmgr_pram.c now compiles UNMODIFIED natively: off-AVR it takes its EEPROM
# primitives from include/pmgr_ee_hal.h, implemented by eesim.c (a simulated
# EEPROM with a busy period and injectable resets/torn bytes). The old sed
# rewrite of the EEMEM declarations is gone with them (MEASUREMENTS 2.156).
t: test.c ../src/pmgr_pram.c eesim.c eesim.h ../include/pmgr_pram.h ../include/pmgr_ee_hal.h Makefile
	cc -std=c11 -Wall -Wextra -Werror -I../include -I. -o t test.c ../src/pmgr_pram.c eesim.c

# Gate G1 - interrupted saves, concurrent changes, non-blocking contract.
ts: test_save.c ../src/pmgr_pram.c eesim.c eesim.h ../include/pmgr_pram.h ../include/pmgr_ee_hal.h Makefile
	cc -std=c11 -O2 -Wall -Wextra -Werror -I../include -I. -o ts test_save.c ../src/pmgr_pram.c eesim.c

run_save: ts
	./ts

# G1 of MEASUREMENTS 2.180 - the ADB host-service command-byte latch (pmgr_bus.c + pmgr_proto.c, host modelled in _delay_us)
tl: test_latch.c ../src/pmgr_bus.c ../src/pmgr_proto.c ../include/pmgr_bus.h ../include/pmgr_proto.h mockinc/avr/io.h mockinc/util/delay.h Makefile
	cc -std=gnu11 -Wall -Wextra -Werror -DPMGR_ADB_HOST_SERVICE -DF_CPU=24000000UL -Imockinc -I../include -o tl test_latch.c ../src/pmgr_bus.c ../src/pmgr_proto.c

run_latch: tl
	./tl

# Review 2.183: a FAILED host latch during an ADB reply must not let the reply succeed (device, host and TCB0 modelled)
tal: test_adb_latch.c ../src/pmgr_adb.c ../src/pmgr_bus.c ../include/pmgr_adb.h ../include/pmgr_bus.h mockinc/avr/io.h mockinc/avr/interrupt.h mockinc/util/delay.h Makefile
	cc -std=gnu11 -Wall -Wextra -Werror -DMOCK_TCB0_ACCESSOR -DPMGR_ADB_HOST_SERVICE -DF_CPU=24000000UL -Imockinc -I../include -o tal test_adb_latch.c ../src/pmgr_adb.c ../src/pmgr_bus.c

run_adb_latch: tal
	./tal

# 2.214: a host WITH the boot ROM's deadlines swept across every ADB phase (review, 2026-09-15)
td: test_deadline.c ../src/pmgr_adb.c ../src/pmgr_bus.c ../include/pmgr_adb.h ../include/pmgr_bus.h mockinc/avr/io.h mockinc/avr/interrupt.h mockinc/util/delay.h Makefile
	cc -std=gnu11 -Wall -Wextra -Werror -DMOCK_TCB0_ACCESSOR -DPMGR_ADB_HOST_SERVICE -DF_CPU=24000000UL -Imockinc -I../include -o td test_deadline.c ../src/pmgr_adb.c ../src/pmgr_bus.c

run_deadline: td
	./td

run: t
	./t

# ⭐ ADC scale-verdict lifecycle - the state machine behind the 2.56 P0.
# Nothing executed it before; the suite compiled the module and stopped there.
# ⛔ Makefile is a PREREQUISITE. Changing the sed recipe without it leaves a
# stale adc_native.c that make considers up to date - the same failure shape
# as a stale pmgr.hex reported as "image identical" (check_docs.py).
adc_native.c: ../src/pmgr_adc.c Makefile
	sed -e 's|#include <util/delay.h>|#include "adcstub.h"|' \
	    -e 's|#include "pmgr_pins.h"|#include "pins_stub.h"|' \
	    -e 's|ADC0.INTFLAGS = ADC_RESRDY_bm;|adcstub_clear_resrdy();|' \
	    $< > $@

ta: test_adc.c adc_native.c ../include/pmgr_adc.h adcstub.h pins_stub.h
	cc -std=c11 -Wall -Wextra -I../include -I. -o ta test_adc.c adc_native.c

run_adc: ta
	./ta

# ⭐ P1 — the charging/ADC interlock, run THROUGH pmgr_tick_1s()
# (design/CHARGING-ADC-INTERLOCK.md §4). pmgr_handlers.c compiles natively
# UNCHANGED except for two register semantics a plain-struct mock cannot
# express - write-1-to-SET/CLEAR on OUT/DIR - which is the same reason
# adc_native.c exists.
HDEFS = -DPMGR_ADB_HOST_SERVICE -DPMGR_PARK_SLEEP -DPMGR_PARK_VREF_ALWAYSON

pins_native.h: ../include/pmgr_pins.h Makefile
	sed -e 's|#define ASSERT_LOW(p,m)    ((p).OUTCLR = (m))|#define ASSERT_LOW(p,m)    ((p).OUT \&= (uint8_t)~(m))|' \
	    -e 's|#define DEASSERT_HIGH(p,m) ((p).OUTSET = (m))|#define DEASSERT_HIGH(p,m) ((p).OUT \|= (uint8_t)(m))|' \
	    $< > $@

handlers_native.c: ../src/pmgr_handlers.c gen_native.py Makefile
	python3 gen_native.py $< $@

tc: test_charging.c handlers_stub.c handlers_native.c ../src/pmgr_calib.c eesim.c pins_native.h handlers_stub.h Makefile
	cc -std=c11 -Wall -Wextra $(HDEFS) -I../include -I. -Imockinc -o tc test_charging.c handlers_stub.c handlers_native.c ../src/pmgr_calib.c eesim.c

run_charging: tc
	./tc

clean:
	rm -f t ts tl tal td ta tc adc_native.c handlers_native.c pins_native.h

# N1 - the calibration record, its CRC and THE GATE (MEASUREMENTS 2.349).
# ⛔ Links handlers_native.c deliberately: N1.9's claim is about the CHARGING
# PATH, not the record, so it must run THROUGH pmgr_tick_1s(). A version that
# only checked pmgr_calib_valid() passed while HICHG was still being asserted.
tcal: test_calib.c ../src/pmgr_calib.c ../include/pmgr_calib.h eesim.c handlers_stub.c handlers_native.c pins_native.h Makefile
	cc -std=gnu11 -O2 -Wall -Wextra -Werror $(HDEFS) -DF_CPU=24000000UL -I../include -I. -Imockinc -o tcal test_calib.c ../src/pmgr_calib.c eesim.c handlers_stub.c handlers_native.c

run_calib: tcal
	./tcal

# ⛔ The installer encodes the record in Python, the firmware decodes it in C.
# A disagreement would NOT be caught by the CRC - Python would certify its own
# wrong bytes and the board would run on a slope nobody chose.
cross_calib: cross_calib.c ../src/pmgr_calib.c ../include/pmgr_calib.h eesim.c Makefile
	cc -std=c11 -O2 -Wall -Wextra -Werror -I../include -I. -o cross_calib cross_calib.c ../src/pmgr_calib.c eesim.c

run_calib_cross: cross_calib
	./cross_calib.sh
