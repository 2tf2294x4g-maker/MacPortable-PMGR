#include "pmgr_adb.h"
#include "pmgr_pins.h"
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

/*
 * ADB DRIVE — established by measurement 2026-08-27 (see ADB-TIMING.md 3a)
 *
 * The exact external topology on pin 28 is NOT resolved. In-circuit diode-mode
 * readings were ambiguous: ~0.6-0.7 V to GND in BOTH polarities (so not a plain
 * junction), ~1.0 V to +5 V. In-circuit measurements see parallel paths and
 * cannot be untangled without lifting the pin.
 *
 * But the required firmware behaviour does NOT depend on knowing the topology.
 * It follows from three facts, each independently solid:
 *
 *   1. SCOPE: pin 28 never exceeds ~0.74 V. It is CLAMPED — 735 mV Top against
 *      the bus's 4.98 V. Whatever clamps it, it is not a logic-level node.
 *   2. SCOPE: pin 28 is INVERTED from the bus and simultaneous with it
 *      (complementary duty, identical 2.730 ms period, 20 us coincidence).
 *      Pin 28 HIGH  <=>  bus LOW.
 *   3. DATASHEET: the M50753's ports are N-channel open drain. They SINK.
 *      They cannot SOURCE.
 *
 * From 1+3: something EXTERNAL raises pin 28 to 0.74 V, because the original
 * chip could not have. From 2: that raised state is what asserts the bus.
 *
 * Therefore:
 *   assert  (bus LOW)  -> release to Hi-Z, let the external pull-up raise pin 28
 *   release (bus HIGH) -> sink pin 28 to 0 V
 *
 * ⛔ PD1 MUST NEVER BE DRIVEN HIGH.
 *    The node clamps at ~0.74 V. Push-pull driving 5 V into a clamped node
 *    sources current limited only by the AVR's OUTPUT IMPEDANCE into the
 *    clamp. ⚠️ Earlier revisions quoted 170 mA from an assumed ~25 ohm; that
 *    figure is RETRACTED. The impedance is UNMEASURED, so no specific
 *    milliamp figure here is supported - earlier revisions quoted 170 mA
 *    from an assumed ~25 ohm. What IS supported: it far exceeds the 20 mA
 *    at which the datasheet specifies output levels, and risks the +/-50 mA
 *    absolute maximum. On the first ADB
 *    transaction. An earlier revision of this file did exactly that.
 *    This holds whatever the clamp turns out to be.
 *
 * Sinking draws ~0.5 mA through R103 (10k) to the (5/0) rail. Well within spec.
 * ⚠️ CORRECTED 2026-09-06: this said "roughly 5 mA through the ~1k path" - a
 * 10x error. The ~1k was an ASSUMPTION made before Apple 050-0219 sheet 12 was
 * traced; the resistor is 10k (MEASUREMENTS.md 2.58).
 *
 * PD2 / FDB reads the bus directly, true polarity, internal pull-up DISABLED.
 */

/* ---- Bus primitives — OPEN DRAIN, INVERTED SENSE --------------------------
 * Note the asymmetry: asserting is the PASSIVE state. That is backwards from
 * most open-drain code and it is deliberate — the external pull-up does the
 * asserting, and the AVR only ever interrupts it. */
#define ADB_ASSERT()    (PMGR_ADB_PORT.DIRCLR = PMGR_ADB_bm)   /* Hi-Z -> bus LOW  */
#define ADB_RELEASE()   do { PMGR_ADB_PORT.OUTCLR = PMGR_ADB_bm;                 \
                             PMGR_ADB_PORT.DIRSET = PMGR_ADB_bm; } while (0)
                                                               /* sink -> bus HIGH */
#define ADB_BUS_HIGH()  ((FDB_PORT.IN & FDB_bm) != 0)

/* ---- Timing, from ADB-TIMING.md 2 -----------------------------------------
 * Derived by matching six code paths in pmgr.asm against the ADB bit cell,
 * which fixed the original's core clock at 1 cycle = 1 us. These are the
 * nominal ADB values; the cited numbers are what the original actually
 * produced, and every one lands inside spec tolerance. */
