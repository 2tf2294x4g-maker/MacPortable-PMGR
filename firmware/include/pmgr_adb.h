/* ADB transaction engine.
 *
 * Transmit is a literal translation of ADBCMDDo (pmgr.asm ~614-730): same
 * frame structure, same contention checks, same intervals.
 *
 * Receive is a SEMANTIC translation of ADBCMDDo2 (pmgr.asm ~731-918). The
 * original is a ~130-instruction unrolled bbs/bbc ladder - a 1 MHz 6502 using
 * instruction cycles as a ruler. Reproducing it on a 24 MHz AVR would copy the
 * ruler, not the measurement. Here TCB0 measures each bus-low pulse in
 * hardware and the ladder's decision thresholds are applied to the result.
 * The assembly remains the authoritative state-machine specification: every
 * branch it can take has a counterpart below, cited by label.
 *
 * ⛔⛔ ELECTRICAL CONTRACT - THIS COMMENT WAS WRONG UNTIL 2026-09-06.
 *
 * It previously said PD1 is a "PUSH-PULL output... HIGH asserts... It is NOT
 * open-drain at the AVR pin", citing ADB-TIMING.md section 3b. Section 3b is
 * itself marked OBSOLETE, superseded by 3a/3a-bis, and pmgr_adb.c has always
 * implemented the opposite. A header contradicting its own implementation and
 * citing a section marked obsolete is worse than no comment: it was read,
 * believed, and used as the basis of a measurement entry (2.58) before anyone
 * opened the .c file.
 *
 * WHAT THE CODE ACTUALLY DOES - and it is correct:
 *
 *     ADB_ASSERT()   DIRCLR -> Hi-Z. R103 (10k to +5V) raises Q12's base,
 *                    Q12 saturates, the ADB bus is pulled LOW.
 *     ADB_RELEASE()  OUTCLR then DIRSET -> drive LOW. Q12's base is sunk to
 *                    ~0 V, Q12 turns off, the bus rises via its pull-up.
 *
 * ⭐ Asserting is the PASSIVE state. The AVR only ever SINKS - about 0.5 mA
 * through R103 - and never sources into the base.
 *
 * ⛔ PD1 MUST NEVER BE DRIVEN HIGH. J21-29 reaches Q12's base with no
 * kilohm-scale series resistor (MEASUREMENTS.md 2.58, diode test 0.7 V), so a
 * push-pull HIGH would source current limited only by the AVR's output
 * impedance into a clamped node. ⚠️ That impedance is UNMEASURED, so specific
 * milliamp figures (100/120/170 mA appear in older revisions) are not
 * supported. What is: it far exceeds the 20 mA at which output levels are
 * specified, and risks the +/-50 mA absolute maximum. An earlier revision of
 * pmgr_adb.c did exactly that.
 *
 * PD2 / FDB reads the bus directly, true polarity, internal pull-up DISABLED.
 */
#ifndef PMGR_ADB_H
#define PMGR_ADB_H

#include <stdint.h>
#include <stdbool.h>

/* ADBStatus ($6). Bit numbers are Apple's - pmgr.asm 10-16. */
#define ADB_ST_INIT         0x01u   /* bit 0 - adb_init            */
#define ADB_ST_NEWCMD       0x02u   /* bit 1 - adb_newcmd          */
#define ADB_ST_AUTOPOLL     0x04u   /* bit 2 - adb_autopoll        */
#define ADB_ST_SRQ          0x08u   /* bit 3 - adb_srq             */
#define ADB_ST_NOREPLY      0x10u   /* bit 4 - adb_noreply         */
#define ADB_ST_CONTENTION   0x20u   /* bit 5 - adb_buscontention   */
#define ADB_ST_ERROR        0x80u   /* bit 7 - adb_error           */

/* ADBCMDDo preserves only bits 0 and 2 across a transaction:
 *     lda ADBStatus / and #%101 / sta ADBStatus        (pmgr.asm 615-617) */
#define ADB_ST_PRESERVED    (ADB_ST_INIT | ADB_ST_AUTOPOLL)

#define ADB_RX_MAX          8u      /* ADBCMDDo2 stacks at most 8 bytes */

void pmgr_adb_init(void);

