#include "pmgr_bus.h"
#include "pmgr_pins.h"
#include <util/delay.h>

#define BUS_DIR_READ()   (BUS_PORT.DIR = 0x00)
#define BUS_DIR_WRITE()  (BUS_PORT.DIR = 0xFF)

void pmgr_bus_init(void)
{
    BUS_DIR_READ();                 /* Dir_Read = $00 in the original */

    /* PMACK and PMINT are outputs, idle HIGH (deasserted). Set the level
     * before the direction so we never glitch them low on startup. */
    DEASSERT_HIGH(PMACK_N_PORT, PMACK_N_bm);
    PMACK_N_PORT.DIRSET = PMACK_N_bm;
    DEASSERT_HIGH(PMINT_N_PORT, PMINT_N_bm);
    PMINT_N_PORT.DIRSET = PMINT_N_bm;

    PMREQ_N_PORT.DIRCLR = PMREQ_N_bm;               /* PMREQ is an input */
}

bool pmgr_bus_req_asserted(void)
{
    return PIN_IS_LOW(PMREQ_N_PORT, PMREQ_N_bm);
}

static bool wait_req(bool want_asserted, uint16_t budget_us)
{
    while (budget_us--) {
        if (pmgr_bus_req_asserted() == want_asserted)
            return true;
        _delay_us(1);
    }
    return false;
}

bool pmgr_bus_receive(uint8_t *out)
{
    if (!wait_req(true, PMGR_REQ_TIMEOUT_US))
        return false;

    /* pmgr.asm:336 - `lda VIA_Com` immediately after the bbc ladder. Latched
     * in one instruction, as the original does.
     *
     * ✅ THE SETUP RACE IS RESOLVED - MEASURED, MEASUREMENTS.md 2.22.
     *
     * The concern was that our 1 us polling latches sooner after PMREQ* falls
     * than the original's 5-cycle ladder (~2.5 us average at phi = 0.98 MHz),
     * and could therefore catch a partially settled bus.
     *
     * Measured with CH1 on PMREQ* (J21 pin 21) triggering the fall, D0-D7 on
     * the bus, 5 us/div = 50 ns/sample, six consecutive host transactions:
     *
     *     NO data line changed at any point in the 30 us BEFORE PMREQ* fell.
     *     Six for six, across two different command bytes ($10 and $69).
     *
     * So t_setup > 30 us - THIRTY TIMES our 1 us polling granularity. The host
     * drives the bus and settles it long before it strobes. The race cannot
     * occur, and NO settle delay is needed here.
     *
     * Note this is a BOUND, not an exact figure: the data was already stable
     * across the whole pre-trigger window, so the true setup time is larger
     * still and was never reached. A bound is sufficient - the code is safe for
     * any value above ~2 us, and 30 us clears that by a wide margin.
     *
     * ⚠️ The bound is against THIS host. It is a property of the Portable's VIA
     * timing, not of the PMGR. If this firmware is ever used behind different
     * host hardware, re-measure before assuming it still holds. */
    *out = BUS_PORT.IN;             /* whole bus in one instruction */

    ASSERT_LOW(PMACK_N_PORT, PMACK_N_bm);
    bool ok = wait_req(false, PMGR_REQ_TIMEOUT_US);
    DEASSERT_HIGH(PMACK_N_PORT, PMACK_N_bm);
    return ok;
}

#ifdef PMGR_ADB_HOST_SERVICE
/* See pmgr_bus.h. The handshake is pmgr_bus_receive()'s, minus the initial wait:
 * the caller only calls when it is prepared to spend the release time. */
static bool    s_latch_armed;
static bool    s_latched;
static uint8_t s_latch_byte;
uint16_t pmgr_latch_ok, pmgr_latch_slow, pmgr_latch_failed;

void pmgr_bus_latch_arm(bool on) { s_latch_armed = on; }
bool pmgr_bus_has_latched(void)  { return s_latched; }