#define T_ATTENTION   800u   /* ADBCMDDo entry, DelayLoop(131) -> 805 us */
#define T_SYNC         65u   /* DelayLoop(6)                   ->  53 us */
#define T_BIT1_LOW     35u   /* bcc-not-taken path             ->  36 us */
#define T_BIT0_LOW     65u   /* DelayExit + DelayLoop(1) path  ->  68 us */
#define T_BIT1_HIGH    65u   /* remainder of the 100 us cell             */
#define T_BIT0_HIGH    35u
#define T_STOP         70u   /* DelayLoop(9)                   ->  71 us */
#define T_TLT         160u   /* DelayLoop(25)                  -> 160 us */

/* Settling allowance before the contention read-back. The original checks the
 * line with a bbc placed one instruction after the clb, i.e. ~5 us later. */
#define T_SETTLE        5u
/* ⭐⭐ VALIDATED 2026-09-13 - pre-registered A/B, MEASUREMENTS.md 2.147-2.149: with this
 * window the keyboard types on 3 of 3 counted boots; without it the first keystroke
 * freezes the machine on 3 of 3. ⛔ DO NOT REDUCE without a new A/B.
 * The SRQ window sampled 28 x T_SETTLE = ~140 us and a real SRQ measured
 * 270-280 us on the wire, so every SRQ fell into .fail as CONTENTION. The
 * original's 9-then-19 samples must span >= 300 us, i.e. >= 11 us each. This
 * reproduces the DURATION, not the count, as PMGR_INT_PULSE_US does. */
#define T_SRQ_SAMPLE   12u   /* 28 x 12 = 336 us window; 9 x 12 = 108 us threshold */

/* ---- Receive timing -------------------------------------------------------
 * TCB0 runs at CLK_PER/2 = 12 MHz, so 12 ticks per microsecond. A 16-bit
 * capture spans 5.46 ms, comfortably past every ADB interval. */
#define TCB_TICKS_PER_US   (F_CPU / 2000000UL)
#define US_TO_TICKS(us)    ((uint16_t)((us) * TCB_TICKS_PER_US))

/* Bit decision threshold: midpoint of the 35 us and 65 us low periods.
 * ADBCMDDo2's ladder makes the same split by counting sample rungs. */
#define T_BIT_THRESHOLD_US   50u

/* A low pulse longer than this is not a data bit. In the original this is the
 * point where the ladder runs off its end into .fail. */
#define T_LOW_MAX_US        110u

/* Device must start its reply within Tlt. Spec allows 140-260 us; the original
 * gives up after ~16 ladder samples (.LAB_ECD2, ldx #16). */
#define T_REPLY_TIMEOUT_US  300u

/* Gap after a bit cell before the frame is considered finished. One cell is
 * 100 us; anything past ~150 us means the device has stopped talking. */
#define T_FRAME_GAP_US      150u

/* ADB command byte: bits 7-4 address, bits 3-2 command, bits 1-0 register.
 * Command 11 = Talk, the only one that makes the device drive a reply. */
#define ADB_CMD_IS_TALK(c)  (((c) & 0x0Cu) == 0x0Cu)

/* --------------------------------------------------------------------------
 * TCB0 in Input Capture Pulse-Width mode, fed from PD2 via the event system.
 * EVCTRL.EDGE = 1 makes the NEGATIVE edge start the counter and the POSITIVE
 * edge capture it, so each CCMP is the duration of one bus-LOW pulse - exactly
 * the quantity the assembly's sample ladders are counting.
 * -------------------------------------------------------------------------- */