/* Run one full ADB transaction: attention, sync, command byte, optional data,
 * then listen for a device reply.
 *
 * cmd      - the ADB command byte (LastADBCMD, $5)
 * tx/txlen - bytes to transmit after the command (0 for a plain poll)
 * rx       - receive buffer, at least ADB_RX_MAX
 * rxlen    - out: bytes actually received
 * status   - out: ADBStatus bits for this transaction
 *
 * Returns false on any error (mirrors the original's sec/clc return).
 *
 * ⭐ ON FAILURE OF THE RECEIVE PHASE, ADB_ST_ERROR AND ADB_ST_NOREPLY ARE SET
 * TOGETHER, ALWAYS. The original's ADBCMDDo2 has a single `.fail` that does
 * `seb adb_error` then `seb adb_noreply`, reached by every failure - silent
 * bus, malformed start bit, truncated byte, over-long low. The two bits do NOT
 * distinguish causes and never did.
 *
 * ⚠️ So do not read NOREPLY as "absent device" and ERROR as "bus problem".
 * An earlier version of this header said a NOREPLY without ERROR was the
 * normal absent-device outcome; that was wrong, and the code matched the wrong
 * description. An absent device sets both, because the start-bit timeout falls
 * through to the same .fail as everything else.
 *
 * A failure of the SEND phase is different: `.fail` in ADBCMDDo sets
 * adb_buscontention + adb_error, and no adb_noreply - nothing was ever
 * listened for.
 */
bool pmgr_adb_transact(uint8_t cmd,
                       const uint8_t *tx, uint8_t txlen,
                       uint8_t *rx, uint8_t *rxlen,
                       uint8_t *status);

/* ---- Queue / service / read -----------------------------------------------
 * ADB is ASYNCHRONOUS in the original and must be here too. $2x does not
 * transact; it queues. The transaction runs later from the 60 Hz service
 * point, and only then is ADBInt raised so the host knows to issue $28.
 *
 *   host $2x  -> pmgr_adb_queue()     ADB_Command .pMgrADB, pmgr.asm 1015-1035
 *   60 Hz     -> pmgr_adb_service()   .ADBAction,           pmgr.asm 260-292
 *   host $78  -> readINT reports the ADB bit
 *   host $28  -> pmgr_adb_reply()     readADB,              pmgr.asm 991-1002
 * -------------------------------------------------------------------------- */

/* $2x subcommands. NOT from pmgr.asm - cmd_pMgrADB and cmd_pMgrADBoff are
 * referenced there but never defined, so the disassembly cannot supply their
 * values. Taken from the PMU opcode numbering that carried forward into the
 * later PowerBooks (Linux pmu.h, PMU_ADB_CMD 0x20). Class D - unconfirmed.
 * Failure mode if 0x21 is wrong is contained: "ADB off" falls through to
 * .bad_command and returns sec, exactly as an unknown subcommand should. */
#define ADB_CMD_PMGRADB     0x20u
#define ADB_CMD_PMGRADBOFF  0x21u

/* Queue a transaction. Clears ADBInt and sets adb_newcmd; does not transact. */
void pmgr_adb_queue(uint8_t adb_cmd, uint8_t status,
                    const uint8_t *tx, uint8_t txlen);

/* .pMgrADBoff: ldm #1,ADBStatus / clb ADBInt,IntFlags (pmgr.asm 1010-1013). */
void pmgr_adb_off(void);

/* Wake path: ADBStatus &= adb_autopoll only (pmgr.asm 1381-1383). Narrower
 * than ADB_ST_PRESERVED - waking drops adb_init so the bus re-initialises. */
void pmgr_adb_wake_reset(void);

/* ResetEntry: ADBStatus = 0 outright (pmgr.asm 154-156). Note the three
 * resets of this byte are all DIFFERENT values, by design:
 *     pmgr_adb_off()         -> 1 (adb_init)        :1010
 *     pmgr_adb_wake_reset()  -> &= adb_autopoll     :1381-1383
 *     pmgr_adb_status_clear()-> 0                   :154-156          */
void pmgr_adb_status_clear(void);

/* ADBStatus ($6) for the $E8 virtual memory map. Read-only by design: the map
 * exposes state, it does not become a second way to mutate it. */
uint8_t pmgr_adb_status(void);

/* True when .ADBAction would run: adb_newcmd set, or autopoll enabled. */
bool pmgr_adb_pending(void);

/* Run one queued/autopoll transaction. Returns true if ADBInt must be raised.
 * Caller must not invoke this while ADBInt is already set - the original
 * guards with bbs ADBInt,IntFlags,.noADBAction (pmgr.asm 257). */
bool pmgr_adb_service(void);

/* Build the $28 reply. Returns the byte count (3 + ADBCMDLength); buf must
 * hold at least 3 + ADB_RX_MAX. */
uint8_t pmgr_adb_reply(uint8_t *buf);

#endif /* PMGR_ADB_H */
