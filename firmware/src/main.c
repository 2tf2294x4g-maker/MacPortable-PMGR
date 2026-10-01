/* Macintosh Portable / PowerBook 100 Power Manager replacement.
 * Reimplements the M50753 PMGR on an AVR128DB64. See ../PROTOCOL-SPEC.md.
 */
#include "pmgr_pins.h"
#include "pmgr_bus.h"
#include "pmgr_proto.h"
#include "pmgr_handlers.h"
#include "pmgr_calib.h"
#include "pmgr_adb.h"
#include "pmgr_pram.h"
#include "pmgr_pwm.h"
#ifdef PMGR_SELFTEST
#include "pmgr_selftest.h"
#endif
#include "pmgr_adc.h"

#include <avr/interrupt.h>
#include <avr/cpufunc.h>
#include <util/delay.h>

/* The timebase is SIXTYHZ (PC2), not an internal timer. Apple's Int1 runs at
 * 60 Hz and 1SEC* is derived by dividing it by 60 (pmgr.asm 571-585). The
 * machine supplies that reference; generating our own would drift against it. */
/* PENDING 60 Hz ticks - a COUNTER, not a flag.
 *
 * A boolean loses ticks whenever two edges arrive before the main loop
 * services one, and the loop can exceed a 16.6 ms period: pmgr_poll() working
 * through a multi-byte reply allows PMGR_REQ_TIMEOUT_US (2 ms) per byte.
 *
 * Every lost tick is time the Mac never sees, because 1SEC* is DERIVED by
 * dividing this by 60 - the exact mirror of the fallback bug described below,
 * which gained ~96 min/day by injecting ticks. Losing them makes the clock run
 * slow instead.
 *
 * Saturates rather than wrapping: if we are ever starved for four seconds
 * something is badly wrong, and wrapping to 0 would silently discard 256 ticks.
 * Replay is cheap - a divider decrement and, once per 60, a 5 us pulse. */
#define TICK_MAX  255u
static volatile uint8_t s_tick;

/* Fallback arming counter, in PIT periods since the last real SIXTYHZ edge.
 *
 * The PIT runs at 64 Hz and SIXTYHZ at 60 Hz, so the PIT is FASTER: four times
 * a second a PIT period legitimately contains no SIXTYHZ edge. An earlier
 * revision treated any such gap as "SIXTYHZ is dead" and injected a tick,
 * producing 64 ticks/s into the /60 divider - a 1SEC* of 1.0667 Hz, gaining
 * about 96 minutes a day on the Mac's clock while SIXTYHZ was perfectly fine.
 *
 * Requiring several CONSECUTIVE silent periods fixes it: 4 PIT periods is
 * 62.5 ms against a 16.7 ms SIXTYHZ period, so ~46 ms of margin. */
#define SIXTYHZ_LOST_PERIODS  4u
#define SIXTYHZ_TIMEOUT_CAP   8u

static volatile uint8_t s_sixtyhz_timeout;

#ifdef PMGR_PARK_SLEEP
volatile bool g_parked, g_park_edge, g_park_pit;
#endif

ISR(PORTC_PORT_vect)
{
    PORTC.INTFLAGS = SIXTYHZ_bm;        /* clb Int1_Req equivalent */
#ifdef PMGR_PARK_SLEEP
    if (g_parked) { g_park_edge = true; return; }    /* the park counts it by hand */
#endif
    if (s_tick < TICK_MAX) s_tick++;
    s_sixtyhz_timeout = 0;              /* the real source is alive */
}

/* Fallback tick, for a bench board with no host supplying SIXTYHZ. Stands down
 * automatically as soon as real edges reappear. In fallback the rate is the
 * PIT's 64 Hz, so 1SEC* runs ~6.7% fast - fine for bench work, and never used
 * when a machine is attached. */
ISR(RTC_PIT_vect)
{
    RTC.PITINTFLAGS = RTC_PI_bm;
#ifdef PMGR_PARK_SLEEP
    if (g_parked) { g_park_pit = true; return; }
#endif

    if (s_sixtyhz_timeout < SIXTYHZ_TIMEOUT_CAP)
        s_sixtyhz_timeout++;

    if (s_sixtyhz_timeout >= SIXTYHZ_LOST_PERIODS) {
        if (s_tick < TICK_MAX) s_tick++;
    }
}

static void clock_init(void)
{
    /* Internal 24 MHz - no crystal. PA0/PA1 are XTALHF but carry VIAD0/VIAD1. */
    ccp_write_io((void *)&CLKCTRL.OSCHFCTRLA, CLKCTRL_FRQSEL_24M_gc);
    ccp_write_io((void *)&CLKCTRL.MCLKCTRLB, 0);
}