void pmgr_adb_init(void)
{
    /* FDB reads the bus directly; the Portable's ADB network supplies the
     * pull-up, so the internal one must stay off or it fights the bus and
     * masks contention detection. Input buffer stays enabled (unlike the
     * ADC pins) because both the TCB event and the polled reads need it. */
    FDB_PORT.PIN2CTRL = 0;

    /* PORTD pin generators are only routable on channels 2 and 3 (see the
     * EVSYS generator table in ioavr128db64.h); channel 0 cannot see PD2. */
    EVSYS.CHANNEL2     = EVSYS_CHANNEL2_PORTD_PIN2_gc;
    EVSYS.USERTCB0CAPT = EVSYS_USER_CHANNEL2_gc;

    TCB0.CTRLB  = TCB_CNTMODE_PW_gc;
    TCB0.EVCTRL = TCB_CAPTEI_bm | TCB_EDGE_bm;   /* EDGE=1 -> measure LOW */
    TCB0.CTRLA  = TCB_CLKSEL_DIV2_gc | TCB_ENABLE_bm;
    TCB0.INTFLAGS = TCB_CAPT_bm;                 /* clear stale capture */
}

/* Wait for one completed low pulse. Returns its width in timer ticks, or 0 on
 * timeout. The measurement is hardware-exact; only the timeout is approximate,
 * which is the correct split - no bit decision depends on this loop. */
#ifdef PMGR_ADB_HOST_SERVICE
#include "pmgr_bus.h"
/* Set when a host latch during reply collection took longer than
 * PMGR_LATCH_FAST_US - LATCH_SLOW, or LATCH_FAILED, which spent the whole
 * receive timeout (~2 ms): an ADB cell may have been overwritten in TCB0, so the
 * reply is discarded as failed (design §9) - recoverable by the host's next
 * poll, unlike a PMGR desync.
 *
 * ⛔ LATCH_FAILED WAS MISSED (review, MEASUREMENTS 2.183): only LATCH_SLOW set
 * this, so a host that never released PMREQ* during a reply let a corrupted
 * reply through as success. test_adb_latch.c reproduces it. */
static bool s_rx_disturbed;

/* ⭐ The ONLY places the ADB code looks at the host (design §9): before the
 * transaction, inside reply collection, after it, and in the SendReset hold.
 * Never inside attention, sync, bits, stop, the SRQ window or Tlt. */
static inline void adb_host_service(void)
{
    const pmgr_latch_t r = pmgr_bus_latch_command();
    /* ⭐ FIX A (design §10, MEASUREMENTS 2.208-2.209): ANY latch - not only a slow or failed one - ends reply
     * collection and discards the reply, because the host is now waiting for its count byte. System _PMgrOp waits
     * ~8 ms for it, but the boot ROM's POST gives up after ~1.76 ms (Sad Mac 0000CD36 on a restart). */
    if (r != LATCH_NONE) s_rx_disturbed = true;
}
#endif

static uint16_t adb_capture(uint16_t timeout_us)
{
#ifdef PMGR_ADB_HOST_SERVICE
    uint8_t every = 0;
#endif
    while (timeout_us--) {
        if (TCB0.INTFLAGS & TCB_CAPT_bm) {
            uint16_t w = TCB0.CCMP;              /* reading CCMP clears CAPT */
            return w ? w : 1u;                   /* never return 0 on success */
        }
#ifdef PMGR_ADB_HOST_SERVICE
        /* Width capture is in hardware, so a quick handshake here cannot move a
         * bit edge; checked every 16 iterations to keep the loop's own timing. */
        if ((++every & 0x0Fu) == 0u) {
            adb_host_service();
            if (pmgr_bus_has_latched()) return 0u;   /* fix A: stop collecting, serve the host */
        }
#endif
        _delay_us(1);
    }
    return 0u;
}

/* ---- Transmit -------------------------------------------------------------
 * Literal translation of the ADBCMDDo bit cell (pmgr.asm .LAB_EC10-.LAB_EC23).
 * Both arms are written out so _delay_us gets compile-time constants. */
