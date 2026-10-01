/* MEASUREMENTS 2.183 (review): a host latch that FAILS during ADB reply collection must not let the reply be
 * reported as successful. pmgr_adb.c + pmgr_bus.c natively with PMGR_ADB_HOST_SERVICE; an ADB device and the host
 * are modelled inside _delay_us(), 1 us per step. TCB0 is modelled as the hardware is: a capture sets CAPT and
 * CCMP, a later capture overwrites CCMP, and the flag stays set until the firmware has had a chance to read it. */
#include "pmgr_adb.h"
#include "pmgr_bus.h"
#include "pmgr_pins.h"
#include "pmgr_tick_defer.h"
#include <stdio.h>
#include <string.h>

PORT_t PORTA, PORTB, PORTC, PORTD, PORTE, PORTF, PORTG;
EVSYS_t EVSYS; volatile uint8_t SREG;
static TCB_t tcb;                 /* the modelled TCB0 */
static int tcb_stage;             /* 0 idle, 1 CAPT seen set by an access, 2 next access was the CCMP read */
TCB_t *tcb0_ptr(void)
{
    if (tcb_stage == 2) { tcb.INTFLAGS &= (uint8_t)~TCB_CAPT_bm; tcb_stage = 0; }
    if (tcb_stage == 1) tcb_stage = 2;
    else if (tcb.INTFLAGS & TCB_CAPT_bm) tcb_stage = 1;
    return &tcb;
}
static int fails;
#define CHECK(c, m) do { if (c) printf("  PASS  %s\n", m); else { printf("  FAIL  %s\n", m); fails++; } } while (0)

static long now_us, t_rx0 = -1;
static uint8_t reply[2]; static long req_assert_at = -1;
static int release_after_us = -1; static long acked_at = -1;   /* -1 = host never releases */
static int pmack_low;

static uint16_t ticks(int us) { return (uint16_t)(us * 12); }
static void emit(uint16_t w) { tcb.CCMP = w; tcb.INTFLAGS |= TCB_CAPT_bm; tcb_stage = 0; }

static long adb_assert_at = -1, adb_release_at = -1;   /* PD1 DIRCLR = ADB asserted (bus low), DIRSET = released */
static void step(void)
{
    now_us++;
    if (PORTD.DIRCLR & PMGR_ADB_bm) { if (adb_assert_at < 0) adb_assert_at = now_us - 1; PORTD.DIRCLR &= (uint8_t)~PMGR_ADB_bm; }
    if (PORTD.DIRSET & PMGR_ADB_bm) { adb_release_at = now_us; PORTD.DIRSET &= (uint8_t)~PMGR_ADB_bm; }
    if (PORTB.OUTCLR & PMACK_N_bm) { pmack_low = 1; acked_at = now_us; PORTB.OUTCLR &= (uint8_t)~PMACK_N_bm; }
    if (pmack_low && release_after_us >= 0 && now_us - acked_at == release_after_us) PORTB.IN |= PMREQ_N_bm;
    if (PORTB.OUTSET & PMACK_N_bm) { pmack_low = 0; PORTB.OUTSET &= (uint8_t)~PMACK_N_bm; }
    if (t_rx0 >= 0) {
        long k = now_us - t_rx0;
        if (k == 0) tcb.CCMP = ticks(35);                      /* start bit (read through the firmware's CAPT write) */
        for (int b = 0; b < 16; b++)
            if (k == 100L * (b + 1)) emit(ticks(((reply[b / 8] >> (7 - b % 8)) & 1) ? 35 : 65));
        if (k == 1700) emit(ticks(70));                         /* stop bit */
    }
    if (req_assert_at >= 0 && now_us == req_assert_at) PORTB.IN &= (uint8_t)~PMREQ_N_bm;   /* host asserts, never releases */
}
void _delay_us(double us) { for (long i = 0; i < (long)us; i++) step(); }
void _delay_ms(double ms) { _delay_us(ms * 1000.0); }