static void tick_init(void)
{
    SIXTYHZ_PORT.PIN2CTRL = PORT_ISC_FALLING_gc;        /* PC2 falling edge */

    /* ⛔ BOUNDED, for the same reason as the ADC poll (see pmgr_adc.c).
     * tick_init() runs BEFORE the cold-entry dispatch, so an RTC that never
     * clears STATUS would hang here and leave the Macintosh held in reset -
     * a dead board with no indication why.
     *
     * The consequence of giving up is mild and worth taking: the PIT is only
     * the FALLBACK tick for a bench board with no host. The real timebase is
     * SIXTYHZ on PC2, configured above and unaffected. So on timeout we skip
     * the PIT and carry on rather than refusing to boot the machine. */
    for (uint16_t us = 0; us < 2000u && RTC.STATUS > 0; us++)
        _delay_us(1);

    if (RTC.STATUS == 0) {
        RTC.CLKSEL     = RTC_CLKSEL_OSC32K_gc;
        RTC.PITINTCTRL = RTC_PI_bm;
        RTC.PITCTRLA   = RTC_PERIOD_CYC512_gc | RTC_PITEN_bm;   /* 64 Hz fallback */
    } else {
        /* No PIT. SIXTYHZ still drives everything when a host is attached;
         * only a host-less bench board loses its fallback tick. DBG0 is the
         * one free debug pin in the normal build - see pmgr_adc.c. */
#ifndef PMGR_LOOP_MARKER            /* marker builds own DBG0 */
        DBG0_PORT.DIRSET = DBG0_bm;
        DBG0_PORT.OUTSET = DBG0_bm;
#endif
    }
}

/* PMGR_RESET* (PB5) is wired to the M50753's own RESET pin on the original
 * board, so asserting it holds the chip in reset and releasing it lands at
 * PMGRResetEntry -> ColdEntry.
 *
 * ⭐ IT IS A LEVEL, NOT AN EDGE. While RESET is low the original executes
 * NOTHING. An earlier revision here treated it as an edge - software-reset the
 * AVR on seeing PB5 low - which was wrong in two compounding ways:
 *
 *   1. after the self-reset the AVR immediately ran again even with PB5 still
 *      low, so a long assertion let the replacement execute substantial
 *      firmware while the original would still be physically held;
 *   2. this service sits at the BOTTOM of the main loop, so each self-reset was
 *      followed by a full cold init - including the park-or-release decision -
 *      before PB5 was looked at again. A held reset therefore produced a reset
 *      LOOP that toggled SYS_RST* at the machine on every pass.
 *
 * Correct behaviour is to WAIT, not to retrigger: hold the machine down and
 * spin until the pin is released, then take one clean cold entry.
 *
 * Nothing is serviced during the wait - no tick, no timekeeping. That is
 * faithful: a chip held in reset does not count seconds either, and the
 * .noinit clock correctly resumes from where it stopped.
 *
 * Distinct from RESET (PB4), the front-panel reset SWITCH, which vectors to
 * Reset_Interrupt and exits via ResetEntry - that one always releases the
 * machine. Same word, two signals, opposite outcomes. */
static void pmgr_reset_pin_wait(void)
{
    if (!PIN_IS_LOW(PMGR_RESET_N_PORT, PMGR_RESET_N_bm))
        return;

    /* Hold the 68000 down for as long as we are held down. */
    ASSERT_LOW(SYS_RST_N_PORT, SYS_RST_N_bm);

    while (PIN_IS_LOW(PMGR_RESET_N_PORT, PMGR_RESET_N_bm))
        { /* held in reset - execute nothing, exactly as the M50753 would */ }
}

/* Asserted while running: hold as above, then take ONE cold entry on release,
 * which is what the release of a real RESET pin does. */
static void pmgr_reset_pin_service(void)
{
    if (!PIN_IS_LOW(PMGR_RESET_N_PORT, PMGR_RESET_N_bm))
        return;

    pmgr_reset_pin_wait();                          /* returns once released */
    /* ⭐ Finish any outstanding PRAM save before the restart (MEASUREMENTS
     * 2.156). The machine is held, so blocking here serves no one less. An
     * unfinished save would be safe anyway (the old slot loads) but would
     * lose the change. */
    (void)pmgr_pram_flush();          /* bounded; proceed either way (2.184) */
    _PROTECTED_WRITE(RSTCTRL.SWRR, RSTCTRL_SWRST_bm);
    for (;;) { }                                    /* not reached */
}