static bool adb_send_bit(bool one)
{
    if (one) {
        ADB_ASSERT();  _delay_us(T_BIT1_LOW);
        ADB_RELEASE(); _delay_us(T_SETTLE);
        /* bbc ADB_In,Port_P4,.fail - the line must have risen. This is the
         * bus-contention test, not a formality: another device holding the
         * bus low here is exactly what adb_buscontention reports. */
        if (!ADB_BUS_HIGH()) return false;
        _delay_us(T_BIT1_HIGH - T_SETTLE);
    } else {
        ADB_ASSERT();  _delay_us(T_BIT0_LOW);
        ADB_RELEASE(); _delay_us(T_SETTLE);
        if (!ADB_BUS_HIGH()) return false;
        _delay_us(T_BIT0_HIGH - T_SETTLE);
    }
    return true;
}

/* ldy #8 / rol $4 / ... / dey / bne - MSB first. */
static bool adb_send_byte(uint8_t v)
{
    for (uint8_t i = 0; i < 8u; i++) {
        if (!adb_send_bit((v & 0x80u) != 0u)) return false;
        v = (uint8_t)(v << 1);
    }
    return true;
}

/* ---- Receive --------------------------------------------------------------
 * Semantic translation of ADBCMDDo2. Correspondence to the original:
 *
 *   .LAB_ECD2  ldx #16, wait for start bit   -> T_REPLY_TIMEOUT_US
 *   .fail      seb adb_noreply               -> ADB_ST_NOREPLY
 *   .LAB_ED28+ the sample ladders            -> T_BIT_THRESHOLD_US
 *   .LAB_ED5B  jmp .fail (ran off the end)   -> T_LOW_MAX_US
 *   .LAB_EDF0  exit, ADBCMDLength = 9 - Y    -> *rxlen
 *
 * (:902-906 is `tya / eor #$FF / clc / adc #$A` = (255-Y)+10 = 265-Y, which
 * wraps to 9-Y - NOT 10-Y as an earlier revision of this comment claimed. Y is
 * loaded with 9 at :752 and decremented once per byte, so 9-Y is exactly the
 * number of bytes received, which is what *rxlen holds.)
 *
 * The original's start bit is consumed by the ladder that detects it; it is a
 * framing bit and carries no data, so it is discarded here too.
 *
 * ⭐ THE STOP BIT. The original commits a bit on the FALLING EDGE OF THE NEXT
 * CELL, not at the end of its own: .LAB_EDC5 (`asl A`, commit 0) and .LAB_EDDD
 * (`sec / rol A`, commit 1) are reachable only from the ladder at :819-852 that
 * waits for the line to go low AGAIN. The stop bit has no following cell, so it
 * is measured and then silently dropped - and `cmp #1` at :867 checks that the
 * accumulator holds only its sentinel, i.e. that no partial byte accumulated.
 *
 * We commit as soon as adb_capture() returns, so the stop bit would otherwise
 * be shifted in as data and the next capture would time out mid-byte, failing
 * the whole frame. A 2-byte Talk R0 reply - the commonest transaction on the
 * bus - returned false with rxlen 0 because of it.
 *
 * The equivalent test here: at a byte boundary, exactly ONE further pulse
 * followed by silence is the stop bit. Two or more means the device really did
 * stop mid-byte. */
