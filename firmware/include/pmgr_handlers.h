/* Command group handlers. One per CMDTable entry, plus the two special cases. */
#ifndef PMGR_HANDLERS_H
#define PMGR_HANDLERS_H

#include "pmgr_proto.h"

/* COLD entry only (main()): clears PowerFlags, battery levels and the second
 * divider, then runs InitPorts. */
void pmgr_handlers_init(void);

/* InitPorts - pmgr.asm 167-184. Port directions and initial levels, no state
 * cleared.
 *
 * `cold` selects Apple's TWO DIFFERENT Port_P0 values (:167-172): a cold entry
 * forces everything off (%10011111); the reset switch PRESERVES HD_PWR* and
 * enables MINUS5_EN. Passing true on both paths powers the hard disk down on
 * every reset-switch press, which the original avoids. */
void pmgr_ports_init(bool cold);

/* InitPorts with Port_P0 = $00 - the $EF soft-reset entry. Those rails are
 * ACTIVE LOW, so this turns everything ON. Traced from ReturnDataToHost2's exit
 * state; see pmgr_cmd_pmgr(). */
void pmgr_ports_init_softreset(void);

/* ===== VIRTUAL M50753 ADDRESS SPACE - the $E0/$E8 backing ==================
 *
 * `.LAB_F200` reaches RAM, I/O and the program ROM at $E800-$FFFF through a
 * real 16-bit pointer, so these take a full 16-bit address.
 *
 * ⛔ NOT AVR SRAM. Addresses are M50753 addresses; the map translates to
 * whatever holds that state on our side. An unmapped address reads 0 and
 * ignores writes.
 *
 * ⭐ WRITES INTO $E800-$FFFF ARE IGNORED AND COMPLETE NORMALLY - the original
 * executes the store, real ROM does not change, and the command still returns
 * success. Do not turn that into an error. */
uint8_t pmgr_mem_read(uint16_t addr);
void    pmgr_mem_write(uint16_t addr, uint8_t value);

/* Entry2 - pmgr.asm 138-166. ⭐ SHARED BY BOTH ENTRY PATHS.
 *
 * `ColdEntry` (Y=8) and `ResetEntry` (Y=0) both `bra Entry2`, so this runs on
 * EVERY entry including a cold hardware reset. The Y value is consumed later,
 * at :167-172, and selects nothing here - only the Port_P0 value.
 *
 * ⛔ IT WAS PREVIOUSLY NAMED pmgr_reset_entry_state() AND CALLED ONLY FROM THE
 * RESET-SWITCH PATH. main() never ran it, so a cold boot with a corrupt-but-
 * present PRAM loaded the corruption and kept it - the checksum was never even
 * tested. Fixed 2026-08-28.
 *
 * On a bad checksum: zeroes PRAM and recommits it, and clears PowerFlags.
 * Always: clears ADBStatus. Never touches BatteryLevel or ref5V_Level, which
 * sit outside the zeroed block - PF_STAY_ASLEEP survives a reset by design.
 *
 * Requires pmgr_pram_init() to have run (it checksums what that loaded). */
void pmgr_entry_state(void);

/* ===================== INTERRUPT-STATE CONTRACT ==========================
 *
 * The two startup paths - pmgr_system_release() and pmgr_sleep() - BOTH leave
 * the interrupt state exactly as they found it. Neither enables interrupts as
 * a side effect. The CALLER owns that decision and performs the single sei().
 *
 * WHY NOT "both return with interrupts enabled". The original can simply
 * enable at the end of each path (`cli` at :221 and :1409 - 6502 CLI ENABLES)
 * because each has one caller and one entry state. Ours do not:
 *
 *     pmgr_system_release()  <- main() only, interrupts OFF
 *                            <- pmgr_reset_switch_service(), which disables
 *                               them itself to match Reset_Interrupt
 *     pmgr_sleep()           <- main() cold park, interrupts OFF
 *                            <- pmgr_cmd_sleep() from the poll loop, ON
 *
 * A forced sei() would be wrong for at least one caller in each case. Saving
 * and restoring SREG is right for all of them, and is the same pattern used by
 * pmgr_adb_transact().
 *
 * Both functions still run their internals with interrupts DISABLED, because
 * both service the 60 Hz tick by hand and a live ISR would double-count it.
 * ======================================================================== */