int main(void)
{
    clock_init();

    pmgr_bus_init();
    pmgr_proto_init();
    pmgr_handlers_init();       /* InitPorts: SYS_RST* asserted, machine held */

    /* ⭐ PMGR_RESET* IS A LEVEL. Do not proceed past reset-state
     * initialisation while it is still asserted - see pmgr_reset_pin_wait().
     *
     * Placed HERE, immediately after the ports are configured, because that is
     * the earliest point at which SYS_RST* is asserted and the machine is
     * safely held. Waiting any later would let us initialise ADB, PRAM, the
     * ADC and the PWM while the original M50753 would still be doing nothing
     * at all. Waiting any earlier would spin with the ports undefined. */
    pmgr_reset_pin_wait();

    pmgr_adb_init();
    pmgr_pram_init();

    /* ⭐ Entry2, pmgr.asm 138-166 - and it runs on THIS path too, not just the
     * reset switch: ColdEntry and ResetEntry both `bra Entry2`. Without it a
     * cold boot never tests the PRAM checksum at all, so corrupt PRAM is
     * loaded and kept.
     *
     * ORDER DIVERGES FROM THE ORIGINAL, deliberately. Apple runs Entry2 BEFORE
     * InitPorts; we configure the ports first (above) so SYS_RST* is asserted
     * and the machine is safely held before anything else happens, and our
     * PRAM must be loaded from EEPROM before it can be checksummed - the
     * original's is just RAM that is already there. The two touch disjoint
     * state (ports vs RAM), so the swap is safe; the one thing that does span
     * both, the Port_P0 value, is chosen inside pmgr_ports_init(). */
    pmgr_entry_state();

    /* ⛔⛔ LOAD THE CALIBRATION RECORD. Without this call s_valid is permanently
     * false, so EVERY board runs uncalibrated with fast charge disabled - the
     * record sits correctly in EEPROM and NOTHING EVER READS IT.
     *
     * ⭐ That is exactly what shipped in `e74c7802` and was caught on HARDWARE,
     * 2026-09-28, by reading $300 and finding UNCAL|GATED set on a board whose
     * record had just been written and verified byte-identical (MEASUREMENTS
     * 2.359). ⛔ THIRTY NATIVE ASSERTIONS PASSED OVER IT, because every one of
     * them calls pmgr_calib_load() itself to set up the state under test - a
     * test that ARRANGES a call can never discover that production omits it.
     *
     * Placed after pmgr_pram_init() (both read EEPROM) and BEFORE the first
     * pmgr_tick_1s() can run, so no charging decision is ever taken against an
     * unloaded record. */
    pmgr_calib_load();

    pmgr_adc_init();

    /* AFTER pmgr_ports_init() (inside pmgr_handlers_init above), which leaves
     * PD6 an output driven low. pmgr_pwm_init() configures TCA0 and the CCL
     * route but leaves the output DISABLED - the original does not start the
     * PWM until :201, in pmgr_system_release(). */
    pmgr_pwm_init();

    tick_init();

#ifdef PMGR_SELFTEST
    /* Bench only - drives the ADB control line. Never built into a firmware
     * that goes into a machine; see pmgr_selftest.h for the jumper. Result is
     * latched on DBG0..DBG2 and the board then halts, so the code stays
     * readable instead of being overwritten by normal operation. */
    pmgr_selftest_report(pmgr_selftest_adb_capture());
    for (;;) { }
#endif

    /* ---- ColdEntry dispatch, pmgr.asm 204-207 --------------------------
     *
     * The hardware RESET vector is PMGRResetEntry -> `bra ColdEntry`
     * (:112-113), which sets Y=8. So reaching main() is ALWAYS a cold entry,
     * and the decision reduces to VIA_TEST:
     *
     *     VIA_TEST SET   -> jmp Sleep   : park with the 68000 held in reset
     *     VIA_TEST CLEAR -> release
     *
     * ⚠️ This must NOT be an unconditional release. A real Portable parks at
     * power-on and waits for a keypress - MEASUREMENTS.md 2.5 - and releasing
     * here regardless would start the machine when the original would not.
     *
     * pmgr_sleep() does not return until a wake event has brought the machine
     * up, because `jmp Sleep` never returns either: it falls through the park
     * loop into the wake path and ends at `jmp CommandReceive` (:1410).
     *
     * Interrupts stay DISABLED through either route: Apple's cli is at :221
     * (reset path) and :1409 (wake path), both AFTER SYS_RST is already high.
     * That is why both sequences poll SIXTYHZ directly instead of using it as
     * an interrupt. */
    /* pmgr.asm 190-203 runs on BOTH branches, BEFORE the dispatch at :204-207:
     * contrast validated and applied, PWM on, batteryNow, KBD_RST* released.
     * Doing it inside pmgr_system_release() would skip it whenever we park -
     * i.e. on every normal cold boot. */
    pmgr_pre_dispatch_init();

    if (pmgr_cold_should_park())
        pmgr_sleep();               /* :207 jmp Sleep */
    else
        pmgr_system_release();      /* :208-218 */

    /* Both paths leave the interrupt state as they found it - see the contract
     * at the top of pmgr_handlers.h - so exactly one sei() serves both, here,
     * once the machine is released either way. The original's equivalent is
     * `cli` (6502 ENABLE) at :221 on the reset path and :1409 on the wake
     * path; it can afford one per path because each has a single caller. */
    sei();

#ifdef PMGR_LOOP_MARKER
    /* ⛔ DIAGNOSTIC (MEASUREMENTS 2.161): DBG0 (J2-3) high inside the 60 Hz tick
     * block, DBG2 (J2-5) high inside pmgr_poll(). Finds what runs during the
     * ~3.9 ms of unanswered requests at the start of a save. */
    DBG0_PORT.OUTCLR = DBG0_bm; DBG0_PORT.DIRSET = DBG0_bm;
    DBG2_PORT.OUTCLR = DBG2_bm; DBG2_PORT.DIRSET = DBG2_bm;
#endif
    for (;;) {
#ifdef PMGR_LOOP_MARKER
        DBG2_PORT.OUTSET = DBG2_bm;
        pmgr_poll();
        DBG2_PORT.OUTCLR = DBG2_bm;
#else
        pmgr_poll();
#endif

        /* ⭐⭐ PRAM persistence, ONE EEPROM byte per pass, never waits
         * (MEASUREMENTS 2.155-2.157). Directly after pmgr_poll() so a pass
         * that just took a $31 write starts saving at once, while the bus
         * keeps being served between bytes. */
        pmgr_pram_service();

        /* pmgr.asm 1702-1714, the $FFF4 reset-switch vector. Polled rather
         * than vectored: the handler spends its whole life in tick-paced wait
         * loops, so there is nothing an interrupt would buy. A press ALWAYS
         * releases the machine, even when a cold boot would have parked it,
         * because it exits via ResetEntry (Y=0) and never reaches the
         * VIA_TEST test. */
        pmgr_reset_switch_service();
        pmgr_reset_pin_service();       /* PMGR_RESET* -> full cold restart */

        /* pmgr.asm 463 - the battery path parks the machine itself when the
         * level falls below deadBatteryLevel. Acted on HERE, outside any tick,
         * because pmgr_sleep()'s wait loop calls the tick and re-entering it
         * from inside would recurse. */
        if (pmgr_sleep_requested())
            pmgr_sleep();

        /* Take the whole backlog at once. The read-and-clear must be atomic:
         * the ISR increments s_tick, so a plain read followed by a store would
         * drop any edge landing between them. */
        uint8_t due;
        {
            uint8_t sreg = SREG;
            cli();
            due = s_tick;
            s_tick = 0;
            SREG = sreg;
        }

        if (due != 0u) {
            /* First tick is a full one - .ADBAction included. The rest advance
             * time only, so a starved loop catches up on the clock without
             * firing a burst of autopolls. */
#ifdef PMGR_LOOP_MARKER
            DBG0_PORT.OUTSET = DBG0_bm;
#endif
#ifdef PMGR_ADB_HOST_SERVICE
            pmgr_bus_latch_arm(true);           /* ADB may latch a command byte */
#endif
            pmgr_tick_60hz();
#ifdef PMGR_ADB_HOST_SERVICE
            pmgr_bus_latch_arm(false);
            /* ⭐ FIX A (design/ADB-HOST-SERVICE.md §10, MEASUREMENTS 2.208-2.209): serve a command byte latched during
             * the tick BEFORE anything else - the boot ROM gives up on its count byte after ~1.76 ms. Here in the
             * main loop, never inside the tick: a $70 sleep command parks in pmgr_sleep(), which calls the tick. */
            if (pmgr_bus_has_latched()) pmgr_poll();
            pmgr_tick_run_deferred();
#endif
            while (--due) pmgr_tick_catchup();
#ifdef PMGR_LOOP_MARKER
            DBG0_PORT.OUTCLR = DBG0_bm;
#endif
        }
    }
}