bool pmgr_bus_take_latched(uint8_t *out)
{
    if (!s_latched) return false;
    *out = s_latch_byte;
    s_latched = false;
    return true;
}

pmgr_latch_t pmgr_bus_latch_command(void)
{
    if (!s_latch_armed || s_latched || !pmgr_bus_req_asserted())
        return LATCH_NONE;

    const uint8_t b = BUS_PORT.IN;          /* same instant as pmgr_bus_receive() */
    ASSERT_LOW(PMACK_N_PORT, PMACK_N_bm);

    /* ⛔ Wait the FULL receive timeout for release, never less. Releasing
     * PMACK* before the host releases PMREQ* would let it believe the byte was
     * taken when it was not - a silent desync (design §9). */
    uint16_t used = 0;
    while (pmgr_bus_req_asserted()) {
        if (++used > PMGR_REQ_TIMEOUT_US) break;
        _delay_us(1);
    }
    DEASSERT_HIGH(PMACK_N_PORT, PMACK_N_bm);

    if (used > PMGR_REQ_TIMEOUT_US) { pmgr_latch_failed++; return LATCH_FAILED; }

    s_latch_byte = b;
    s_latched    = true;
    if (used > PMGR_LATCH_FAST_US) { pmgr_latch_slow++; return LATCH_SLOW; }
    pmgr_latch_ok++;
    return LATCH_OK;
}
#endif