/* Releases KBD_RST* then SYS_RST*. Until this is called the 68000 is held in
 * reset. Call it LAST, once the PMGR can actually service commands.
 *
 * ⚠️ Do NOT call this unconditionally at startup. A cold power-on may have to
 * PARK instead - see pmgr_cold_should_park().
 *
 * Interrupts: must ALREADY be disabled on entry (it polls SIXTYHZ directly,
 * exactly as the original does because its cli comes later, at :221). Leaves
 * them disabled. */
/* pmgr.asm 190-203 - contrast validate/set, PWM on, batteryNow, KBD_RST
 * released. Runs on BOTH branches, BEFORE pmgr_cold_should_park(), because
 * that is where the original's dispatch sits (:204-207). Skipping it on the
 * park branch leaves the contrast unvalidated and the display comes up at a
 * random brightness after every cold boot. Interrupts must be off. */
void pmgr_pre_dispatch_init(void);

void pmgr_system_release(void);

/* pmgr.asm 204-207. True when a cold entry must park in Sleep rather than
 * release the machine, i.e. VIA_TEST is set. Every hardware reset is a cold
 * entry (the RESET vector is `bra ColdEntry`, :112-113). */
bool pmgr_cold_should_park(void);

/* pmgr.asm 1329-1410. Parks the machine, waits for a wake event, and performs
 * the WAKE release sequence. Returns only once the machine is up again.
 *
 * The wake path is NOT the reset path: it ASSERTS KBD_RST* (:1395), waits two
 * ticks, then releases KBD_RST* and SYS_RST* ~5 us apart. pmgr_system_release()
 * instead RELEASES KBD_RST* first and waits ~30 ms. See MEASUREMENTS.md 2.5,
 * 2.5b, 2.10 and 2.11.
 *
 * Interrupts: disables them internally for the whole park and wake (it services
 * the tick by hand), then RESTORES the caller's state. Callable with them
 * either on or off. */
/* Tick edges between accepting the sleep command and asserting SYS_RST*.
 * MEASURED = 3 (MEASUREMENTS.md 2.27): three sleeps gave 37/44/45 ms, and K=3
 * is the only integer tick count whose one-tick-wide band contains all three.
 * K=2 (16.7-33.3 ms) and K=4 (50.0-66.7 ms) are both excluded.
 * Any future sleep measured outside 33.3-50.0 ms refutes this. */
#define PMGR_SLEEP_PARK_TICKS  3u

void pmgr_sleep(void);

/* pmgr.asm 1702-1714, the $FFF4 vector. Call from the main loop. Does nothing
 * unless the reset switch is currently pressed; otherwise asserts SYS_RST*,
 * waits out the switch (debounce is the switch's, 320 ms measured - poll it,
 * never use a fixed delay), then takes the ResetEntry path, which ALWAYS
 * releases regardless of VIA_TEST. */
void pmgr_reset_switch_service(void);

/* True once if the battery path asked for a forced sleep (pmgr.asm 463
 * `jmp Sleep`). Reading it clears it. Call from the main loop and, if set,
 * call pmgr_sleep() - never from inside a tick, which would re-enter it. */
bool pmgr_sleep_requested(void);

void pmgr_cmd_invalid(pmgr_packet_t *pkt);     /* clc; rts - no error reported */
void pmgr_cmd_power(pmgr_packet_t *pkt);       /* $1x */
void pmgr_cmd_adb(pmgr_packet_t *pkt);         /* $2x */
void pmgr_cmd_time_pram(pmgr_packet_t *pkt);   /* $3x */
void pmgr_cmd_contrast(pmgr_packet_t *pkt);    /* $4x */
void pmgr_cmd_modem(pmgr_packet_t *pkt);       /* $5x */
void pmgr_cmd_battery(pmgr_packet_t *pkt);     /* $6x */
void pmgr_cmd_sleep(pmgr_packet_t *pkt);       /* $7x */
void pmgr_cmd_timer(pmgr_packet_t *pkt);       /* $8x */
void pmgr_cmd_sound(pmgr_packet_t *pkt);       /* $9x */
void pmgr_cmd_pmgr(pmgr_packet_t *pkt);        /* $Ex */

void pmgr_cmd_read_int(pmgr_packet_t *pkt);    /* $78 */
void pmgr_cmd_read_adb(pmgr_packet_t *pkt);    /* $28 */

