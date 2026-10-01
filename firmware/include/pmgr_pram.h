/* PRAM / xPRAM storage and the real-time counter.
 *
 * MEMORY MODEL — from Time_PRAM_Command and FUN_EF84 (pmgr.asm 1040-1145):
 *
 *   $2F..$AE   xPRAM, 128 bytes
 *   $AF        checksum over those 128 bytes
 *   $37..$3A   4 bytes  ) both INSIDE xPRAM - "classic" PRAM is a window
 *   $3F..$4E   16 bytes ) into the same 128 bytes, not separate storage
 *
 * xPram addressing: the host sends a location, and the original does
 * `adc #$2F` to reach the byte. Since xPRAM starts at $2F, the host's
 * location IS the array index. No adjustment needed here.
 *
 * TIME — 4 bytes, and the byte order matters twice over:
 *   - In memory the original has timeD at the LOWEST address, then timeC,
 *     timeB, timeA. `timeRead` returns 4 bytes ascending from timeD, so the
 *     wire order is timeD first = MSB first = big-endian, which is what a
 *     68000 Mac expects.
 *   - The 1 Hz tick does `inc timeA` first with carry upward, so timeA is the
 *     LSB. Held here as a uint32_t and serialised MSB-first.
 *
 * `CombineTime` is not combining anything - it is `0 + timeD + timeC + timeB
 * + timeA`, a checksum byte stored in `Time`.
 *
 * PERSISTENCE - three different lifetimes, deliberately:
 *
 *   PRAM   EEPROM, saved IN THE BACKGROUND after a change (two slots, never
 *          blocks the bus - see pmgr_pram.c and MEASUREMENTS 2.155/2.156).
 *          Survives everything once durable.
 *   TIME   .noinit RAM + checksum. Survives a RESET, not a power loss.
 *   -      EEPROM for the time would exhaust a 100k-cycle part in ~28 hours
 *          at 1 Hz. Not an option, and not what the original did.
 *
 * The time model is the original's, not a compromise. Its time is plain RAM in
 * a chip that never loses power (J21 pins 1/2 = VCC ALWAYS ON), so a reset
 * leaves it intact - which is exactly why pmgr.asm VALIDATES instead of
 * reinitialising at startup (:157-159, `jsr CombineTime / cmp Time`), and
 * refreshes the checksum on every 1 Hz tick (:594-595).
 *
 * We reproduce that with .noinit, which the C runtime does not clear. After a
 * reset the checksum agrees and the clock is kept; after a power loss .noinit
 * is garbage, the checksum disagrees, and the time is zeroed - the same
 * "Mac forgot the date" behaviour a machine shows with a dead PRAM battery.
 *
 * Retaining time across POWER LOSS would need an RTC or a supercap. That is a
 * genuine enhancement over the original, not a fidelity fix.
 *
 * ⛔⛔ THE MIRROR-IMAGE DIVERGENCE, AND IT IS THE ONE PEOPLE WILL HIT. The
 * ORIGINAL loses PRAM when ALL power is removed, which is the classic "pull the
 * battery to zap PRAM" repair. EEPROM does not, so on this board that fix
 * SILENTLY DOES NOTHING. There is no PRAM-clear command here either - only the
 * ordinary $3x byte writes - so the recovery paths are:
 *
 *     OS-level zap (the ROM writes defaults through us)   works
 *     removing all power                                  NO EFFECT
 *     a genuinely stuck value                             UPDI EEPROM erase
 *
 * ⚠️ That makes a bad PRAM value a PROGRAMMER-level problem rather than a
 * screwdriver one, at bring-up as much as in service. Adding a host-command
 * PRAM reset would close it and is firmware-only.
 */
#ifndef PMGR_PRAM_H
#define PMGR_PRAM_H

#include <stdint.h>
#include <stdbool.h>

#define PRAM_SIZE       128u        /* $2F..$AE */
#define PRAM_OFF_37     8u          /* $37 - $2F */
#define PRAM_OFF_3F     16u         /* $3F - $2F */
#define PRAM_LEN_37     4u
#define PRAM_LEN_3F     16u

/* $3x subcommands. `cmd_timeWrite` etc. are referenced but never defined in
 * pmgr.asm, so these come from the PMU numbering that carried into the later
 * PowerBooks. $3A is CONFIRMED by measurement: a captured boot exchange read
 * `3A 02 46 01` -> `3A 01 00`, an xPRAM read of location $46 (see
 * MEASUREMENTS.md 2.9). The others remain Class D. */
#define CMD_TIME_WRITE  0x30u
#define CMD_PRAM_WRITE  0x31u
#define CMD_XPRAM_WRITE 0x32u       /* anything else in the write path */
#define CMD_TIME_READ   0x38u
#define CMD_PRAM_READ   0x39u
#define CMD_XPRAM_READ  0x3Au       /* anything else in the read path */

void     pmgr_pram_init(void);      /* load from EEPROM, validate checksum */

/* pmgr.asm 141-148 InitRAM - zero all 128 bytes and recommit the checksum.
 * Only for the bad-checksum recovery in pmgr_entry_state(). */
void     pmgr_pram_zero(void);

uint8_t  pmgr_pram_read(uint8_t idx);
void     pmgr_pram_write(uint8_t idx, uint8_t v);
void     pmgr_pram_commit(void);    /* recompute checksum; schedules a save, returns at once */

/* Background persistence (MEASUREMENTS 2.156).
 *   service  - call from the main loop; at most one EEPROM byte per call, never waits
 *   pending  - true until the newest change is durable
 *   flush    - finish synchronously. ⛔ BLOCKS for ~10 ms per byte: only where
 *              the host is held (sleep entry, PMGR_RESET* restart). Bounded at 5 s;
 *              callers proceed either way (failure policy in pmgr_pram.c) */
void     pmgr_pram_service(void);
bool     pmgr_pram_save_pending(void);
bool     pmgr_pram_flush(void);       /* true = durable; false = gave up (bounded, 2.184) */
uint16_t pmgr_pram_save_count(void);  /* completed saves since init */
bool     pmgr_pram_save_failed(void); /* latched after 3 failed read-backs in a row, a flush
                                       * timeout, or a stuck NVM controller at init */

/* FUN_EF84. Returns the 8-bit sum of all 128 bytes. Sets *valid false when the
 * original would return CARRY SET - checksum mismatch, OR every byte zero
 * (blank PRAM sums to 0 and would otherwise pass against a stored 0). */
uint8_t  pmgr_pram_checksum(bool *valid);

/* Time. Serialised MSB-first on the wire. */
uint32_t pmgr_time_get(void);
void     pmgr_time_set(uint32_t t);
void     pmgr_time_tick(void);      /* call once per second */
uint8_t  pmgr_time_combine(void);   /* CombineTime -> the `Time` checksum byte */

#endif /* PMGR_PRAM_H */