static bool adb_receive(uint8_t *rx, uint8_t *rxlen)
{
    *rxlen = 0;

    TCB0.INTFLAGS = TCB_CAPT_bm;    /* discard anything from the TX phase */
#ifdef PMGR_ADB_HOST_SERVICE
    s_rx_disturbed = false;
#endif

    /* ⭐ THIS FUNCTION SETS NO STATUS BITS AT ALL - by design, and it is a fix.
     *
     * The original's receive routine (ADBCMDDo2) has exactly ONE failure label
     * and it sets BOTH bits, unconditionally:
     *
     *     .fail   seb adb_error,ADBStatus
     *             seb adb_noreply,ADBStatus
     *
     * Every failure branches there - the start-bit timeout, the framing checks,
     * the over-long low. There is no path that sets one without the other.
     *
     * ⛔ We used to set adb_noreply HERE, on the start-bit timeout only, and
     * let the caller add adb_error. That produced two divergences at once:
     * the other four failure paths reported ERROR with NO NOREPLY, and the
     * absent-device case reported NOREPLY with NO ERROR. An old comment here
     * claimed the latter was deliberate ("normal for a device that is not
     * present"). It was not - the original sets both even for a silent bus,
     * because the start-bit timeout falls through to that same .fail.
     *
     * So: report success or failure, nothing else. The single call site sets
     * both bits, which is what .fail does. */
    uint16_t w = adb_capture(T_REPLY_TIMEOUT_US);
    if (w == 0u) return false;              /* silent bus - still .fail */
    if (w > US_TO_TICKS(T_LOW_MAX_US)) return false;   /* malformed start */

    for (uint8_t byte = 0; byte < ADB_RX_MAX; byte++) {
        uint8_t v = 0;
        for (uint8_t bit = 0; bit < 8u; bit++) {
            /* First bit of a byte may follow a longer gap than later bits, so
             * every bit uses the frame-gap timeout. */
            w = adb_capture(T_FRAME_GAP_US);
            if (w == 0u) {
                /* No further pulse. Position within the byte says what ended:
                 *
                 *   bit 0  nothing at all after the last complete byte - the
                 *          frame simply stopped. Clean.
                 *   bit 1  exactly ONE pulse, then silence. That pulse was the
                 *          STOP BIT: the original never commits it, because a
                 *          bit is committed on the next falling edge and there
                 *          is none. Discard it - `v` is thrown away here - and
                 *          end the frame. This is the NORMAL exit for every
                 *          well-formed reply.
                 *   bit>1  real data bits accumulated and then the device went
                 *          quiet mid-byte. Genuinely truncated.
                 *
                 * The bit<=1 test is our equivalent of `cmp #1` at :867: at a
                 * byte boundary the accumulator must hold nothing but its
                 * sentinel. */
                if (bit > 1u) return false;     /* truncated mid-byte */
                if (byte == 0u) return false;   /* no complete byte at all */
#ifdef PMGR_ADB_HOST_SERVICE
                if (s_rx_disturbed) return false;   /* design §9 */
#endif
                *rxlen = byte;
                return true;                    /* clean frame end */
            }
            if (w > US_TO_TICKS(T_LOW_MAX_US)) return false;   /* .LAB_ED5B */

            /* Short low = 1, long low = 0. */
            v = (uint8_t)(v << 1);
            if (w < US_TO_TICKS(T_BIT_THRESHOLD_US)) v |= 1u;
        }
        rx[byte] = v;
    }

#ifdef PMGR_ADB_HOST_SERVICE
    if (s_rx_disturbed) return false;           /* design §9 */
#endif
    *rxlen = ADB_RX_MAX;
    return true;
}