/* PowerFlags ($1D in the original) */
#define PF_CHARGER_CONNECTED  0x01u
#define PF_HICHG_ON           0x02u
#define PF_HICHG_OVERFLOW     0x04u
#define PF_BATTERY_DEAD       0x08u
#define PF_BATTERY_LOW        0x10u
#define PF_CHARGER_CHANGED    0x20u
/* Bit 6: while set, :1366 - `bbs 6,PowerFlags,.LAB_F0EA` - sends the sleep loop
 * straight back round WITHOUT testing any wake condition, so the machine cannot
 * be woken at all. Named for that effect.
 *
 * ⭐ THE CONDITION IS THE COMPUTED MIDPOINT, not either named threshold:
 *
 *     :438-445   $20 = (lowBatteryLevel>>1) + (deadBatteryLevel>>1) = 70
 *     :455-457   BatteryLevel < 70  ->  seb 6,PowerFlags
 *
 * 70 appears in no equate because it is derived at runtime from the other two.
 * See PMGR_ADC_STAY_ASLEEP_LEVEL in pmgr_adc.h.
 *
 * ⭐ OBSERVED ON HARDWARE 2026-08-28 (MEASUREMENTS.md 2.18): at 5.8 V the
 * machine refused ~30 s of keypresses while PMGCLK_F kept bursting, then woke
 * instantly at 6.5 V. */
#define PF_STAY_ASLEEP        0x40u

uint8_t pmgr_power_flags(void);
/* TWO different reads, and they are not interchangeable - pmgr.asm has both:
 *
 *   pmgr_battery_now()  = batteryNow  (:546-570)  RAW store. A seed, for init
 *                         (:202) and charger-change (:417).
 *   pmgr_battery_read() = ReadBattery (:518-545)  (new+old)/2. The periodic
 *                         read, used by BatteryManage (:401) - the smoothing
 *                         is what stops a transient dip tripping a shutdown.
 *
 * Both refresh ref5V_Level, which is never averaged in either. */
void    pmgr_battery_now(void);
void    pmgr_battery_read(void);
/* The tick spine. Apple's Int1 runs at 60 Hz (FUN_EB97, pmgr.asm 571-585) and
 * 1SEC* is DERIVED by dividing by 60 - it is not a primary timebase.
 * Call pmgr_tick_60hz() once per SIXTYHZ edge; it pulses 1SEC* every 60th. */
void    pmgr_tick_60hz(void);

/* A backlogged tick: advances the /60 divider and 1SEC* but does NOT poll ADB.
 * Use for the second and subsequent ticks of a batch, so a starved main loop
 * catches up on TIME without firing a burst of autopolls. */
void    pmgr_tick_catchup(void);
#ifdef PMGR_ADB_HOST_SERVICE
void    pmgr_tick_run_deferred(void);   /* fix A, MEASUREMENTS 2.209 */
#endif

void    pmgr_tick_1s(void);

/* Wake-up timer, pmgr.asm 596-610 / 1370 / 1376.
 *   pmgr_wake_timer_tick()     call from the 1 Hz tick; `asleep` mirrors the
 *                              original's $0 bit 6 (set :1356, cleared :1380)
 *   pmgr_wake_timer_expired()  $0 bit 5 - the armed time has been reached
 *   pmgr_wake_timer_ack()      :1376 clb 5,$0, on taking the wake            */
void    pmgr_wake_timer_tick(bool asleep);
bool    pmgr_wake_timer_expired(void);
void    pmgr_wake_timer_ack(void);
/* :1377 clb wake_time_on - DISARMS the timer. Runs only on the .LAB_F108 exit
 * (timer or ring wake), never on the AKD wake. Host-visible through the $8x
 * read, which reports the armed flag. */
void    pmgr_wake_timer_disarm(void);

/* ring_wake_on ($0 bit 7), armed by Modem_Command. With it set, a LOW
 * RING_DETECT wakes the machine (pmgr.asm 1371-1372). */
bool    pmgr_ring_wake_armed(void);

/* .ADBAction - run one ADB transaction if one is due. Call from the tick. */
void    pmgr_adb_poll(void);      /* invoked by the divider, not directly */

/* Poll for one falling edge on SIXTYHZ. Returns false on timeout, so a bench
 * board with no host still boots. Used during the release sequence, where
 * interrupts are still disabled - the original polls for the same reason. */
bool    pmgr_wait_sixtyhz_edge(void);

#ifdef PMGR_PARK_SLEEP
/* ⭐ LOW-POWER PARK (design/AVR-PARK-SLEEP.md, MEASUREMENTS 2.199). While
 * g_parked is set the SIXTYHZ and PIT ISRs only record a wake cause - they do
 * NOT count ticks, because the park services each tick by hand. */
extern volatile bool    g_parked;
extern volatile bool    g_park_edge;
extern volatile bool    g_park_pit;
#endif

#endif /* PMGR_HANDLERS_H */