bool pmgr_bus_send(uint8_t value)
{
    /* ⭐⭐ DO NOT REMOVE - VALIDATED 2.135. Match the original's ack-release -> reply gap.
     * On a 24 MHz AVR the path from the last receive's DEASSERT_HIGH(PMACK)
     * to the ASSERT_LOW below is ~5 us; the original takes ~22 us. At 5 us the
     * System's readINT handler NEVER takes our reply (PMREQ* stays high, we hold
     * the byte to timeout, ADBInt never clears, ADB never runs again - 2.131).
     * Second speed-induced fault of the $FFFFCD34 class (:151 below). */
    _delay_us(PMGR_REPLY_GAP_US);

    /* SendByte in the original opens with  bbc PMREQ,Port_P3,.Fail
     * - branch on bit CLEAR, i.e. fail if PMREQ is already LOW.
     * The host must have PMREQ HIGH before the PMGR may drive the bus. */
    if (pmgr_bus_req_asserted())
        return false;

    BUS_PORT.OUT = value;
    BUS_DIR_WRITE();

    ASSERT_LOW(PMACK_N_PORT, PMACK_N_bm);

    /* ...and completes on  bbc PMREQ,Port_P3,.Done  - the host takes the byte
     * by driving PMREQ LOW. This is the OPPOSITE edge from ReceiveByte, which
     * waits for PMREQ to go high. The protocol is not symmetric. */
    bool ok = wait_req(true, PMGR_REQ_TIMEOUT_US);

    /* .Done / .Fail: seb PMACK, then Dir_Read (pmgr.asm 391-392, 396-397).
     * Order matters - release the strobe before letting go of the bus. */
    DEASSERT_HIGH(PMACK_N_PORT, PMACK_N_bm);

    /* ⛔⛔⛔ DO NOT MOVE BUS_DIR_READ() BACK UP HERE. It lives below the
     * PMREQ-high wait, and that placement is load-bearing. This was the
     * $FFFFCD34 fault - see MEASUREMENTS.md 2.100-2.105.
     *
     * The host reads the byte ONE INSTRUCTION after pulling PMREQ low:
     *   $1CA4  bclr.b #$0,(a1)        PMREQ* LOW
     *   $1CA8  move.w #$40,d5
     *   $1CAC  move.b $1e00(a1),d0    <- the actual VIA read
     * Releasing on the PMREQ edge lets the bus go BEFORE that read.
     *
     * MEASURED - two separate samples, do NOT mix their figures:
     *   2.102  n=16, MIXED bytes      1.268 - 2.920 us   spread 1.65 us  sd 0.523
     *   2.103  n=14, IDENTICAL ($68)  1.520 - 2.890 us   spread 1.37 us  sd 0.450
     *   bus reaches $FF within ~124 ns of release        (repeatable to ~10 ns)
     * The equivalent-byte subset is what shows the spread is NOT byte-to-byte
     * variation; the mixed sample is the wider bound.
     * All the variation is in WHEN we release - wait_req polls at 1 us - so a
     * fixed host sampling instant inside that spread gives an INTERMITTENT
     * fault. That is why this presented as "2 boots in 3" and resisted seven
     * falsified hypotheses.
     *
     * The host then misreads the reply's COUNT byte, expects more payload than
     * we send, waits for a byte that never comes, and reports $CD34 - which is
     * exactly the ROM's receive timeout at $1C9C.
     *
     * A/B CONFIRMED, 2.105, same board and session, n=8 per arm:
     *   with this placement     7 of 8 POST passes, CD34 0
     *                           8th boot = ERROR 01000E3E / 00001FF2,
     *                           unexplained, never recurred in 15 further boots
     *   released early instead  0 of 8 POST passes, CD34 5   (p = 0.0014)
     *
     * ⛔ Handshake timing is UNCHANGED by this: PMACK* rises exactly where it
     * always did, and no guard, timeout or dispatch path is touched. Only how
     * long the bus stays driven AFTER the handshake completes. */

    /* ⭐ RACE FIX - do not return while PMREQ is still asserted.
     *
     * We reach here having just waited for PMREQ to go LOW, so it IS low. The
     * next SendByte opens with `bbc PMREQ,Port_P3,.Fail` (:379) - it FAILS if
     * PMREQ is low. So without this wait, the second byte of every reply
     * aborts, and ReturnDataToHost2 always sends at least two (command, then
     * ByteCount, then payload).
     *
     * The original gets away with the same structure only because it is SLOW.
     * Between `.Done`'s seb PMACK (:396) and the next `bbc PMREQ` (:379) it
     * executes rts + bcs + lda + jsr + bbc, about 22 cycles ~= 22 us at
     * phi = 0.98 MHz. On a 24 MHz AVR the same path is well under 1 us.
     *
     * MEASUREMENTS.md 2.12 shows why 22 us is enough and 1 us is not: PMACK*
     * low is 12-20 us while PMREQ* low is 16-108 us. The host routinely holds
     * PMREQ down for tens of microseconds after we release PMACK.
     *
     * This is a SPEED-INDUCED bug - correct-looking code that only works
     * because the original could not run fast enough to lose the race. Expect
     * more of this class wherever the original polls a host-driven line. */
    bool released = ok ? wait_req(false, PMGR_REQ_TIMEOUT_US) : true;

    /* ⭐ The bus is released HERE - after the host has released PMREQ, which by
     * ROM control flow is after its read ($1CAC precedes `bset #0,(a1)` at
     * $1C7A). Self-limiting: no arbitrary delay, the host's own edge ends it.
     *
     * Unconditional, and after a BOUNDED wait_req, so a host that never
     * releases PMREQ cannot leave us driving the bus indefinitely.
     *
     * No contention: the host holds DDRA=0 through receive, and on the paths
     * where it does drive ($1C20, $1C42) it raises PMREQ at $1C7A first -
     * measured 2.5-4 us before it drives, against our 1 us poll. */
    BUS_DIR_READ();

    if (ok && !released)
        return false;                   /* host never released PMREQ */

    return ok;
}

void pmgr_bus_interrupt_host(void)
{
    ASSERT_LOW(PMINT_N_PORT, PMINT_N_bm);
    _delay_us(PMGR_INT_PULSE_US);
    DEASSERT_HIGH(PMINT_N_PORT, PMINT_N_bm);
}