/* -------------------------------------------------------------------------- */
static bool adb_transact_locked(uint8_t cmd,
                                const uint8_t *tx, uint8_t txlen,
                                uint8_t *rx, uint8_t *rxlen,
                                uint8_t *status)
{
    /* lda ADBStatus / and #%101 / sta ADBStatus (pmgr.asm 615-617) */
    uint8_t st = (uint8_t)(*status & ADB_ST_PRESERVED);
    *rxlen = 0;

    /* (Fix A: the host is checked BEFORE the transaction in pmgr_adb_service(), where a latch can still leave the
     * queued command pending. Latching here instead would lose the command or run a ~2.6 ms Listen blind.) */
    /* Attention, then sync. */
    ADB_ASSERT();
    _delay_us(T_ATTENTION);
    ADB_RELEASE();
    _delay_us(T_SYNC);

    /* bbc ADB_In,Port_P4,.fail - bus must be idle high after sync. */
    if (!ADB_BUS_HIGH()) goto fail;

    if (!adb_send_byte(cmd)) goto fail;

    /* Stop bit. */
    ADB_ASSERT();
    _delay_us(T_STOP);
    ADB_RELEASE();

    /* SRQ window (.LAB_EC42-.LAB_EC54): a device may hold the line low past
     * the stop bit to request service. The original samples up to 9 times for
     * the line to rise, then up to 19 more; still low after that is .fail. */
    {
        uint8_t n = 0;
        while (!ADB_BUS_HIGH()) {
            if (++n > 28u) goto fail;
            _delay_us(T_SRQ_SAMPLE);          /* was T_SETTLE (5 us) - see 2.133 */
        }
        if (n > 9u) st |= ADB_ST_SRQ;
    }

    /* Data phase, if any: Tlt gap then the payload (.LAB_EC66 onward). */
    if (txlen != 0u) {
        _delay_us(T_TLT);
        for (uint8_t i = 0; i < txlen; i++) {
            if (!adb_send_byte(tx[i])) goto fail;
        }
        ADB_ASSERT();
        _delay_us(T_STOP);
        ADB_RELEASE();
    }

    /* ADBCMDDo ENDS HERE. In the original, reception is a separate routine
     * (ADBCMDDo2) that the caller invokes only when a reply is expected -
     * .LAB_EC60 returns clc/rts without ever listening. Only a Talk command
     * produces a device reply; Listen, Flush and SendReset do not. Calling
     * ADBCMDDo2 after a Listen would report adb_noreply on every write. */
    if (!ADB_CMD_IS_TALK(cmd)) {
        *status = st;
        return true;
    }

    if (!adb_receive(rx, rxlen)) {
        /* .fail (ADBCMDDo2): seb adb_error / seb adb_noreply - BOTH, on every
         * failure, with no distinction between a silent bus and a mangled
         * frame. See the note in adb_receive(). */
        st |= (uint8_t)(ADB_ST_ERROR | ADB_ST_NOREPLY);
        *status = st;
        return false;
    }

    *status = st;
    return true;

fail:
    /* .fail: seb adb_buscontention / seb adb_error / clb ADB_Out / sec */
    st |= (uint8_t)(ADB_ST_CONTENTION | ADB_ST_ERROR);
    ADB_RELEASE();
    *status = st;
    return false;
}

/* ⭐ ADB RUNS WITH INTERRUPTS DISABLED.
 *
 * The bit cells are 35 us and 65 us of software-timed low (adb_send_bit), so
 * any ISR that lands mid-cell stretches that low by its own duration and
 * corrupts the bit. RX is immune - TCB0 captures widths in hardware - but TX
 * is pure _delay_us().
 *
 * WHY THE ORIGINAL DOES NOT NEED THIS. Its ADB routines contain no interrupt
 * control whatever (nothing in pmgr.asm 600-930 touches Int_Ctrl_Reg), because
 * by the time ADB runs almost nothing can interrupt it:
 *
 *   :186  ldm #0,Int_Ctrl_Reg      - everything off
 *   :220  seb Int2_Enb             - ONLY Int2 is ever enabled
 *   Int1_Enb is never set: the 60 Hz is POLLED, not vectored (PORT-AUDIT.md)
 *   Timer_1_Vec, Timer_2_Vec, Timer_X_Vec, Interrupt_1 are all bare `rti`
 *
 * So the only interrupt that can do anything mid-transaction is
 * Reset_Interrupt - and if the reset switch is pressed, a mangled ADB cell is
 * irrelevant. The original's ADB is uninterruptible in practice.
 *
 * Ours is not: PORTC_PORT_vect (SIXTYHZ, 16.6 ms) and RTC_PIT_vect (15.6 ms)
 * are both live, and a transaction is 1.7-4 ms, so a good fraction of them
 * would take a hit.
 *
 * NO TICK IS LOST. AVR interrupt flags LATCH: an edge arriving during the
 * blackout still sets PORTC.INTFLAGS / RTC.PITINTFLAGS and its ISR runs as
 * soon as interrupts come back. And a 4 ms blackout cannot span two 16.6 ms
 * SIXTYHZ edges, so at most one is pending - delayed, never dropped.
 *
 * The host bus is unaffected in a way the original does not also suffer: it is
 * polled, not vectored, and the original equally stops servicing CommandReceive
 * for the duration of its own ADB transaction.
 *
 * SREG is saved and restored rather than ending with sei(), so this is safe to
 * call from a context that already had interrupts off - the reset and wake
 * paths both run with them disabled. */
