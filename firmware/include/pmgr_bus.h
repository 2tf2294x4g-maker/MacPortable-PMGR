/* PMGR byte-level handshake over the VIA parallel bus.
 * See PROTOCOL-SPEC.md section 2. All handshake lines are ACTIVE LOW.
 *
 * The two directions use OPPOSITE PMREQ edges - the protocol is not symmetric:
 *
 *   RECEIVE: wait PMREQ LOW  -> read bus -> assert PMACK -> wait PMREQ HIGH
 *   SEND:    require PMREQ HIGH -> drive bus -> assert PMACK -> wait PMREQ LOW
 *
 * Verified against pmgr.asm (PM_ReceiveByte_Wait/ReceiveByte at $335, SendByte
 * at $378). An earlier revision of PROTOCOL-SPEC.md had SendByte inverted.
 */
#ifndef PMGR_BUS_H
#define PMGR_BUS_H

#include <stdint.h>
#include <stdbool.h>

/* The original polls with 9 unrolled tests and an X-register loop; falling
 * through means timeout. We use a microsecond budget instead - same effect,
 * but explicit. Tune against the real machine during bring-up. */
#define PMGR_REQ_TIMEOUT_US   2000u

/* PMINT* pulse width.
 *
 * ⭐ MEASURED 10.240 us on real hardware 2026-08-28 - MEASUREMENTS.md 2.3.
 * This value was a supplied guess; it is now confirmed correct.
 *
 * pmgr.asm 295-297:  clb PMINT / seb 1,$0 / seb PMINT.  Note the instruction
 * BETWEEN the two port writes - that is why this is 10 cycles and 1SEC* below
 * is only 5. At the measured core clock (0.983065 MHz) 10 cycles = 10.17 us;
 * measured 10.240 us, the extra ~50 ns being the open-drain pull-up's rise
 * time crossing the analyser threshold late. */
#define PMGR_INT_PULSE_US     10u

/* 1SEC* pulse width (pmgr.asm 583-584: clb OneSec / seb OneSec).
 *
 * ⭐ MEASURED 5.120 us on real hardware 2026-08-28 - MEASUREMENTS.md 2.4.
 * Also a supplied guess originally, also confirmed correct.
 *
 * SEB/CLB on a zero-page port are 5-cycle read-modify-write ops on the 740
 * core, and here they are ADJACENT, so the pulse is exactly 5 cycles. On a
 * 24 MHz AVR the same two statements are ~83 ns - far too narrow for the VIA's
 * CA2 input to register. Reproduce the ORIGINAL'S DURATION, not its
 * instruction count.
 *
 * The measured 10.240 / 5.120 = exactly 2.000 confirms the 10:5 cycle ratio
 * independently of any clock calibration.
 *
 * NOTE: the "~2 MHz" figure this comment previously used was WRONG. The
 * M50753 divides XIN by 4, not 2:
 *
 *     XIN  = 3.93216 MHz   (a standard crystal)
 *     phi  = 0.983065 MHz
 *     1 machine cycle = 1.01723 us
 *
 * Confirmed to +26 ppm with the scope's hardware frequency counter on the
 * analog channel: 3.932261 MHz, nine identical reads. See MEASUREMENTS.md
 * 2.1-ter. */
#define PMGR_ONESEC_PULSE_US  5u

void pmgr_bus_init(void);

/* Returns false on timeout. */
bool pmgr_bus_receive(uint8_t *out);
bool pmgr_bus_send(uint8_t value);

/* ⭐ VALIDATED 2026-09-12 - pre-registered A/B, MEASUREMENTS.md 2.131-2.135:
 * arm A (this gap) 4 of 4 cold boots ADB alive after Welcome; arm B (baseline
 * revert, same session) 4 of 4 dead with the echo held and PMREQ* high.
 *
 * Minimum gap between releasing an acknowledge and driving the next reply
 * byte. Measured on the wire: we assert the readINT echo 5.0 us after
 * releasing the count byte's ack, in BOTH phases. The ROM's handler takes it;
 * the System's handler never does, and PMREQ* sits high while we hold the byte
 * to the 2 ms timeout. The original's gap is ~22 cycles at ~1 us (pmgr_bus.c
 * :143-145). This reproduces the ORIGINAL'S DURATION, as PMGR_INT_PULSE_US and
 * PMGR_ONESEC_PULSE_US do. ⛔ The true minimum is UNMEASURED - only that ~5 us
 * fails and 20 us works. ⛔ DO NOT REDUCE without a new A/B. */
#define PMGR_REPLY_GAP_US     20u

/* PMINT is a PULSE, not a level (spec section 2). */
void pmgr_bus_interrupt_host(void);

bool pmgr_bus_req_asserted(void);

#ifdef PMGR_ADB_HOST_SERVICE
/* ⭐ Serving the host DURING ADB work (design/ADB-HOST-SERVICE.md, MEASUREMENTS 2.180).
 *
 * The host's only fragile byte is the COMMAND byte: it waits ~500 us for PMACK*
 * and retries 8 times; count and data bytes wait ~16x longer. So while the 60 Hz
 * tick is doing ADB work, the ADB code calls pmgr_bus_latch_command() at points
 * where no ADB timing is affected. If the host is asserting PMREQ*, it completes
 * ONE command-byte handshake exactly as pmgr_bus_receive() would and stores the
 * byte; pmgr_poll() takes it as the command afterwards and receives the rest.
 *
 *   arm      only around pmgr_tick_60hz() in the main loop (never reset/sleep/wake)
 *   returns  LATCH_NONE (nothing done), LATCH_OK, LATCH_SLOW (release took
 *            > PMGR_LATCH_FAST_US - the caller may treat its own timing as
 *            disturbed), LATCH_FAILED (host never released; nothing stored, as a
 *            failed pmgr_bus_receive()) */
#define PMGR_LATCH_FAST_US  60u
typedef enum { LATCH_NONE, LATCH_OK, LATCH_SLOW, LATCH_FAILED } pmgr_latch_t;
void         pmgr_bus_latch_arm(bool on);
pmgr_latch_t pmgr_bus_latch_command(void);
bool         pmgr_bus_take_latched(uint8_t *out);
bool         pmgr_bus_has_latched(void);
extern uint16_t pmgr_latch_ok, pmgr_latch_slow, pmgr_latch_failed;   /* diagnostics */
#endif

#endif /* PMGR_BUS_H */
