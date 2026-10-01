/* Power-on self-test for the ADB capture path.
 *
 * WHY THIS EXISTS
 * ---------------
 * pmgr_adb.c configures TCB0 in Input Capture Pulse-Width mode with
 * EVCTRL.EDGE = 1, on the belief that this makes the NEGATIVE edge start the
 * counter and the POSITIVE edge capture it - i.e. that CCMP is the width of
 * the LOW pulse. That belief came from recollection of the AVR-Dx datasheet,
 * not from the datasheet itself, and the device header exposes only the bit
 * name (TCB_EDGE_bm = 0x10). It is unverified.
 *
 * If it is backwards, CCMP is the HIGH period instead, every ADB bit inverts,
 * and the failure presents as a plausible-but-wrong bitstream rather than an
 * obvious fault - the worst kind. This test settles it on real silicon in
 * about 3 ms instead of trusting the assumption.
 *
 * WIRING
 * ------
 * Requires a jumper from PMGR_ADB (PD1) to FDB (PD2). On the breakout these
 * are two pins on J3/J4, so it is a single jumper wire.
 *
 * ⛔⛔ THIS TEST DELIBERATELY VIOLATES THE PD1 RULE. READ THIS BEFORE FLASHING.
 * -------------------------------------------------------------------------
 * ADB-TIMING.md 3a: **PD1 MUST NEVER BE DRIVEN HIGH.** In a machine PD1 sits on
 * a node that clamps at ~0.74 V, so push-pull driving it to 5 V sources
 * current limited only by the AVR's UNMEASURED output impedance - far past the
 * **20 mA** at which output levels are specified, and risking the **+/-50 mA**
 * absolute maximum. ⚠️ The old "170 mA / 8x over" figure assumed ~25 ohm and is
 * RETRACTED - the impedance was never measured.
 *
 * This test drives PD1 HIGH anyway, because proving the TCB edge semantics
 * requires generating a known pulse and there is no other way to make one.
 *
 * WHY THAT IS SAFE HERE, AND ONLY HERE: on the bench breakout PD1 goes to a
 * jumper wire and PD2, a high-impedance INPUT. There is no clamp, so there is
 * nothing to source into - microamps, not milliamps. The hazard is entirely a
 * property of the machine's ADB network, which is absent on the breakout.
 *
 * ⛔ Consequently: **bench breakout ONLY, with the jumper fitted, never with
 * the board in a machine.** `make selftest` builds to
 * `pmgr-SELFTEST-BENCH-ONLY.hex` rather than `pmgr.hex` precisely so this image
 * cannot be flashed into a machine by picking the wrong file, and `make flash`
 * depends on `pmgr.hex` only.
 *
 * ⚠️ AN EARLIER VERSION OF THIS COMMENT DESCRIBED THE OBSOLETE MODEL. It said
 * "in the machine ADB_ASSERT() (PD1 HIGH) makes the bus LOW", which is
 * ADB-TIMING.md section 3b - explicitly superseded. Under the CURRENT contract
 * (3a) ADB_ASSERT() is Hi-Z (`DIRCLR`) and release is a SINK; nothing in normal
 * operation ever drives PD1 high. Describing this test against the old model
 * made a deliberate, bounded violation look like ordinary behaviour.
 *
 * The test drives PD1 directly rather than through ADB_ASSERT/ADB_RELEASE - so
 * that it measures the capture path itself, and so that this violation is
 * visible at the point of use instead of hidden behind a macro whose meaning
 * has already changed once.
 */
#ifndef PMGR_SELFTEST_H
#define PMGR_SELFTEST_H

#include <stdint.h>

typedef enum {
    /* TCB measured the LOW pulse. EDGE=1 is correct; pmgr_adb.c is right. */
    PMGR_ST_OK                = 0,

    /* TCB measured the HIGH period instead. EDGE is inverted from what
     * pmgr_adb.c assumes - clear TCB_EDGE_bm in pmgr_adb_init(). Until that
     * is changed, every received ADB bit is inverted. */
    PMGR_ST_EDGE_INVERTED     = 1,

    /* PD2 does not follow PD1: the loopback jumper is missing or open.
     * Nothing about the TCB was learned. */
    PMGR_ST_NO_LOOPBACK       = 2,

    /* Loopback works but no capture ever completed. TCB is not being
     * triggered - suspect the EVSYS routing (channel 2 -> USERTCB0CAPT) or
     * that TCB0 is not enabled. */
    PMGR_ST_NO_CAPTURE        = 3,

    /* A capture completed but matched neither the low nor the high interval.
     * Suspect the TCB clock divider (TCB_CLKSEL_DIV2_gc) or F_CPU. The
     * measured tick count is available via pmgr_selftest_ticks(). */
    PMGR_ST_TIMEBASE_WRONG    = 4,
} pmgr_selftest_t;

/* Run the test. Leaves PMGR_ADB released (LOW) and TCB0 re-armed clean. */
pmgr_selftest_t pmgr_selftest_adb_capture(void);

/* Raw CCMP from the last run, in TCB ticks. Useful when the verdict is
 * PMGR_ST_TIMEBASE_WRONG - divide by 12 for microseconds at CLK_PER/2. */
uint16_t pmgr_selftest_ticks(void);

/* Present a result code on DBG0..DBG2 (J2 pins 3-5). Three bits, so codes
 * 0-7; every value of pmgr_selftest_t fits. DBG0 is the LSB. */
void pmgr_selftest_report(pmgr_selftest_t r);

#endif /* PMGR_SELFTEST_H */