bool pmgr_adb_transact(uint8_t cmd,
                       const uint8_t *tx, uint8_t txlen,
                       uint8_t *rx, uint8_t *rxlen,
                       uint8_t *status)
{
    const uint8_t sreg = SREG;
    cli();
    const bool ok = adb_transact_locked(cmd, tx, txlen, rx, rxlen, status);
#ifdef PMGR_ADB_HOST_SERVICE
    adb_host_service();                         /* after the transaction */
#endif
    SREG = sreg;
    return ok;
}

/* ==========================================================================
 * Queue / service / read - the asynchronous spine.
 *
 * Zero-page layout the original uses, and the reason the $28 reply looks the
 * way it does (pmgr.asm 7-18):
 *
 *      $5  LastADBCMD
 *      $6  ADBStatus
 *      $7  ADBCMDLength
 *      $8.. ADB data buffer
 *
 * readADB sets WriteLocation = $0005 and ByteCount = ADBCMDLength + 3, then
 * ReturnDataToHost2 walks UPWARD from $5. The three header bytes are not a
 * designed header - they are simply the three zero-page cells that happen to
 * sit below the buffer. Reproduce the order exactly.
 * ========================================================================== */

static uint8_t s_last_cmd;      /* $5  */
static uint8_t s_status;        /* $6  */
static uint8_t s_cmdlength;     /* $7  - TX length going in, RX count coming out */
static uint8_t s_buf[ADB_RX_MAX];
static uint8_t s_pending_len;   /* $3  - queued TX length, reloaded at service */
static bool    s_newcmd;

void pmgr_adb_queue(uint8_t adb_cmd, uint8_t status,
                    const uint8_t *tx, uint8_t txlen)
{
    s_last_cmd = adb_cmd;                       /* lda $13 / sta LastADBCMD */
    s_status   = status;                        /* lda $14 / sta ADBStatus  */
    s_status  |= ADB_ST_NEWCMD;                 /* seb adb_newcmd           */
    s_newcmd   = true;

    if (txlen > ADB_RX_MAX) txlen = ADB_RX_MAX;
    for (uint8_t i = 0; i < txlen; i++) s_buf[i] = tx[i];

    s_pending_len = txlen;                      /* sty $3            */
    s_cmdlength   = txlen;                      /* sty ADBCMDLength  */
}

void pmgr_adb_off(void)
{
    /* ⭐ ZERO - VERIFIED IN THE BINARY, 2026-08-30.
     *     EEA9  3C 00 06   ldm #$00,ADBStatus
     * ⛔ This was ADB_ST_INIT (1). The historical listing said `#1`; the binary
     * says `#0`, so $21 clears ADBStatus COMPLETELY - including adb_init, which
     * means the bus must re-initialise. Leaving bit 0 set skipped that. */
    s_status = 0u;                              /* EEA9  ldm #0,ADBStatus */
    s_newcmd = false;
}

/* ResetEntry, pmgr.asm 154-156:  lda #0 / sta $0 / sta ADBStatus
 *
 * A FULL clear to zero - distinct from pmgr_adb_off(), which sets ADBStatus to
 * 1 (`ldm #1,ADBStatus`, :1010), and from pmgr_adb_wake_reset(), which keeps
 * adb_autopoll. Three different resets of the same byte, each with its own
 * call site; do not collapse them. */
/* ADBStatus ($6) as the virtual memory map sees it - read-only. */
uint8_t pmgr_adb_status(void) { return s_status; }

void pmgr_adb_status_clear(void)
{
    s_status = 0;
    s_newcmd = false;
}

/* Wake path, pmgr.asm 1381-1383:
 *      lda ADBStatus / and #4 / sta ADBStatus
 *
 * Only adb_autopoll survives sleep. Note this is NARROWER than the mask
 * ADBCMDDo applies between transactions (#%101, which also keeps adb_init) -
 * waking deliberately drops adb_init so the bus is re-initialised. */
