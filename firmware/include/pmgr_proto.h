/* PMGR packet layer and command dispatch. See PROTOCOL-SPEC.md sections 3-4. */
#ifndef PMGR_PROTO_H
#define PMGR_PROTO_H

#include <stdint.h>
#include <stdbool.h>

/* The original buffers payload from $13 upward in zero page. 32 is ample for
 * every command in the dispatch table; oversized lengths are rejected. */
#define PMGR_MAX_PAYLOAD  32

typedef struct {
    uint8_t command;
    uint8_t length;
    uint8_t data[PMGR_MAX_PAYLOAD];
} pmgr_packet_t;

/* Command encoding:  [7:4] group   [3] read flag   [2:0] subcommand */
#define PMGR_CMD_GROUP(c)   (((c) & 0xF0u) >> 4)
#define PMGR_CMD_ISREAD(c)  (((c) & 0x08u) != 0u)   /* readBit = 3 */
#define PMGR_CMD_SUB(c)     ((c) & 0x07u)

/* Special-cased ahead of the table lookup. */
#define PMGR_CMD_READ_INT   0x78u
#define PMGR_CMD_READ_ADB   0x28u

/* IntFlags ($01). The host is interrupted when any of the LOW THREE bits is
 * set - pmgr.asm 292-296:  lda IntFlags / and #%111 / beq .noInterrupts
 *
 * Each bit is cleared by whichever handler services it, NOT by readINT:
 *   bit 0  ADBInt   - set on ADB completion/SRQ (290), cleared 1001/1012/1017
 *   bit 1  battery  - set on battery_low change and charger_changed (416/450/454)
 *   bit 2  battery  - cleared with bit 1 by Battery_Command (1286-1287);
 *                     nothing in the listing sets it - purpose unknown
 *   bit 3            - cleared by readINT itself (1299), outside the %111 mask
 */
#define PMGR_INT_ADB_bm      (1u << 0)
#define PMGR_INT_BATTERY_bm  (1u << 1)
#define PMGR_INT_BAT2_bm     (1u << 2)
#define PMGR_INT_READ_bm     (1u << 3)
#define PMGR_INT_MASK        0x07u

void pmgr_proto_init(void);

bool pmgr_receive_command(pmgr_packet_t *pkt);
bool pmgr_return_data(uint8_t command, const uint8_t *data, uint8_t length);

void pmgr_dispatch(pmgr_packet_t *pkt);

/* Raise an interrupt condition; the poll loop pulses PMINT when set. */
/* The original's zero-page $0 bit 1: "PMINT has been pulsed at the host and
 * readINT has not yet acknowledged it". It is an INTERLOCK - while it is set,
 * neither the ADB handler (pmgr.asm 1000) nor the battery handler (1285) may
 * clear its IntFlags bits, because the host has not yet seen them.
 *   set   296  seb 1,$0   when PMINT is pulsed
 *   clear 1300 clb 1,$0   by readINT
 * Other bits of $0 (2, 5, 6) are used by the sleep/wake paths and are NOT
 * modelled here. */
bool pmgr_int_pending(void);

/* pmgr.asm 291-297. Pulses PMINT* if any of IntFlags bits 0-2 is set, and does
 * so EVERY time it is called - Apple has no guard, and the repetition is the
 * host's retry. Call from the 60 Hz tick, which is the cadence at which the
 * original reaches .noADBAction. */
void pmgr_int_service(void);

/* ========== KNOWN DIVERGENCE: payload reception is BUFFERED, not handler-driven
 *
 * Apple does NOT read the payload as part of receiving a command. `ReceiveCommand`
 * (pmgr.asm) fetches exactly TWO bytes - CommandByte and ByteCount - and returns.
 * Each handler then calls `WriteToLocation` itself, and SEVEN OF THE NINE call
 * sites OVERWRITE ByteCount with a constant first:
 *
 *     .xPramWrite / .xPramRead   ldm #2,ByteCount
 *     .SetContrastReceive        ldm #1,ByteCount
 *     modem write                ldm #1,ByteCount
 *     sound write                ldm #1,ByteCount
 *     Sleep ($7F)                ldm #4,ByteCount
 *     Power ($1x), .pMgrADB, $Ex use the host's count
 *
 * So for most commands the ORIGINAL IGNORES THE HOST'S COUNT and reads a fixed
 * number of bytes chosen by the handler. We read the host's count up front,
 * buffer it, and dispatch.
 *
 * WHEN THE TWO AGREE: whenever the host sends the count the handler expects,
 * which is what the Mac ROM does. Byte-for-byte identical on the wire - each
 * payload byte is the same PMREQ/PMACK exchange either way.
 *
 * WHEN THEY DIVERGE - only on a count the handler did not expect:
 *
 *   host count > handler's constant   Apple reads its constant and leaves the
 *                                     rest on the bus; its NEXT ReceiveCommand
 *                                     then interprets a leftover PAYLOAD byte
 *                                     as a command. The stream mis-frames.
 *                                     We consume the whole payload and stay in
 *                                     sync.
 *   host count < handler's constant   Apple blocks in PM_ReceiveByte_Wait for
 *                                     bytes that never arrive, times out, and
 *                                     fails. We dispatch with a short payload.
 *
 * ⭐ DELIBERATELY NOT REPRODUCED. Restructuring to handler-driven reception is
 * a large change to the one path every command flows through, and it would buy
 * fidelity ONLY on inputs the Mac ROM does not generate - in exchange for
 * reproducing a stream-desync bug. The current behaviour is a superset: correct
 * where Apple is correct, and merely more robust where Apple mis-frames.
 *
 * ⚠️ IF BRING-UP EVER SHOWS THE COMMAND STREAM LOSING SYNC, THIS IS THE FIRST
 * PLACE TO LOOK - a host that relies on Apple's mis-framing (unlikely, but it
 * is observable behaviour) would not be reproduced here. The table above says
 * exactly how many bytes each handler should have consumed.
 * ======================================================================== */

void pmgr_set_int_flag(uint8_t mask);
uint8_t pmgr_int_flags(void);
void pmgr_clear_int_flags(uint8_t mask);
void pmgr_ack_int(void);

void pmgr_poll(void);

#endif /* PMGR_PROTO_H */
