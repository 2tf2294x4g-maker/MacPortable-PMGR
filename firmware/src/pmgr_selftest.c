#include "pmgr_selftest.h"
#include "pmgr_pins.h"
#include <avr/io.h>
#include <util/delay.h>
#include <stdbool.h>

/* Deliberately asymmetric by 4x so the two candidate answers cannot be
 * confused, and so a wrong clock divider lands outside BOTH windows rather
 * than accidentally inside the other one. */
#define ST_LOW_US     250u
#define ST_HIGH_US   1000u

#define TICKS_PER_US  (F_CPU / 2000000UL)          /* CLK_PER/2 = 12 MHz */
#define ST_LOW_TICKS  ((uint16_t)(ST_LOW_US  * TICKS_PER_US))
#define ST_HIGH_TICKS ((uint16_t)(ST_HIGH_US * TICKS_PER_US))
#define ST_TOL_TICKS  ((uint16_t)(60u * TICKS_PER_US))   /* +/-60 us */

static uint16_t s_ticks;

uint16_t pmgr_selftest_ticks(void) { return s_ticks; }

static bool st_near(uint16_t v, uint16_t target)
{
    return (v > target - ST_TOL_TICKS) && (v < target + ST_TOL_TICKS);
}

/* ⛔ VIOLATES ADB-TIMING.md 3a: PD1 must never be driven high in a machine
 * (unbounded current into a 0.74 V clamp; the AVR's output impedance is
 * unmeasured, so the old "170 mA / 8x" figure is RETRACTED - but it far
 * exceeds the 20 mA spec point and risks the +/-50 mA maximum). Safe ONLY on the
 * bench breakout, where PD1 meets a jumper and PD2's high-impedance input and
 * there is no clamp to source into. See pmgr_selftest.h.
 *
 * Drive PD1 directly. NOT via ADB_ASSERT/ADB_RELEASE - those carry the
 * inverting-driver semantics, which the bare jumper does not have. Here HIGH
 * means the pin is high and nothing more. */
#define ST_PD1_HIGH()  (PMGR_ADB_PORT.OUTSET = PMGR_ADB_bm)
#define ST_PD1_LOW()   (PMGR_ADB_PORT.OUTCLR = PMGR_ADB_bm)
#define ST_PD2()       ((FDB_PORT.IN & FDB_bm) != 0u)

pmgr_selftest_t pmgr_selftest_adb_capture(void)
{
    s_ticks = 0;

    /* --- 1. Is the jumper actually there? ---------------------------------
     * Without this check a missing jumper would show up as NO_CAPTURE and be
     * misread as an EVSYS fault. */
    ST_PD1_HIGH(); _delay_us(20); if (!ST_PD2()) return PMGR_ST_NO_LOOPBACK;
    ST_PD1_LOW();  _delay_us(20); if ( ST_PD2()) return PMGR_ST_NO_LOOPBACK;

    /* --- 2. Generate one known asymmetric pulse ---------------------------
     * Sequence: settle HIGH, go LOW for 250 us, return HIGH, stay 1000 us,
     * then LOW again to close out a high-period measurement.
     *
     *   If EDGE=1 starts on the falling edge and captures on the rising one,
     *   CCMP is the 250 us LOW.
     *   If it is the other way round, the counter starts at the rising edge
     *   and captures at the final falling edge: CCMP is the 1000 us HIGH.
     *
     * One capture therefore distinguishes the two cases outright. */
    ST_PD1_HIGH();
    _delay_us(200);                       /* settle; discard startup edges */
    TCB0.CNT = 0;
    TCB0.INTFLAGS = TCB_CAPT_bm;

    ST_PD1_LOW();
    _delay_us(ST_LOW_US);
    ST_PD1_HIGH();
    _delay_us(ST_HIGH_US);
    ST_PD1_LOW();
    _delay_us(50);                        /* let the closing edge land */

    if (!(TCB0.INTFLAGS & TCB_CAPT_bm)) {
        ST_PD1_LOW();
        return PMGR_ST_NO_CAPTURE;
    }

    s_ticks = TCB0.CCMP;                  /* reading CCMP clears CAPT */

    /* --- 3. Leave the pin released and the TCB clean ---------------------- */
    ST_PD1_LOW();
    TCB0.INTFLAGS = TCB_CAPT_bm;

    if (st_near(s_ticks, ST_LOW_TICKS))  return PMGR_ST_OK;
    if (st_near(s_ticks, ST_HIGH_TICKS)) return PMGR_ST_EDGE_INVERTED;
    return PMGR_ST_TIMEBASE_WRONG;
}

void pmgr_selftest_report(pmgr_selftest_t r)
{
    DBG0_PORT.DIRSET = DBG0_bm;
    DBG1_PORT.DIRSET = DBG1_bm;
    DBG2_PORT.DIRSET = DBG2_bm;

    if ((uint8_t)r & 1u) DBG0_PORT.OUTSET = DBG0_bm; else DBG0_PORT.OUTCLR = DBG0_bm;
    if ((uint8_t)r & 2u) DBG1_PORT.OUTSET = DBG1_bm; else DBG1_PORT.OUTCLR = DBG1_bm;
    if ((uint8_t)r & 4u) DBG2_PORT.OUTSET = DBG2_bm; else DBG2_PORT.OUTCLR = DBG2_bm;
}