static bool talk(uint8_t *rx, uint8_t *len, uint8_t *st, long assert_offset)
{
    tcb.INTFLAGS = 0; tcb_stage = 0; PORTB.IN |= PMREQ_N_bm; PORTB.OUTCLR = PORTB.OUTSET = 0; pmack_low = 0;
    FDB_PORT.IN |= FDB_bm;                                      /* ADB line idle high throughout */
    now_us = 0;
    t_rx0 = 1735;                                               /* attention 800 + sync 65 + 8 bits 800 + stop 70 */
    req_assert_at = assert_offset < 0 ? -1 : t_rx0 + assert_offset;
    pmgr_bus_latch_arm(true);
    bool ok = pmgr_adb_transact(0x2C, NULL, 0, rx, len, st);    /* Talk, address 2, register 0 */
    pmgr_bus_latch_arm(false);
    t_rx0 = -1;
    uint8_t junk; (void)pmgr_bus_take_latched(&junk);
    return ok;
}

int main(void)
{
    uint8_t rx[8], len = 0, st = 0;
    reply[0] = 0xA5; reply[1] = 0x81;                            /* byte 1 bit 0 = 1: a stop bit read in its place decodes as 0 */

    bool ok = talk(rx, &len, &st, -1);
    CHECK(ok && len == 2 && rx[0] == 0xA5 && rx[1] == 0x81, "model check: undisturbed 2-byte reply decodes exactly");

    /* host asserts PMREQ* 50 us after the capture of byte 1's 7th bit, and never releases:
       the latch waits the full receive timeout while bit 8 and the stop bit are captured over each other */
    memset(rx, 0, sizeof rx); len = 0; st = 0;
    ok = talk(rx, &len, &st, 1550);
    printf("        (transact returned %s, len %u, rx %02X %02X, latch failed count %u)\n", ok ? "true" : "false", len, rx[0], rx[1], pmgr_latch_failed);
    CHECK(pmgr_latch_failed >= 1, "the host never released: the latch reported LATCH_FAILED");
    CHECK(!ok, "a reply collected across a FAILED latch is not reported as successful");

    /* the same moment, but the host releases after 100 us (> PMGR_LATCH_FAST_US): LATCH_SLOW */
    uint16_t slow0 = pmgr_latch_slow;
    release_after_us = 100; memset(rx, 0, sizeof rx); len = 0; st = 0;
    ok = talk(rx, &len, &st, 1550);
    release_after_us = -1;
    CHECK(pmgr_latch_slow == slow0 + 1 && !ok, "slow release during the reply (LATCH_SLOW): reply not reported as successful");

    /* FIX A (design §10, MEASUREMENTS 2.209): even a quick release (10 us, LATCH_OK) now ENDS reply collection at
     * once and discards the reply - the host is waiting for its count byte, and the boot ROM gives up after ~1.76 ms.
     * (Before fix A this case asserted the reply still decoded exactly, and the count byte waited for the frame.) */
    release_after_us = 10; memset(rx, 0, sizeof rx); len = 0; st = 0;
    acked_at = -1;
    long t_back = 0;
    {
        tcb.INTFLAGS = 0; tcb_stage = 0; PORTB.IN |= PMREQ_N_bm; PORTB.OUTCLR = PORTB.OUTSET = 0; pmack_low = 0;
        FDB_PORT.IN |= FDB_bm; now_us = 0; t_rx0 = 1735; req_assert_at = t_rx0 + 550;       /* mid byte 0 of the reply */
        pmgr_bus_latch_arm(true);
        ok = pmgr_adb_transact(0x2C, NULL, 0, rx, &len, &st);
        t_back = now_us;
        pmgr_bus_latch_arm(false); t_rx0 = -1;
    }
    release_after_us = -1;
    printf("        (latched at %ld us, transact returned at %ld us: +%ld us)\n", acked_at, t_back, t_back - acked_at);
    CHECK(!ok, "fix A: a latch during the reply (LATCH_OK) discards the reply");
    CHECK(pmgr_bus_has_latched(), "fix A: the latched command byte is held for pmgr_poll()");
    CHECK(acked_at > 0 && t_back - acked_at <= 60, "fix A: transact returns within 60 us of the latch (was: after the whole frame)");
    { uint8_t junk; (void)pmgr_bus_take_latched(&junk); }

    /* host already sending when the tick starts: no ADB work at all, the queued command stays pending */
    {
        const uint8_t none = 0;
        pmgr_adb_queue(0x2C, 0, &none, 0);
        PORTB.IN |= PMREQ_N_bm; PORTB.OUTCLR = PORTB.OUTSET = 0; pmack_low = 0; now_us = 0; t_rx0 = -1;
        req_assert_at = -1; PORTB.IN &= (uint8_t)~PMREQ_N_bm; release_after_us = 10; adb_assert_at = -1;
        PORTD.DIRCLR = 0;
        pmgr_bus_latch_arm(true);
        bool sv = pmgr_adb_service();
        if (PORTD.DIRCLR & PMGR_ADB_bm) adb_assert_at = now_us;       /* a write with no delay after it */
        pmgr_bus_latch_arm(false); release_after_us = -1; PORTB.IN |= PMREQ_N_bm;
        CHECK(!sv && pmgr_bus_has_latched(), "fix A: host sending at tick start -> byte latched, service returns false");
        CHECK(adb_assert_at < 0, "fix A: ... and the ADB bus was never driven");
        CHECK(pmgr_adb_pending(), "fix A: ... and the queued ADB command is still pending for the next tick");
        uint8_t junk; (void)pmgr_bus_take_latched(&junk);
    }

    /* SendReset: host asserts PMREQ* 100 us into the hold; the hold must last >= 3.0 ms, then end at once */
    {
        const uint8_t none = 0;
        pmgr_adb_queue(0x00, 0, &none, 0);
        PORTB.IN |= PMREQ_N_bm; PORTB.OUTCLR = PORTB.OUTSET = 0; pmack_low = 0; now_us = 0; t_rx0 = -1;
        req_assert_at = 100; release_after_us = 10; acked_at = -1; adb_assert_at = -1; adb_release_at = -1;
        PORTD.DIRCLR = 0; PORTD.DIRSET = 0;
        pmgr_bus_latch_arm(true);
        (void)pmgr_adb_service();
        if (PORTD.DIRSET & PMGR_ADB_bm) adb_release_at = now_us;      /* ADB_RELEASE() is the last write, no step after it */
        pmgr_bus_latch_arm(false); release_after_us = -1; req_assert_at = -1; PORTB.IN |= PMREQ_N_bm;
        long held = adb_release_at - adb_assert_at;
        printf("        (reset held %ld us, latched at %ld us, released at %ld us)\n", held, acked_at, adb_release_at);
        CHECK(held >= 3000, "fix A: SendReset still holds the bus low >= 3.0 ms");
        CHECK(acked_at >= 3000 && adb_release_at - acked_at <= 250, "fix A: host latched only after 3.0 ms, reset released within 250 us of it");
        uint8_t junk; (void)pmgr_bus_take_latched(&junk);
    }

    /* SendReset with no host activity: the full original hold, >= 4.6 ms */
    {
        const uint8_t none = 0;
        pmgr_adb_queue(0x00, 0, &none, 0);
        PORTB.IN |= PMREQ_N_bm; PORTB.OUTCLR = PORTB.OUTSET = 0; pmack_low = 0; now_us = 0; t_rx0 = -1;
        req_assert_at = -1; adb_assert_at = -1; adb_release_at = -1; PORTD.DIRCLR = 0; PORTD.DIRSET = 0;
        pmgr_bus_latch_arm(true);
        (void)pmgr_adb_service();
        if (PORTD.DIRSET & PMGR_ADB_bm) adb_release_at = now_us;
        pmgr_bus_latch_arm(false);
        printf("        (quiet reset held %ld us)\n", adb_release_at - adb_assert_at);
        CHECK(adb_release_at - adb_assert_at >= 4600 && !pmgr_bus_has_latched(), "fix A: SendReset with a quiet host holds >= 4.6 ms, as before");
    }

    /* the 1 Hz divider with latches: 600 ticks, a latch on every 7th, must yield 10 seconds, deferred ones included */
    {
        uint8_t div = 60; bool deferred = false; int ran = 0, deferred_ran = 0;
        for (int t = 1; t <= 600; t++) {
            bool latched = (t % 7) == 0;
            if (pmgr_tick_divider_step(&div, latched, &deferred)) ran++;
            if (deferred) { deferred = false; deferred_ran++; }     /* main loop: pmgr_tick_run_deferred() */
        }
        printf("        (1 Hz ran %d on time, %d deferred)\n", ran, deferred_ran);
        CHECK(ran + deferred_ran == 10 && deferred_ran >= 1, "fix A: 600 ticks with latches -> exactly 10 one-second ticks (some deferred)");
    }

    printf("\n  %s\n", fails ? "FAILURES" : "all pass");
    return fails != 0;
}
