/* MEASUREMENTS 2.214 (review, 2026-09-15): sweep a HOST WITH REAL DEADLINES across every ADB phase.
 *
 * test_adb_latch.c models a host that waits forever; the boot ROM does not. From 2.212, calibrated on the wire:
 *
 *     send attempt        460 loops = 1.76 ms before the host abandons the byte ($CD36)
 *     command byte        TWO attempts (pmgr.asm-side host at $901b28, dbra D2), each preceded by a bus-idle wait
 *     count / data byte   ONE attempt - no retry
 *
 * So: assert PMREQ* at every offset through a Talk, a Listen and a SendReset, and record how long the PMGR takes to
 * acknowledge. Anything over 1.76 ms would lose a count/data byte outright and burn one of a command byte's two
 * attempts; anything over ~3.5 ms loses a command byte entirely.
 *
 * The ADB device and the host live inside _delay_us(), 1 us per step, as in test_adb_latch.c. */
#include "pmgr_adb.h"
#include "pmgr_bus.h"
#include "pmgr_pins.h"
#include <stdio.h>
#include <string.h>

PORT_t PORTA, PORTB, PORTC, PORTD, PORTE, PORTF, PORTG;
EVSYS_t EVSYS; volatile uint8_t SREG;
static TCB_t tcb;
static int tcb_stage;
TCB_t *tcb0_ptr(void)
{
    if (tcb_stage == 2) { tcb.INTFLAGS &= (uint8_t)~TCB_CAPT_bm; tcb_stage = 0; }
    if (tcb_stage == 1) tcb_stage = 2;
    else if (tcb.INTFLAGS & TCB_CAPT_bm) tcb_stage = 1;
    return &tcb;
}
static int fails;
#define CHECK(c, m) do { if (c) printf("  PASS  %s\n", m); else { printf("  FAIL  %s\n", m); fails++; } } while (0)

#define SEND_DEADLINE_US   1760L      /* 2.212: one send attempt */
#define IDLE_WAIT_US        100L      /* the host's bus-idle wait before a retry (modelled, short = pessimistic) */

static long now_us, t_rx0 = -1;
static uint8_t reply[2] = { 0xA5, 0x81 };

/* --- host model: assert, wait up to SEND_DEADLINE_US, else abandon; optionally one retry (command byte) --- */
static long h_first_assert;           /* when the host first asserts */
static int  h_attempts_left;          /* 1 = count/data byte, 2 = command byte */
static long h_assert_at;              /* next assert time, -1 = not scheduled */
static int  h_asserted;
static long h_ack_delay;              /* ack delay of the attempt that succeeded, -1 = none did */
static int  h_abandoned;              /* attempts the host gave up on */

static void host_reset(long assert_at, int attempts)
{
    h_first_assert = assert_at; h_assert_at = assert_at; h_attempts_left = attempts;
    h_asserted = 0; h_ack_delay = -1; h_abandoned = 0;
    PORTB.IN |= PMREQ_N_bm;
}

static void host_step(void)
{
    if (!h_asserted && h_assert_at >= 0 && now_us >= h_assert_at) {   /* >=, not ==: an assert time of 0 is before the first step */
        PORTB.IN &= (uint8_t)~PMREQ_N_bm; h_asserted = 1;        /* PMREQ* low */
    }
    if (!h_asserted) return;
    if (PORTB.OUTCLR & PMACK_N_bm) {                             /* the PMGR acknowledged */
        PORTB.OUTCLR &= (uint8_t)~PMACK_N_bm;
        if (h_ack_delay < 0) h_ack_delay = now_us - h_assert_at;
        PORTB.IN |= PMREQ_N_bm;                                  /* host releases at once */
        h_asserted = 0; h_assert_at = -1;
        return;
    }
    if (now_us - h_assert_at >= SEND_DEADLINE_US) {               /* gave up on this attempt */
        PORTB.IN |= PMREQ_N_bm; h_asserted = 0; h_abandoned++;
        if (--h_attempts_left > 0) h_assert_at = now_us + IDLE_WAIT_US;
        else h_assert_at = -1;
    }
}

static void step(void)
{
    now_us++;
    host_step();
    if (PORTB.OUTSET & PMACK_N_bm) PORTB.OUTSET &= (uint8_t)~PMACK_N_bm;
    if (t_rx0 >= 0) {
        long k = now_us - t_rx0;
        if (k == 0) tcb.CCMP = (uint16_t)(35 * 12);
        for (int b = 0; b < 16; b++)
            if (k == 100L * (b + 1)) {
                tcb.CCMP = (uint16_t)((((reply[b / 8] >> (7 - b % 8)) & 1) ? 35 : 65) * 12);
                tcb.INTFLAGS |= TCB_CAPT_bm; tcb_stage = 0;
            }
        if (k == 1700) { tcb.CCMP = (uint16_t)(70 * 12); tcb.INTFLAGS |= TCB_CAPT_bm; tcb_stage = 0; }
    }
}
void _delay_us(double us) { for (long i = 0; i < (long)us; i++) step(); }
void _delay_ms(double ms) { _delay_us(ms * 1000.0); }

