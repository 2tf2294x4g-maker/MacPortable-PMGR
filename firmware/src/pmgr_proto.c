#include "pmgr_proto.h"
#include "pmgr_bus.h"
#include "pmgr_handlers.h"

static uint8_t s_int_flags;
static bool    s_int_pending;   /* $0 bit 1 */

void pmgr_proto_init(void) { s_int_flags = 0; s_int_pending = false; }

bool pmgr_int_pending(void) { return s_int_pending; }

void pmgr_set_int_flag(uint8_t mask)   { s_int_flags |= mask; }
uint8_t pmgr_int_flags(void)           { return s_int_flags; }
void pmgr_clear_int_flags(uint8_t mask){ s_int_flags &= (uint8_t)~mask; }

/* readINT acknowledges the interrupt (pmgr.asm 1300: clb 1,$0). */
void pmgr_ack_int(void) { s_int_pending = false; }

/* ⭐ pmgr.asm 291-297 .noADBAction - THE PMINT PULSE, and its RETRY CADENCE.
 *
 *     .noADBAction  lda IntFlags / and #%111 / beq .noInterrupts
 *                   clb PMINT,Port_P3
 *                   seb 1,$0
 *                   seb PMINT,Port_P3
 *
 * ⚠️ THERE IS NO GUARD. `seb 1,$0` is SET here but never TESTED here, so the
 * pulse repeats every time this point is reached while any flag remains set.
 * That is the host's RETRY: miss an edge and another one follows.
 *
 * ⭐ HOW OFTEN IS "every time"? Not per command, and not per spin iteration -
 * both readings are wrong and one of them was in this file:
 *
 *   - after dispatching a command Apple does `bra CommandReceive`, straight to
 *     the TOP of the loop. It never passes through .noADBAction.
 *   - the PMREQ spin at .LAB_E913 does not reach it either.
 *   - .noADBAction is reached only through the ADB service path, which runs
 *     from the 60 Hz tick (and from the spin when adb_newcmd is set).
 *
 * So the real cadence is ~60 Hz. Hence this lives on the tick.
 *
 * ⛔ WHAT WAS WRONG BEFORE: pmgr_poll() pulsed once and latched on
 * s_int_pending until readINT cleared it. The old comment justified that by
 * saying our loop runs far faster than Apple's and would "hammer the VIA" -
 * true of pmgr_poll(), but the conclusion should have been "pulse at Apple's
 * rate", not "pulse once". A host that missed the single edge waited forever;
 * against a real PMGR it would have got another 16.7 ms later. */
void pmgr_int_service(void)
{
    if ((s_int_flags & PMGR_INT_MASK) == 0u) return;   /* beq .noInterrupts */
    pmgr_bus_interrupt_host();                         /* clb/seb PMINT     */
    s_int_pending = true;                              /* seb 1,$0 - SET,   */
                                                       /* deliberately not  */
                                                       /* used as a gate    */
}

/* Host -> PMGR:  command, length, data */
bool pmgr_receive_command(pmgr_packet_t *pkt)
{
#ifdef PMGR_ADB_HOST_SERVICE
    /* A command byte already handshaken during ADB work (pmgr_bus.h). */
    if (!pmgr_bus_take_latched(&pkt->command))
#endif
    if (!pmgr_bus_receive(&pkt->command)) return false;
    if (!pmgr_bus_receive(&pkt->length))  return false;

    if (pkt->length > PMGR_MAX_PAYLOAD) {
        /* Drain so the bus is not left mid-packet, then reject. */
        for (uint8_t i = 0; i < pkt->length; i++) {
            uint8_t junk;
            if (!pmgr_bus_receive(&junk)) return false;
        }
        return false;
    }

    for (uint8_t i = 0; i < pkt->length; i++) {
        if (!pmgr_bus_receive(&pkt->data[i])) return false;
    }
    return true;
}

/* PMGR -> host: command echoed, length, data */
bool pmgr_return_data(uint8_t command, const uint8_t *data, uint8_t length)
{
    if (!pmgr_bus_send(command)) return false;
    if (!pmgr_bus_send(length))  return false;
    for (uint8_t i = 0; i < length; i++) {
        if (!pmgr_bus_send(data[i])) return false;
    }
    return true;
}

/* 16-entry group table. The original indexes a WORD table with (cmd & $F0) >> 3;
 * that shift produces a byte offset into 2-byte entries, which is the same
 * group number we get here with >> 4. */
typedef void (*pmgr_handler_t)(pmgr_packet_t *);

static const pmgr_handler_t CMDTable[16] = {
    pmgr_cmd_invalid,        /* 0  $0x */
    pmgr_cmd_power,          /* 1  $1x */
    pmgr_cmd_adb,            /* 2  $2x */
    pmgr_cmd_time_pram,      /* 3  $3x */
    pmgr_cmd_contrast,       /* 4  $4x */
    pmgr_cmd_modem,          /* 5  $5x */
    pmgr_cmd_battery,        /* 6  $6x */
    pmgr_cmd_sleep,          /* 7  $7x */
    pmgr_cmd_timer,          /* 8  $8x */
    pmgr_cmd_sound,          /* 9  $9x */
    pmgr_cmd_invalid,        /* A */
    pmgr_cmd_invalid,        /* B */
    pmgr_cmd_invalid,        /* C */
    pmgr_cmd_invalid,        /* D */
    pmgr_cmd_pmgr,           /* E  $Ex */
    pmgr_cmd_invalid,        /* F */
};

void pmgr_dispatch(pmgr_packet_t *pkt)
{
    /* Two commands are handled before the table (spec section 4). */
    if (pkt->command == PMGR_CMD_READ_INT) { pmgr_cmd_read_int(pkt); return; }
    if (pkt->command == PMGR_CMD_READ_ADB) { pmgr_cmd_read_adb(pkt); return; }

    CMDTable[PMGR_CMD_GROUP(pkt->command)](pkt);
}

void pmgr_poll(void)
{
#ifdef PMGR_ADB_HOST_SERVICE
    if (pmgr_bus_has_latched() || pmgr_bus_req_asserted()) {
#else
    if (pmgr_bus_req_asserted()) {
#endif
        pmgr_packet_t pkt;
        if (pmgr_receive_command(&pkt))
            pmgr_dispatch(&pkt);
        /* On failure we simply return; the original reports no error either. */
    }

    /* ⛔ THE PMINT PULSE IS NOT HERE ANY MORE - see pmgr_int_service(),
     * called from the 60 Hz tick. This function used to pulse once and latch
     * until readINT, which suppressed Apple's retry. */
}