void pmgr_adb_wake_reset(void)
{
    s_status &= ADB_ST_AUTOPOLL;
    s_newcmd = false;
}

bool pmgr_adb_pending(void)
{
    return s_newcmd || (s_status & ADB_ST_AUTOPOLL) != 0u;
}

bool pmgr_adb_service(void)
{
#ifdef PMGR_ADB_HOST_SERVICE
    /* Fix A: if the host is already sending, take its command byte and do NO ADB work this tick - the queued
     * command or autopoll stays pending (nothing below has run) and goes out on the next tick. */
    adb_host_service();
    if (pmgr_bus_has_latched()) return false;
#endif
    /* lda ADBStatus / and #%101 - only adb_init and adb_autopoll survive. */
    s_status &= ADB_ST_PRESERVED;
    s_newcmd  = false;

    /* lda $3 / sta ADBCMDLength - restore the queued TX length, which
     * ADBCMDDo consumes and ADBCMDDo2 then overwrites with the RX count. */
    s_cmdlength = s_pending_len;

    /* lda LastADBCMD / and #$F / bne .ADBNotReset - a command whose low
     * nibble is zero is SendReset: hold the bus low ~4.6 ms (three
     * DelayLoop(0) calls, 256 iterations each) and do not transact. */
    if ((s_last_cmd & 0x0Fu) == 0u) {
        ADB_ASSERT();
#ifdef PMGR_ADB_HOST_SERVICE
        /* The reset only needs >= 3 ms low; hold it in 200 us steps (design §9). Fix A (§10): look at the host only
         * once 3.0 ms have passed (15 steps), and end the hold as soon as a command byte is latched, so the rest
         * of its transaction is served within ~200 us. Otherwise 23 steps, >= 4.6 ms as the original. */
        for (uint8_t i = 0; i < 23u; i++) {
            _delay_us(200);
            if (i >= 14u) {
                adb_host_service();
                if (pmgr_bus_has_latched()) break;
            }
        }
#else
        _delay_ms(4.638);
#endif
        ADB_RELEASE();
        s_status |= ADB_ST_NOREPLY;
        return true;                            /* bra .ADBFinish */
    }

    uint8_t rxlen = 0;
    bool tx_ok = pmgr_adb_transact(s_last_cmd, s_buf, s_cmdlength,
                                   s_buf, &rxlen, &s_status);

    if (!tx_ok) {
        /* Distinguish the two failure points the original keeps separate.
         * ADBCMDDo failing (contention/sync) -> seb adb_noreply, .ADBFinish.
         * ADBCMDDo2 failing -> the autopoll suppression below. */
        if (!(s_status & ADB_ST_NOREPLY)) {
            s_status |= ADB_ST_NOREPLY;         /* pmgr.asm 282 */
            return true;
        }

        /* ADBCMDDo2 failed. THE ONLY PATH THAT SUPPRESSES ADBInt:
         *     bbc adb_autopoll,ADBStatus,.ADBFinish
         *     bbc adb_srq,ADBStatus,.noADBAction
         * An autopoll that came back empty with no service request is the
         * normal idle case - no key, no mouse. Raising ADBInt here would
         * interrupt the host 60 times a second forever. */
        if ((s_status & ADB_ST_AUTOPOLL) && !(s_status & ADB_ST_SRQ))
            return false;

        return true;
    }

    s_cmdlength = rxlen;                        /* ADBCMDLength = RX count */
    return true;
}

uint8_t pmgr_adb_reply(uint8_t *buf)
{
    uint8_t n = s_cmdlength;
    if (n > ADB_RX_MAX) n = ADB_RX_MAX;

    buf[0] = s_last_cmd;      /* $5 */
    buf[1] = s_status;        /* $6 */
    buf[2] = s_cmdlength;     /* $7 */
    for (uint8_t i = 0; i < n; i++) buf[3 + i] = s_buf[i];

    return (uint8_t)(3u + n);                   /* adc #3 */
}