/* one sweep point: queue `cmd`, assert at `at`, run the tick's ADB work, return the ack delay (-1 = never) */
static long run_at(uint8_t cmd, const uint8_t *tx, uint8_t txlen, long at, int attempts, int *abandoned)
{
    tcb.INTFLAGS = 0; tcb_stage = 0; PORTB.OUTCLR = PORTB.OUTSET = 0;
    FDB_PORT.IN |= FDB_bm;
    now_us = 0;
    t_rx0 = ((cmd & 0x0Cu) == 0x0Cu) ? 1735 : -1;    /* Talk = bits 3:2 == 11 (pmgr_adb.c); Listen and Reset get no reply */
    host_reset(at, attempts);
    pmgr_adb_queue(cmd, 0, tx, txlen);
    pmgr_bus_latch_arm(true);
    (void)pmgr_adb_service();
    /* The tick has returned. In the real firmware the main loop now serves the byte immediately: pmgr_poll() takes a
     * latched one, or pmgr_bus_receive() handshakes a request that arrived after the tick. Model that as the loop
     * calling pmgr_bus_latch_command() every microsecond, and keep stepping so a RETRIED attempt is served too. */
    for (int i = 0; i < 6000; i++) {
        uint8_t junk;
        (void)pmgr_bus_take_latched(&junk);
        if (h_asserted) (void)pmgr_bus_latch_command();
        if (!h_asserted && h_assert_at < 0) break;          /* host is done: acknowledged or out of attempts */
        step();
    }
    pmgr_bus_latch_arm(false);
    uint8_t junk; (void)pmgr_bus_take_latched(&junk);
    t_rx0 = -1;
    *abandoned = h_abandoned;
    return h_ack_delay;
}

/* Recorded results of this sweep on fix A (b9b180e9), MEASUREMENTS 2.214. The count/data figures are NOT zero and are
 * not treated as failures here: they are the residual of 2.210/2.213, open and recorded. The test fails only if a
 * change makes any of them WORSE - that is what it is for. */
static void sweep(const char *name, uint8_t cmd, const uint8_t *tx, uint8_t txlen, long span,
                  int expect_single, int expect_both)
{
    long worst = -1, worst_at = -1; int lost_single = 0, lost_both = 0, n = 0;
    for (long at = 0; at <= span; at += 25) {
        int aband = 0;
        long d = run_at(cmd, tx, txlen, at, 2, &aband);           /* command byte: two attempts */
        n++;
        if (aband >= 1) lost_single++;                            /* a count/data byte here would have been lost */
        if (d < 0) lost_both++;                                   /* even a command byte gets no acknowledgement */
        if (d > worst) { worst = d; worst_at = at; }
    }
    printf("  %-26s points %3d | worst ack %5ld us (at +%ld us) | would lose a count/data byte: %d | "
           "command byte lost: %d\n", name, n, worst, worst_at, lost_single, lost_both);
    char msg[200];
    snprintf(msg, sizeof msg, "%s: command byte still survives every arrival time (two attempts): %d, recorded %d",
             name, lost_both, expect_both);
    CHECK(lost_both <= expect_both, msg);
    snprintf(msg, sizeof msg, "%s: count/data exposure no worse than recorded: %d of %d points, recorded %d",
             name, lost_single, n, expect_single);
    CHECK(lost_single <= expect_single, msg);
    if (lost_single < expect_single)
        printf("        ⭐ IMPROVED on the recorded %d - update the expectation in this test and in 2.214\n", expect_single);
}

int main(void)
{
    const uint8_t payload[2] = { 0x12, 0x34 };
    printf("host deadlines from MEASUREMENTS 2.212: one send attempt %ld us; command byte 2 attempts; count/data 1\n",
           SEND_DEADLINE_US);
    sweep("Talk (autopoll)",   0x2C, NULL,    0, 3200,  0, 0);
    sweep("Listen (2 bytes)",  0x2A, payload, 2, 5200, 73, 0);
    sweep("SendReset",         0x00, NULL,    0, 5200, 50, 0);
    printf("\n  ⚠️ The count/data exposure above is the KNOWN residual of 2.210/2.213 (the ADB transmit phase and the reset\n"
           "     hold are blind for longer than one 1.76 ms send attempt). It is bounded here, not fixed. Reachability in the\n"
           "     real machine is narrower - a count byte follows an ACKNOWLEDGED command byte by ~32 us, and after fix A that\n"
           "     command either aborted the ADB work or was taken outside it - which is why this is a WATCH, not a gate.\n");
    printf("\n  %s\n", fails ? "FAILURES (see 2.214 - some are EXPECTED and recorded, not silently accepted)" : "all pass");
    return fails != 0;
}
