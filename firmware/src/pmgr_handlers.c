#include "pmgr_handlers.h"
#include "pmgr_calib.h"
#include "pmgr_pins.h"
#include "pmgr_rom.h"
#include "pmgr_adc.h"
#include "pmgr_bus.h"
#include "pmgr_adb.h"
#include "pmgr_pram.h"
#include "pmgr_pwm.h"
#include "pmgr_tick_defer.h"
#include <avr/interrupt.h>      /* cli/sei for the reset-switch path */
#ifdef PMGR_PARK_SLEEP
#include <avr/sleep.h>
#endif
#include <util/delay.h>

/* Apple's %10011111 - the "everything off" power byte. Written in TWO places
 * and it must be the same value in both, which is why it is defined once:
 *
 *   InitPorts     pmgr.asm 173-184   the power-on state
 *   Sleep entry   pmgr.asm 1355-1357 what drops the rails when parking
 *
 * Expressed with the named bitmasks rather than a literal 0x9F because PORTG's
 * bit order is NOT the M50753 Port 0 order - p0_write() exists precisely to
 * reverse between them. Writing a literal here would silently scramble the
 * rails. */
#define PMGR_P0_ALL_OFF  ((uint8_t)(SYS_PWR_N_bm    |   /* P07 = 1 */ \
                                    SERIAL_PWR_N_bm |   /* P04 = 1 */ \
                                    MODEM_PWR_N_bm  |   /* P03 = 1 */ \
                                    HD_PWR_N_bm     |   /* P02 = 1 */ \
                                    SCC_CNTRL_bm    |   /* P01 = 1 */ \
                                    IWM_CNTRL_bm))      /* P00 = 1 */
                                    /* MINUS5_EN (P06), SOUND_PWR_N (P05) = 0 */


static uint8_t s_power_flags;   /* $1D */
static uint8_t s_battery_level; /* $1E - inferred, see PORT-AUDIT.md */
static uint8_t s_ref5v_level;   /* $1F */
static uint8_t s_sec_divider;
#ifdef PMGR_ADB_HOST_SERVICE
static bool    s_1s_deferred;       /* fix A: a 60th tick's 1 Hz work waiting for a latched transaction */
#endif
/* HICHG state machine - $21/$22/$23 in the M50753 map. See pmgr_tick_1s(). */
static uint8_t  s_hichg_state;   /* $21: 0 idle, 1 charging, 2 top-off, 3 done */
/* ⚠️ MODELLED AS TWO BYTES, NOT A uint16_t. The binary's countdown is NOT a
 * plain 16-bit decrement:
 *     dec $22 / bne skip / dec $23 / bne skip / done
 * The high byte is touched only when the low byte reaches 0, and "done" needs
 * BOTH to hit 0 in the SAME pass. From $22=1,$23=1 that finishes in one tick,
 * whereas `--(uint16)0x0101` gives 0x0100 and does not. Same for the carry on
 * the way up. Keeping two bytes reproduces it exactly. */
static uint8_t  s_hichg_lo;      /* $22 */
/* ⭐ P1's ADC interlock, RECONCILED with the calibration gate (2.374, option A).
 * ⛔ THE FAULT IS NOT A STATE. P1 used s_hichg_state = 4, which $21 would have
 * reported to the Mac - a value Apple's firmware never produces.
 * design/FAST-CHARGE-TIMEOUT.md §3 states the opposite principle outright:
 * "No value Apple never produces is written into Apple's map; the fault lives
 * in a SEPARATE RECORD." So the state is forced to 3 - Apple's own "done", the
 * same thing the gate does - and "faulted" is carried HERE, visible at $302.
 * ⭐ Same actions, same N=3 debounce, same recovery. Only the bookkeeping moved. */
#define PMGR_HICHG_RECOVER_SAMPLES  3u
static bool     s_hichg_faulted;     /* ADC scale bad - NOT in Apple's map */
static uint8_t  s_hichg_valid_run;   /* consecutive valid samples while faulted */
static uint8_t  s_hichg_hi;      /* $23 */   /* $24 - counts 60 SIXTYHZ edges per second */

/* $0 bit 6 - "the machine is parked". Set on sleep entry (:1356 seb 6,$0),
 * cleared on wake (:1380 clb 6,$0). The wake-timer compare is gated on it. */
static bool     s_asleep;

/* pmgr.asm 463 `jmp Sleep` - the battery path parks the machine itself. Set
 * from the 1 Hz tick, consumed by the main loop; see the note at the call. */
static bool     s_sleep_requested;

bool pmgr_sleep_requested(void)
{
    bool r = s_sleep_requested;
    s_sleep_requested = false;
    return r;
}

uint8_t pmgr_power_flags(void) { return s_power_flags; }

/* COLD entry - clears state, then the ports. Only main() should call this.
 *
 * A reset-switch press must NOT come through here: ResetEntry preserves most
 * of this state. See pmgr_reset_entry_state(). */
void pmgr_handlers_init(void)
{
    s_power_flags = 0;
    s_battery_level = 0;
    s_ref5v_level = 0;
    s_sec_divider = 60u;            /* ldm #$3C,$24 */

    pmgr_ports_init(true);          /* ColdEntry: Y=8 */
}

/* ResetEntry's state handling - pmgr.asm 138-166, everything before InitPorts.
 *
 * What it clears is CONDITIONAL, and getting this wrong loses live state on
 * every reset-switch press:
 *
 *   :138-139  PRAM checksum good -> skip straight to :153
 *   :140-152  PRAM checksum BAD  -> zero PRAM, clear PowerFlags ($1D) and
 *                                   $21, restore the three thresholds
 *   :154-156  ALWAYS             -> clear $0 and ADBStatus ($6)
 *
 * ⭐ BatteryLevel ($1E) and ref5V_Level ($1F) are NEVER cleared - not even on
 * a bad checksum. InitRAM covers $2F..$AE and the explicit stores are $1D and
 * $21; $1E/$1F fall in neither. They simply carry over, which is right: the
 * battery has not changed just because the machine was reset, and zeroing them
 * would make the next BatteryManage read compare a smoothed value against a
 * seed of 0.
 *
 * ⭐ PowerFlags surviving matters most for PF_STAY_ASLEEP: clearing it on a
 * reset would let a machine wake that the battery state says must not.
 *
 * NOT modelled: the $24 zeroing at :160-165, which the original does only when
 * the TIME checksum fails. Our second-divider lives here while the time and
 * its checksum live in pmgr_pram.c, so the two are not currently coupled. The
 * cost is at most one second of drift after a reset that also lost the time. */
void pmgr_entry_state(void)
{
    bool pram_ok;
    (void)pmgr_pram_checksum(&pram_ok);

    if (!pram_ok) {
        /* :141-145 InitRAM - zero $2F..$AE, then :147-148 recompute into $AF.
         * ⛔ THIS WAS MISSING. The old version cleared PowerFlags and nothing
         * else, so a corrupt-but-present PRAM was loaded and KEPT. */
        pmgr_pram_zero();

        s_power_flags = 0;          /* :146  sta PowerFlags ($1D, outside the
                                     * zeroed block, hence its own store) */

        /* :149-151 restore lowBatteryLevel/deadBatteryLevel/HICHGLevel.
         * DELIBERATELY ABSENT. Those live at $71/$72/$73, INSIDE the $2F..$AE
         * block the wipe just cleared - the original must put them back. Ours
         * are compile-time constants in pmgr_adc.h, so nothing destroyed them
         * and there is nothing to restore.
         *
         * ⚠️ That divergence has a real behavioural consequence: in the
         * original the host can WRITE $71-$73 through an xPRAM write and
         * change the battery thresholds at runtime. Against our constants the
         * same write lands in s_pram[66..68] and is silently ignored. Not
         * known to be exercised by any Mac ROM, but it is a difference, not an
         * equivalence. */
    }

    pmgr_adb_status_clear();        /* :156  sta ADBStatus ($6) */

    /* s_battery_level ($1E), s_ref5v_level ($1F): outside the zeroed block and
     * never cleared here, on either path. Deliberate - see the header. */
}

/* InitPorts - pmgr.asm 173-184 only. No state is cleared here. */
static void ports_init_p0(uint8_t p0)
{
    /* ---- Port directions and initial values, taken verbatim from the
     * original's InitPorts (pmgr.asm 173-184). Every value here is Apple's,
     * not a guess. Dir bit 1 = output on the M50753; Dir_Write = $FF. ----
     *
     *   Port_P0 = %10011111  dir %11111111   -> our PORTG
     *   Port_P1 = %11000001  dir %10110101   -> our PORTE
     *   Port_P3 = %11110101  dir %01111010   -> our PORTB
     *   Port_P4 = %00001110  dir %00000101   -> our PORTD
     */

    /* PORTG == M50753 Port 0: all eight are outputs.
     *
     * ⭐ THE VALUE DIFFERS BY ENTRY PATH - pmgr.asm 167-172:
     *
     *     lda Port_P0 / and #4 / ora #$40   ; the RESET-entry value
     *     sty $1 / tst $1 / beq InitPorts   ; Y==0 (ResetEntry) -> use it
     *     lda #%10011111                    ; Y!=0 (ColdEntry)  -> use this
     *
     * So a COLD entry forces everything off, while the RESET switch PRESERVES
     * HD_PWR* (P02) and enables MINUS5_EN (P06). Writing $9F on both paths -
     * which we did until 2026-08-28 - powers the hard disk down on every press
     * of the reset switch, which the original explicitly avoids.
     *
     * ✅ VERIFIED ON HARDWARE, MEASUREMENTS.md 2.29. Scope on HD_PWR* (J21 pin
     * 30) through a real reset-switch press: it stays ASSERTED for the whole
     * 799 ms the machine is held in reset, never deasserting. The listing was
     * read correctly. */
    /* ⭐ THREE different Port_P0 values reach this store, by three paths:
     *
     *   ColdEntry   (Y=8)   $9F  everything OFF          :167-172
     *   ResetEntry  (Y=0)   (P0 & 4) | $40  preserve HD_PWR*, enable -5V
     *   $EF soft reset      $00  everything ON           see pmgr_cmd_pmgr()
     *
     * The third is not a Y case at all - `jmp InitPorts` from PMGR_Command
     * lands on this store with whatever the accumulator holds. */
    SYS_PWR_N_PORT.OUT = p0;
    SYS_PWR_N_PORT.DIR = 0xFFu;

    /* PORTE == M50753 Port 1. NOT all inputs: STOP_CLK, KBD_RST*, HICHG and
     * MODEM_A/B are DRIVEN by the PMGR (dir %10110101). HICHG in particular is
     * the high-charge ENABLE, not a sense line - the original only ever writes
     * it (pmgr.asm 479 seb / 510 clb). */
    AKD_PORT.DIRCLR = (uint8_t)(AKD_bm | CHRG_ON_N_bm | RING_DETECT_bm);
    AKD_PORT.OUTCLR = (uint8_t)(STOP_CLK_bm | KBD_RST_N_bm | HICHG_bm); /* all 0 */
    AKD_PORT.OUTSET = MODEM_AB_bm;                                      /* = 1  */
    AKD_PORT.DIRSET = (uint8_t)(STOP_CLK_bm | KBD_RST_N_bm |
                                HICHG_bm | MODEM_AB_bm);
    /* KBD_RST* starts LOW = keyboard controller held in reset. */

    /* PORTD == M50753 Port 4 plus the analogue input.
     * PMGR_ADB is ADB_Out (P40, output); FDB is ADB_In (P41, input). */
    PMGR_ADB_PORT.DIRCLR = (uint8_t)(FDB_bm | MODEM_INS_N_bm | PMGR_IN0_bm);
    PMGR_ADB_PORT.OUTCLR = (uint8_t)(PMGR_ADB_bm | SOUND_OFF_bm | PMGR_PWM_bm);
    PMGR_ADB_PORT.OUTSET = DISP_BLANK_N_bm;          /* P42 = 1 */
    PMGR_ADB_PORT.DIRSET = (uint8_t)(PMGR_ADB_bm | DISP_BLANK_N_bm |
                                     SOUND_OFF_bm | PMGR_PWM_bm);

    /* PORTB == M50753 Port 3. PMACK/PMINT/PMREQ are set up in pmgr_bus_init(). */
    ONESEC_N_PORT.OUTSET  = ONESEC_N_bm;             /* P34 = 1, deasserted */
    ONESEC_N_PORT.DIRSET  = ONESEC_N_bm;

    /* SYS_RST* = 0: system held in reset until pmgr_system_release().
     * The original holds it from pmgr.asm 1354 to 1406, across the whole
     * bring-up, releasing one instruction before CommandReceive. */
    ASSERT_LOW(SYS_RST_N_PORT, SYS_RST_N_bm);
    SYS_RST_N_PORT.DIRSET = SYS_RST_N_bm;
}

void pmgr_ports_init(bool cold)
{
    ports_init_p0(cold
        ? PMGR_P0_ALL_OFF
        : (uint8_t)((SYS_PWR_N_PORT.OUT & HD_PWR_N_bm) | MINUS5_EN_bm));
}

/* $EF soft reset: InitPorts entered with A = 0 - see pmgr_cmd_pmgr(). */
void pmgr_ports_init_softreset(void) { ports_init_p0(0x00u); }


/* One falling edge on SIXTYHZ, polled. Guard is longer than a 50 Hz period so
 * this works on either line frequency; it returns false rather than hanging if
 * SIXTYHZ is absent (bench board with no host attached). */
bool pmgr_wait_sixtyhz_edge(void)
{
    bool prev = (SIXTYHZ_PORT.IN & SIXTYHZ_bm) != 0u;
    for (uint16_t i = 0; i < 25000u; i++) {
        bool now = (SIXTYHZ_PORT.IN & SIXTYHZ_bm) != 0u;
        if (prev && !now) return true;
        prev = now;
        _delay_us(1);
    }
    return false;
}

/* Bring the machine up, in the RESET/INIT order (pmgr.asm 198-218).
 *
 * ⭐ MEASURED AND VALIDATED 2026-08-28 - MEASUREMENTS.md 2.5b. Portable #2,
 * reset switch tapped: KBD_RST* released at +322 ms, SYS_RST* at +352 ms.
 * The 30 ms separation is this function's two-tick wait, on silicon.
 *
 * The order is deliberate and is NOT the same as the wake path:
 *
 *   reset (198-218):   batteryNow -> KBD_RST RELEASED -> 2 ticks -> SYS_RST released
 *   wake  (1395-1406): KBD_RST ASSERTED -> 2 ticks -> KBD_RST released
 *                      -> SYS_RST released 4 us later
 *
 * Note the wake path ASSERTS KBD_RST first (clb at 1395) and ends with two
 * ADJACENT sebs, so its two releases are ~4 us apart, not 30 ms. That 4 us vs
 * 30 ms is what distinguishes the two paths in a capture; MEASUREMENTS.md 2.5
 * is the wake path, 2.5b is this one.
 *
 * On reset the keyboard controller is released BEFORE the delay so it has
 * time to come up; the system is released after. Releasing both together - as
 * an earlier revision did - gives the keyboard no head start.
 *
 * WHICH PATH RUNS WHEN. The hardware RESET vector at $FFFE is PMGRResetEntry,
 * which is `bra ColdEntry` (112-113); ColdEntry sets Y=8, so `cpy #0` at 204
 * fails and the code takes `jmp Sleep` at 207. Power-on therefore NEVER
 * reaches the SYS_RST release below - the machine parks and waits for a key,
 * which is the observed bench behaviour. This function's path is entered only
 * via ResetEntry (Y=0), reachable from exactly one site: `jmp ResetEntry` at
 * 1714, the tail of the Reset_Interrupt handler for the reset switch.
 *
 * TIMING BUDGET, measured: 2 ms between the port init that drives KBD_RST*
 * low (177) and the release at 203. SetContrast + batteryNow must fit there.
 *
 * Interrupts must still be DISABLED when this is called: Apple's delay polls
 * the timer flag (bit Int_Ctrl_Reg / bpl) precisely because cli comes later,
 * at 221, after SYS_RST is already released.
 */
/* pmgr.asm 190-203 - everything the reset path does BEFORE the park decision.
 *
 * ⭐ THIS MUST NOT LIVE IN pmgr_system_release(). The original's dispatch is at
 * :204-207, AFTER all of this:
 *
 *     190-196  validate $2E, default 4
 *     200      jsr SetContrast
 *     201      seb 0,PWM_Ctrl_Reg      PWM on
 *     202      jsr batteryNow
 *     203      seb KBD_RST             keyboard released
 *     204-207  cpy #0 / bbc VIA_TEST / jmp Sleep   <- dispatch
 *
 * An earlier revision bundled these into pmgr_system_release(), which runs only
 * on the RELEASE branch. So on the PARK branch - which is every normal cold
 * boot, since ColdEntry sets Y=8 - none of it ran. pmgr_contrast_validate()
 * never executed, and pmgr_sleep()'s wake then did pmgr_pwm_enable(true) at
 * :1396 using whatever 5-bit value .noinit happened to hold: a random screen
 * contrast on every cold start.
 *
 * ⚠️ Said "brightness" until 2026-09-07. On the Portable the words are loosely
 * interchangeable; on a PowerBook 100 they are NOT - pin 57's PWM drives the
 * CCFL inverter's BRIGHTNESS there, not contrast (MEASUREMENTS.md 2.68/2.69).
 * The VALUE this restores is the contrast setting on both machines.
 *
 * Call this BEFORE pmgr_cold_should_park(). Interrupts must already be off. */
void pmgr_pre_dispatch_init(void)
{
    /* 190-196  validate the stored contrast BEFORE using it. $2E is RAM below
     * the PRAM block, so it survives a reset but is garbage after a power
     * loss; the original range-checks rather than initialising.
     * 200-201  jsr SetContrast / seb 0,PWM_Ctrl_Reg */
    pmgr_contrast_validate();
    pmgr_contrast_set(pmgr_contrast_get());
    pmgr_pwm_enable(true);

    /* 202  jsr batteryNow - the battery and +5V reference are sampled BEFORE
     * anything is released, so the host finds valid readings immediately.
     * Also true on the park branch: a parked PMGR still needs a battery
     * reading, and BatteryManage smooths against it from the first tick. */
    pmgr_battery_now();

    /* 203  seb KBD_RST,Port_P1 - released here on BOTH branches. The wake path
     * re-asserts it at :1395 if we do park. */
    DEASSERT_HIGH(KBD_RST_N_PORT, KBD_RST_N_bm);
}

/* pmgr.asm 208-218 - the RELEASE branch only. Everything above must already
 * have run; call pmgr_pre_dispatch_init() first. */
void pmgr_system_release(void)
{

    /* 209-215  ldx #2 / bit Int_Ctrl_Reg / bpl / jsr FUN_EB97 / dex / bne
     *
     * QUANTIFIED 2026-08-26. The loop waits twice for Int1_Req (bit 7 of
     * Int_Ctrl_Reg), calling FUN_EB97 each time. FUN_EB97 (571-585) decrements
     * $24 and, when it reaches zero, pulses 1SEC* and reloads $24 with $3C=60.
     * One 1SEC* pulse per 60 calls means FUN_EB97 runs at 60 Hz, so Int1 is the
     * 60 Hz source - almost certainly the SIXTYHZ line, which we have on PC2.
     *
     * We now wait for two REAL edges, exactly as Apple does, rather than a
     * fixed delay - self-correcting on a 50 Hz machine. If SIXTYHZ is absent
     * we fall back to the nominal period so a bench board still boots. */
    for (uint8_t i = 0; i < 2u; i++) {
        if (!pmgr_wait_sixtyhz_edge())
            _delay_ms(17);          /* no SIXTYHZ - approximate one period */
    }

    /* 216  seb SYS_RST,Port_P3 - the 68000 starts executing HERE. */
    DEASSERT_HIGH(SYS_RST_N_PORT, SYS_RST_N_bm);

    /* 217-218  seb SOUND_PWR,Port_P0 / clb SOUND_PWR,Port_P0 - a pulse on the
     * sound rail immediately after release. Purpose not established, but it is
     * unconditional, so reproduce it. */
    SOUND_PWR_N_PORT.OUTSET = SOUND_PWR_N_bm;
    SOUND_PWR_N_PORT.OUTCLR = SOUND_PWR_N_bm;
}

/* ===================== SLEEP / WAKE / RESET SWITCH ======================= */

static void sleep_wait_tick(void);      /* defined with the tick spine below */

/* pmgr.asm 204-207 - the ColdEntry / ResetEntry dispatch.
 *
 *   204  cpy #0
 *   205  beq LAB_E8F9                    ; Y==0 (ResetEntry) -> release
 *   206  bbc VIA_TEST,Port_P3,LAB_E8F9   ; VIA_TEST CLEAR    -> release
 *   207  jmp Sleep                       ; Y!=0 AND VIA_TEST SET -> PARK
 *
 * The hardware RESET vector is PMGRResetEntry -> `bra ColdEntry` (:112-113),
 * which sets Y=8. So EVERY hardware reset is a cold entry, and the decision
 * collapses to VIA_TEST alone. ResetEntry (Y=0) is reachable from exactly one
 * place, the `jmp ResetEntry` at :1714 - which is why pmgr_reset_switch_service()
 * calls pmgr_system_release() directly rather than coming through here.
 *
 * ✅ POLARITY CONFIRMED - MEASURED 2026-08-28, MEASUREMENTS.md 2.23.
 * Parking requires VIA_TEST SET. J21 pin 6 read 5.2 V on a running machine, so
 * SET = HIGH and the test below is the right way round.
 *
 * TWO legs, and it needs both. The reading above is taken while the machine is
 * RUNNING; the park decision is taken at cold entry, before SYS_RST* is
 * released and therefore before the VIA is alive. What bridges the gap is 2.5:
 * a real Portable does park at power-on and needs a keypress, so VIA_TEST was
 * already SET at cold entry too. Running level and reset-time behaviour agree,
 * which is what a static pull-up looks like. Had they disagreed, VIA_TEST would
 * be VIA-driven and the reset-time level would need its own capture.
 *
 * Bench note: a board that floats this low boots straight through instead of
 * parking, which is the more useful bench default anyway. */
bool pmgr_cold_should_park(void)
{
    return !PIN_IS_LOW(VIA_TEST_PORT, VIA_TEST_bm);
}

/* Sleep, park, and wake - pmgr.asm 1329-1410.
 *
 * Returns only once the machine is fully awake and released, because that is
 * what the original does: `jmp Sleep` never comes back, it falls through the
 * park loop into the wake path and ends at `jmp CommandReceive` (:1410).
 *
 * ⭐ MEASURED - MEASUREMENTS.md 2.10 and 2.11:
 *   - sleep entry asserts SYS_RST* and NOTHING else; KBD_RST* stays released
 *   - while parked the core runs ~85 us once per 60 Hz tick (WIT + Int1),
 *     i.e. timekeeping never stops
 *   - wake waits for the key to be RELEASED (measured 224 ms hold), then
 *     asserts KBD_RST*, waits 2 ticks (measured 32 ms), then releases
 *     KBD_RST* and SYS_RST* ~5 us apart
 *
 * THREE WAKE SOURCES, all implemented in the park loop below:
 *
 *   AKD    :1367-1368  armed by a RELEASE then woken by a PRESS. The only one
 *                      the machine uses in practice, and the only one observed
 *                      on hardware (2.11, 2.13).
 *   timer  :1370       $0 bit 5, set by the 1 Hz compare at :596-610 when the
 *                      armed wakeupTime matches. Armed by pmgr_cmd_timer().
 *   ring   :1371-1372  ring_wake_on ($0 bit 7) AND RING_DETECT reading LOW.
 *                      Armed by pmgr_cmd_modem().
 *
 * ⚠️ Only the AKD path has been exercised END TO END on real hardware - a wake
 * has been observed to fire. The arming commands for the other two ARE measured:
 *
 *   cmd_timerSet     $80, count 4, MSB-first   MEASURED, 2.32 (two captures whose
 *                                              payloads differ by exactly 1500)
 *   cmd_disableWakeUp $82                      binary-verified; was wrongly $81
 *   Modem_Command    $50/$58                   MEASURED, 2.30
 *   RingWakeEnable   payload bit 2             MEASURED, 2.30
 *
 * What is still Class D is the FIVE REMAINING modem payload bits, none of which
 * this loop depends on. So if a timer or ring wake misbehaves, suspect this loop
 * or the 1 Hz compare at :596-610 BEFORE suspecting the opcodes - they are better
 * evidenced than most of this file. */
void pmgr_sleep(void)
{
    /* 1330  clb DISP_BLANK,Port_P4 - display off */
    ASSERT_LOW(DISP_BLANK_N_PORT, DISP_BLANK_N_bm);

    /* 1331  sei - 6502 SEI DISABLES; the listing's own comment says
     * "Disable interrupts". Held until :1409 `cli` (ENABLE) at the end of the
     * wake path.
     *
     * Not optional for us. pmgr_cmd_sleep() reaches here from the poll loop
     * with interrupts ENABLED, and every wait below calls sleep_wait_tick(),
     * which polls SIXTYHZ and services the tick BY HAND. Leaving the ISR live
     * would count each 60 Hz edge twice - once here, once when the pending
     * flag replays - and the Mac's clock would gain time for the whole
     * duration of sleep.
     *
     * SREG is SAVED and RESTORED rather than ending with sei(). The original
     * can simply enable at :1409 because it has exactly one caller; we have
     * two, entered in different interrupt states (main() before sei(), and
     * pmgr_cmd_sleep() from the poll loop after it). Restoring leaves no
     * hidden side effect at either call site - the same pattern as
     * pmgr_adb_transact(). */
    const uint8_t sreg = SREG;
    cli();

    /* 1332-1344  three 60 Hz periods, servicing FUN_EB97 on each */
    sleep_wait_tick();
    sleep_wait_tick();
    sleep_wait_tick();

    /* 1345-1346  a pulse on the sound rail, as at 217-218 */
    SOUND_PWR_N_PORT.OUTSET = SOUND_PWR_N_bm;
    SOUND_PWR_N_PORT.OUTCLR = SOUND_PWR_N_bm;

    /* 1347-1351  save Port_P0 (with SYS_PWR forced SET) and Port_P1, for
     * restoration on wake at 1384-1387. */
    const uint8_t saved_p0 = (uint8_t)(SYS_PWR_N_PORT.OUT | SYS_PWR_N_bm);
    const uint8_t saved_p1 = KBD_RST_N_PORT.OUT;

    /* 1352  clb SOUND_OFF,Port_P3 - clear the bit, i.e. UNMUTE.
     * Written as a bare OUTCLR rather than ASSERT_LOW(): SOUND_OFF is not an
     * active-low signal (it has no _N suffix), so the assert/deassert macros
     * would read as the opposite of what this does. Same bit operation. */
    SOUND_OFF_PORT.OUTCLR = SOUND_OFF_bm;

    /* 1353  clb 0,PWM_Ctrl_Reg - contrast PWM off. */
    pmgr_pwm_enable(false);

    /* ⭐ THREE TICK EDGES BEFORE THE PARK - MEASURED, MEASUREMENTS.md 2.27.
     *
     * The host's last bus transaction ends and the PMGR parks THREE 60 Hz tick
     * edges later. Three sleeps measured 37, 44 and 45 ms; the spread is one
     * tick wide because the command lands at an arbitrary phase within a tick:
     *
     *     delay = (K-1) * 16.67 ms + phase,   phase in [0, 16.67)
     *     K = 3  ->  33.3 .. 50.0 ms   contains all three
     *     K = 2  ->  16.7 .. 33.3 ms   excluded
     *     K = 4  ->  50.0 .. 66.7 ms   excluded
     *
     * ⚠️ COUNT TICKS, NEVER DELAY MILLISECONDS. A fixed ms delay cannot be
     * right: the observed interval genuinely varies by up to a full tick, and
     * that variation IS the quantization. Waiting on tick edges reproduces it
     * for free; _delay_ms() of any value would be wrong most of the time.
     *
     * Interrupts are already disabled here (1331), so the tick is serviced by
     * hand exactly as the release path does. */
    for (uint8_t k = 0; k < PMGR_SLEEP_PARK_TICKS; k++) {
        if (!pmgr_wait_sixtyhz_edge())
            break;                  /* bench board with no host - park anyway */
        pmgr_tick_catchup();        /* keep TIME advancing across the wait */
    }

    /* 1354  clb SYS_RST,Port_P3 - the machine goes down HERE, and this is the
     * only line sleep entry touches (measured, 2.10). */
    ASSERT_LOW(SYS_RST_N_PORT, SYS_RST_N_bm);

    /* ⭐ Make outstanding PRAM changes durable BEFORE the rails go down
     * (MEASUREMENTS 2.156). Not in the original, whose PRAM is battery RAM.
     * Safe to block: SYS_RST* is asserted, so there is no host to serve. */
    (void)pmgr_pram_flush();          /* bounded; proceed either way (2.184) */

    s_asleep = true;                /* 1356  seb 6,$0 */

    /* ⭐ 1357  Port_P0 = %10011111 - THE RAILS GO DOWN HERE.
     *
     * ⛔ THIS WRITE WAS MISSING until 2026-08-28. The comment described it; the
     * code only cleared `armed`. Without it, sleep left Port_P0 at whatever the
     * host last set: no rails dropped, no power saved, and SYS_PWR* never
     * deasserted - so a formal Shut Down would have powered nothing down.
     *
     * Caught by MEASUREMENTS.md 2.28: on a real shutdown, SYS_PWR* IS observed
     * going high shortly after SYS_RST* falls, which is only possible if this
     * write happens. The wake path already restored saved_p0 (:1394 and the
     * restore below), so the intent was always there - only the entry half was
     * absent. */
    SYS_PWR_N_PORT.OUT = PMGR_P0_ALL_OFF;

    /* 1355-1358  clear the AKD arming flag. Note 1358 branches PAST the arming
     * check on the first pass, so the first tick can never both arm and wake. */
    bool armed = false;

    /* Which label the loop exits to. .LAB_F108 (timer and ring) clears the
     * timer state; .LAB_F10F (AKD) does not. Both then run 1380 onward. */
    bool timer_path = false;

    /* ---- the park loop, 1359-1372 ---- */
    for (;;) {
        sleep_wait_tick();              /* 1360  jsr FUN_EB97 */

        /* ⭐ 1361-1362  ARM HERE - FIRST, and on EVERY pass.
         *
         *     .LAB_F0EA  jsr FUN_EB97
         *                bbs AKD,Port_P1,.LAB_F0F2   ; key down -> skip
         *                seb 2,$0                    ; key up   -> ARM
         *     .LAB_F0F2  clb/seb STOP_CLK
         *                bbs 6,PowerFlags,.LAB_F0EA  ; stay-asleep -> loop
         *
         * ⛔ THE ORDER MATTERS AND WE HAD IT WRONG. We tested PF_STAY_ASLEEP
         * first and did `armed = false` on that path. The original arms BEFORE
         * that test and NEVER clears bit 2 in the loop, so the arming survives
         * a low-battery park. Clearing it meant that after the battery
         * recovered we needed one further tick with no key down before a press
         * could wake - where the original is already armed and wakes at once,
         * which is what MEASUREMENTS.md 2.18 observed at 6.5 V. */
        if (PIN_IS_LOW(AKD_PORT, AKD_bm))
            armed = true;

        /* 1364-1365  clb/seb STOP_CLK - halt the core until the next
         * interrupt. We cannot stop our own clock the same way; waiting for
         * the 60 Hz edge is behaviourally identical and costs only power.
         * A real AVR sleep mode belongs here eventually. */
        STOP_CLK_PORT.OUTCLR = STOP_CLK_bm;
        STOP_CLK_PORT.OUTSET = STOP_CLK_bm;

        /* 1366  bbs 6,PowerFlags -> keep sleeping without testing any wake
         * condition. Set at :457 in the battery path. `armed` is deliberately
         * left ALONE - see above. */
        if (s_power_flags & PF_STAY_ASLEEP)
            continue;

        /* 1367-1368  armed AND a key now down -> wake.
         * ⭐ This two-state requirement is why a very short tap can be missed
         * entirely: AKD is only sampled here, once per 60 Hz tick, so a tap
         * shorter than ~16.6 ms can fall between two samples. Measured in
         * MEASUREMENTS.md 2.13 - a 6 ms tap failed, a 28 ms tap woke it. */
        if (armed && !PIN_IS_LOW(AKD_PORT, AKD_bm))
            break;                      /* -> .LAB_F10F, timer state UNTOUCHED */

        /* 1370  bbs 5,$0,.LAB_F108 - the WAKE-UP TIMER fired. Set by the 1 Hz
         * compare at :596-610. */
        if (pmgr_wake_timer_expired()) {
            timer_path = true;          /* -> .LAB_F108 */
            break;
        }

        /* 1371-1372  RING WAKE, the third source: armed by Modem_Command
         * (ring_wake_on, $0 bit 7) AND RING_DETECT reading LOW.
         *
         *     1371  bbc ring_wake_on,$0,.LAB_F0EA   ; not armed -> keep sleeping
         *     1372  bbs RING_DETECT,Port_P1,.LAB_F0EA ; HIGH -> keep sleeping
         *
         * Note the polarity: a LOW RING_DETECT is the ring. That matches the
         * inversion in .modemRead (:1260-1261), so both places agree even
         * though the signal carries no _N in our pin map. */
        if (pmgr_ring_wake_armed() && PIN_IS_LOW(RING_DETECT_PORT, RING_DETECT_bm)) {
            timer_path = true;          /* ring falls THROUGH .LAB_F108 too */
            break;
        }
    }

    /* ---- wake, 1378-1410 ---- */

    /* ⭐ 1373-1377 .LAB_F108 - ONLY the timer and ring paths land here:
     *
     *     .LAB_F108  clb 5,$0              ; clear the timer-hit
     *                clb wake_time_on,$0   ; and DISARM the timer
     *     .LAB_F10F  clb 6,$0              ; <- the AKD wake enters HERE
     *
     * ⛔ WE PREVIOUSLY DID BOTH-AND-NEITHER: `pmgr_wake_timer_ack()` ran on
     * EVERY wake including AKD (Apple leaves bit 5 alone there), and
     * `clb wake_time_on` was never reproduced at all - s_wake_time_on was only
     * cleared by an explicit $8x disable.
     *
     * The second half is HOST-VISIBLE: `.timerRead` returns the armed flag as
     * $17 bit 0, so after a timer wake a host reading the timer state saw
     * "still armed" from us and "disarmed" from Apple.
     *
     * ⚠️ An earlier note here claimed that leaving bit 5 set on an AKD wake was
     * "arguably a bug in Apple's code, reproduced deliberately". IT IS NOT.
     * FUN_EB97 clears bit 5 at the top of EVERY tick (:572), so it cannot
     * persist past one tick in the original. The persistence was ours. The
     * .LAB_F108 clear below is still correct - it is what the original does -
     * but it is no longer the only thing preventing a stale hit. */
    if (timer_path) {
        pmgr_wake_timer_ack();          /* 1376  clb 5,$0 */
        pmgr_wake_timer_disarm();       /* 1377  clb wake_time_on,$0 */
    }

    s_asleep = false;                   /* 1380  clb 6,$0 - BOTH paths */

    /* 1381-1383  ADBStatus &= 4 */
    pmgr_adb_wake_reset();

    /* 1384-1387  restore the saved port bytes */
    SYS_PWR_N_PORT.OUT = saved_p0;
    KBD_RST_N_PORT.OUT = saved_p1;

    /* 1388-1393  wait for the key to be RELEASED, servicing the tick.
     * ⭐ MEASURED 224 ms (2.11-bis) - this is the operator's finger, and the
     * core runs the whole time with SYS_RST* still asserted. */
    while (!PIN_IS_LOW(AKD_PORT, AKD_bm))
        sleep_wait_tick();

    /* 1394  clb SYS_PWR,Port_P0 - system power on */
    ASSERT_LOW(SYS_PWR_N_PORT, SYS_PWR_N_bm);

    /* 1395  clb KBD_RST,Port_P1 - keyboard controller INTO reset. The wake
     * path asserts it; the reset path at :203 releases it. Opposite senses,
     * and the single most confusable thing about these two routines. */
    ASSERT_LOW(KBD_RST_N_PORT, KBD_RST_N_bm);

    /* 1396  seb 0,PWM_Ctrl_Reg - PWM back on. Sleep never touches the
     * contrast VALUE, so re-enabling the output restores the previous
     * setting without a fresh SetContrast. (Said "brightness" until
     * 2026-09-07 - see the note at pmgr_pre_dispatch_init.) */
    pmgr_pwm_enable(true);

    /* 1397  seb DISP_BLANK,Port_P4 - display back on */
    DEASSERT_HIGH(DISP_BLANK_N_PORT, DISP_BLANK_N_bm);

    /* 1398-1404  ldx #2 - two 60 Hz ticks. Measured 32 ms (2.11-bis) and
     * 19.2 ms (2.11a): both are two ticks entered at an arbitrary phase. */
    sleep_wait_tick();
    sleep_wait_tick();

    /* 1405-1406  two ADJACENT sebs - measured 5 us apart (2.4 gives the
     * 5-cycle cost). KBD_RST* first, SYS_RST* second. */
    DEASSERT_HIGH(KBD_RST_N_PORT, KBD_RST_N_bm);
    DEASSERT_HIGH(SYS_RST_N_PORT, SYS_RST_N_bm);

    /* 1407-1409  clb Int2_Req / seb Int2_Enb / cli (ENABLE).
     *
     * Discard the tick flags that latched while we were servicing ticks by
     * hand, or each one replays as an extra tick for time already counted -
     * and sleep can last hours, so the backlog counter would be saturated. */
    SIXTYHZ_PORT.INTFLAGS = SIXTYHZ_bm;
    RTC.PITINTFLAGS       = RTC_PI_bm;

    /* Restore, do not force. See the contract in pmgr_handlers.h. */
    SREG = sreg;
}

/* The reset switch - pmgr.asm 1702-1714, the $FFF4 vector on P3 bit 0.
 *
 *   1704  clb SYS_RST,Port_P3            ; assert immediately
 *   1705-1712  wait two 60 Hz ticks
 *   1713  bbc RESET,Port_P3,.LAB_F32F    ; loop while RESET is still LOW
 *   1714  jmp ResetEntry                 ; Y=0 -> ALWAYS releases
 *
 * ⭐ MEASURED - MEASUREMENTS.md 2.5b: SYS_RST* asserted at t0, the switch
 * bounced/was held for 320 ms, then KBD_RST* fell (port init) and released
 * 2 ms later, with SYS_RST* following 30 ms after that.
 *
 * Because it ends at ResetEntry (Y=0), `cpy #0` at :204 succeeds and the
 * VIA_TEST test is never reached - a reset-switch press ALWAYS releases the
 * machine, even when a cold power-on would have parked it. */
void pmgr_reset_switch_service(void)
{
    if (!PIN_IS_LOW(RESET_N_PORT, RESET_N_bm))
        return;                                 /* not pressed */

    /* Interrupts OFF for the whole sequence.
     *
     * In the original this is an ISR (the $FFF4 vector), so interrupts are
     * already disabled on entry, and Entry2 keeps them that way with `sei` at
     * :1335 - 6502 SEI DISABLES - until `cli` at :221, after SYS_RST is high.
     *
     * We are called from the main loop with interrupts ENABLED, so we must do
     * it explicitly. pmgr_system_release() documents the same requirement.
     *
     * Timekeeping does not stop: sleep_wait_tick() calls tick_core() by hand on
     * every 60 Hz edge, exactly as the original calls FUN_EB97 inside its wait
     * loops. That matters here because the debounce can run 320 ms (measured,
     * MEASUREMENTS.md 2.5b) - far longer than one tick, so relying on latched
     * interrupt flags would lose most of them.
     *
     * Saved and restored, not forced - the same contract as pmgr_sleep() and
     * pmgr_adb_transact(). Today the only caller is the main loop with
     * interrupts already on, so a bare sei() would behave identically; but
     * restoring keeps ONE rule true everywhere, instead of a rule plus an
     * exception a reader has to verify by checking who calls what. */
    const uint8_t sreg = SREG;
    cli();

    /* 1704 */
    ASSERT_LOW(SYS_RST_N_PORT, SYS_RST_N_bm);

    /* 1705-1712  two ticks, then 1713 loops until the pin is released.
     * The debounce length is the SWITCH's, not the code's - 320 ms measured.
     * Poll the pin as the original does; never substitute a fixed delay. */
    sleep_wait_tick();
    sleep_wait_tick();
    while (PIN_IS_LOW(RESET_N_PORT, RESET_N_bm))
        sleep_wait_tick();

    /* 1714  jmp ResetEntry -> the conditional state handling at 138-166, then
     * InitPorts at 173-184, then the release path at 208-218.
     *
     * NOT pmgr_handlers_init(): that is the COLD path and clears PowerFlags,
     * the battery levels and the divider unconditionally. ResetEntry clears
     * PowerFlags only when the PRAM checksum fails and never clears the
     * battery levels at all - so coming through the cold path would discard
     * PF_STAY_ASLEEP on every reset-switch press. */
    pmgr_entry_state();             /* :138-166, shared with ColdEntry */
    pmgr_ports_init(false);         /* :167-184, ResetEntry value */
    pmgr_pre_dispatch_init();       /* :190-203 */
    pmgr_system_release();          /* :208-218 */

    /* No park check here: ResetEntry sets Y=0, so `cpy #0` at :204 succeeds
     * and :208 is reached unconditionally. A reset-switch press always
     * releases the machine, whatever VIA_TEST says. */

    /* Discard the tick interrupts that latched while we were servicing them by
     * hand - otherwise every pending flag fires an EXTRA tick_core() the moment
     * interrupts return, double-counting time we have already accounted for.
     * The original's InitPorts clears the lot with `ldm #0,Int_Ctrl_Reg`
     * (:186), which is the same intent. */
    SIXTYHZ_PORT.INTFLAGS = SIXTYHZ_bm;
    RTC.PITINTFLAGS       = RTC_PI_bm;

    SREG = sreg;            /* :221 cli (6502 CLI ENABLES) - restored, not forced */
}

void pmgr_cmd_invalid(pmgr_packet_t *pkt)
{
    (void)pkt;   /* the original does clc; rts - deliberately silent */
}

/* ---- M50753 Port 0 <-> our PORTG ----------------------------------------
 * The host's power bitmap is in PORT 0 bit order. Our PORTG carries the same
 * eight signals in the OPPOSITE order - verified exact reversal, P0 bit N maps
 * to PORTG bit 7-N for all eight:
 *
 *   P0:  0 IWM  1 SCC  2 HD  3 MODEM  4 SERIAL  5 SOUND  6 -5V  7 SYS
 *   PG:  0 SYS  1 -5V  2 SOUND 3 SERIAL 4 MODEM  5 HD    6 SCC  7 IWM
 *
 * So Apple's algorithm runs on a P0-order byte and we reverse on the way in
 * and out. Skipping this would send every rail command to the wrong rail.
 *
 * The invariant is CHECKED AT GENERATION TIME: gen_pins.py verifies that all
 * eight Port 0 signals sit on one port in exactly reversed bit order, and
 * refuses to emit a header otherwise. This #error catches a stale header. */
#ifndef PMGR_P0_EXACT_REVERSAL
#error "pmgr_pins.h is stale or the board no longer maps Port 0 as an exact bit reversal. Re-run gen_pins.py; if it fails, pmgr_cmd_power() must be reworked."
#endif
static uint8_t reverse8(uint8_t v)
{
    v = (uint8_t)(((v & 0xF0u) >> 4) | ((v & 0x0Fu) << 4));
    v = (uint8_t)(((v & 0xCCu) >> 2) | ((v & 0x33u) << 2));
    v = (uint8_t)(((v & 0xAAu) >> 1) | ((v & 0x55u) << 1));
    return v;
}
static uint8_t p0_read(void)       { return reverse8(PMGR_P0_PORT.OUT); }
static void    p0_write(uint8_t v) { PMGR_P0_PORT.OUT = reverse8(v); }

/* --- $1x Power ------------------------------------------------------------
 * Translated LITERALLY from pmgr.asm 940-980. The payload is a SEQUENCE of
 * bitmap bytes, each applied to Port 0:
 *
 *   bit 7 clear -> .TurnOn   P0 = ((c & $40) ^ $7F) & ((c & $7F) | P0)
 *   bit 7 set   -> off path  P0 = (~c & P0) | (c & $40)
 *
 * Bit 6 is special-cased in both paths because MINUS_5V has the opposite
 * polarity to the active-low rails. Do not "simplify" this - the Mac ROM
 * driver emits exactly this format. */
void pmgr_cmd_power(pmgr_packet_t *pkt)
{
    if (PMGR_CMD_ISREAD(pkt->command)) {
        /* .powerRead (pmgr.asm 981-990): one byte, SYS_PWR masked off, bits
         * 0-5 inverted, and bit 5 replaced by the SOUND_LATCH INPUT rather
         * than the port latch. */
        uint8_t a = (uint8_t)(p0_read() & 0x7Fu);
        a ^= 0x3Fu;
        if ((SOUND_LATCH_PORT.IN & SOUND_LATCH_bm) != 0u) a |= (uint8_t)(1u << 5);
        else                                              a &= (uint8_t)~(1u << 5);
        pmgr_return_data(pkt->command, &a, 1);
        return;
    }

    if (pkt->length == 0u) return;          /* tst ByteCount / bne / rts */

    for (uint8_t i = 0; i < pkt->length; i++) {
        uint8_t c  = pkt->data[i];
        uint8_t p0 = p0_read();             /* Apple re-reads Port_P0 each pass */

        if ((c & 0x80u) != 0u) {
            p0 = (uint8_t)(((uint8_t)(~c) & p0) | (c & 0x40u));
        } else {
            uint8_t t = (uint8_t)((c & 0x7Fu) | p0);
            p0 = (uint8_t)((uint8_t)((c & 0x40u) ^ 0x7Fu) & t);
        }
        p0_write(p0);                       /* written inside the loop */
    }
}

/* --- $6x Battery ----------------------------------------------------------
 * The original returns THREE consecutive zero-page bytes starting at
 * PowerFlags (pmgr.asm 1281-1284):
 *
 *     ldm #PowerFlags,$1B   ; $1D
 *     ldm #0,$1C
 *     ldm #3,ByteCount
 *     jsr ReturnDataToHost2
 *
 * so the payload is  PowerFlags ($1D), BatteryLevel ($1E), ref5V_Level ($1F).
 * BatteryLevel has no equate in the listing; $1E is by elimination. */
/* batteryNow - pmgr.asm 546-570. RAW, unsmoothed:
 *      556  lda AD_Reg / 557  sta BatteryLevel
 *
 * A seed, not a measurement. Used where the previous value is meaningless or
 * stale: at init (:202) and the instant the charger is plugged or unplugged
 * (:417), where averaging against the old state would smear the transition. */
void pmgr_battery_now(void)
{
    s_battery_level = pmgr_adc_read_battery();   /* AD_Batt, channel 1 */
    s_ref5v_level   = pmgr_adc_read_ref5v();     /* AD_Ref,  channel 0 */
}

/* ReadBattery - pmgr.asm 518-545. The SMOOTHED periodic read:
 *
 *      528  lda AD_Reg
 *      529  clc
 *      530  adc BatteryLevel
 *      531  ror A              ; (new + old) / 2, carry rotated into bit 7
 *      532  sta BatteryLevel
 *
 * A single-pole IIR with alpha = 0.5. `ror` after `adc` rotates the add's carry
 * into bit 7, so it is a true 9-bit sum halved back to 8 - not a lossy
 * ((a>>1)+(b>>1)).
 *
 * ⭐ THIS IS THE ONE BatteryManage USES (:401), and the smoothing is the point:
 * the low/dead-battery thresholds gate a SHUTDOWN, and a Portable's rail dips
 * whenever the drive spins or the backlight strikes. Deciding on a raw sample
 * would let one transient trip it.
 *
 * ref5V_Level is deliberately NOT averaged here - :542-543 is a plain store,
 * exactly as in batteryNow. It is a stable divider off +5 V with nothing to
 * smooth, and the host uses it to normalise BatteryLevel ratiometrically. */
void pmgr_battery_read(void)
{
    uint16_t sum = (uint16_t)pmgr_adc_read_battery() + s_battery_level;
    s_battery_level = (uint8_t)(sum >> 1);
    s_ref5v_level   = pmgr_adc_read_ref5v();
}

/* $F051 Battery_Command. Binary-verified opcodes, 2026-08-30. */
#define CMD_BATTERY_READ   0x68u   /* cached  - no conversion */
#define CMD_BATTERY_NOW    0x69u   /* forces batteryNow first */

void pmgr_cmd_battery(pmgr_packet_t *pkt)
{
    /* ⭐ Battery_Command, VERIFIED FROM THE BINARY at $F051, 2026-08-30:
     *
     *     F053  cmp cmd_batteryRead ($68)  ->  beq .batteryRead   CACHED
     *     F057  cmp cmd_batteryNow  ($69)  ->  bne .exit          SILENT
     *     F05B  jsr batteryNow                                    then read
     *
     * ⛔ We previously called batteryNow() for the WHOLE $6x group and replied
     * to all of it, on the grounds that the opcodes were unknown. They are
     * known now: only $68 and $69 do anything, and ONLY $69 forces a
     * conversion. Any other $6x returns in silence. */
    if (pkt->command == CMD_BATTERY_NOW) {
        pmgr_battery_now();                      /* F05B - $69 only */
    } else if (pkt->command != CMD_BATTERY_READ) {
        return;                                  /* F059 bne .exit - silent */
    }

    /* ⭐ §5: the level reported to the Mac is CONVERTED BACK to the nominal
     * scale, so the host's own battery indicator agrees with what charging
     * decides. ⛔ Without this the Mac sees this unit's shifted numbers and its
     * warnings fire at the wrong voltages - which is exactly what 2.361
     * observed. Identity with no valid record. */
    uint8_t reply[3] = { s_power_flags,
                         pmgr_calib_to_nominal(s_battery_level),
                         s_ref5v_level };

    /* pmgr.asm 1284: bcs .exit - if the transfer FAILED the host never received
     * the data, so the flags must survive for the next attempt. */
    if (!pmgr_return_data(pkt->command, reply, sizeof reply))
        return;

    /* 1285: bbs 1,$0,.exit - do NOT clear the flags while an interrupt is
     * still unacknowledged, or the host loses the event. */
    if (pmgr_int_pending())
        return;

    /* 1286-1288: clb 2,IntFlags / clb 1,IntFlags / clb charger_changed */
    pmgr_clear_int_flags((uint8_t)(PMGR_INT_BAT2_bm | PMGR_INT_BATTERY_bm));
    s_power_flags &= (uint8_t)~PF_CHARGER_CHANGED;
}

/* --- $78 read interrupt data --------------------------------------------- */
void pmgr_cmd_read_int(pmgr_packet_t *pkt)
{
    uint8_t flags = pmgr_int_flags();

    /* NOTE the asymmetry with Battery_Command: readINT has NO  bcs  after
     * ReturnDataToHost (pmgr.asm 1298-1300). It clears bit 3 and the $0 latch
     * unconditionally, even on a failed transfer. That is Apple's behaviour,
     * observed not inferred - do not "fix" it by adding a check. */
    (void)pmgr_return_data(pkt->command, &flags, 1);
    /* pmgr.asm 1299: clb 3,IntFlags - bit 3 ONLY. Bits 0-2 stay set until the
     * handler that owns each one services it. Clearing them here would drop
     * pending ADB and battery events on the floor. */
    pmgr_clear_int_flags(PMGR_INT_READ_bm);
    pmgr_ack_int();                      /* pmgr.asm 1300: clb 1,$0 */
}

/* --- ADB ------------------------------------------------------------------
 * ADB is ASYNCHRONOUS. $2x queues; the transaction runs at the 60 Hz service
 * point; ADBInt then tells the host to collect the result with $28.
 * See pmgr_adb.h and ADB-TIMING.md. */

/* ADB_Command, pmgr.asm 1004-1035. The host payload is
 *     data[0] ADB command   -> $13 -> LastADBCMD
 *     data[1] ADB status    -> $14 -> ADBStatus
 *     data[2] data length   -> $15 -> loop counter
 *     data[3..]             -> $16.. -> buffer at $8..
 */
void pmgr_cmd_adb(pmgr_packet_t *pkt)
{
    if (pkt->command == ADB_CMD_PMGRADBOFF) {
        pmgr_adb_off();                              /* ldm #1,ADBStatus  */
        pmgr_clear_int_flags(PMGR_INT_ADB_bm);       /* clb ADBInt        */
        return;
    }
    if (pkt->command != ADB_CMD_PMGRADB) return;     /* .bad_command: sec */

    pmgr_clear_int_flags(PMGR_INT_ADB_bm);           /* pmgr.asm 1017 */

    uint8_t cmd = (pkt->length > 0u) ? pkt->data[0] : 0u;
    uint8_t st  = (pkt->length > 1u) ? pkt->data[1] : 0u;
    uint8_t n   = (pkt->length > 2u) ? pkt->data[2] : 0u;

    /* The original trusts $15 blindly (ldx $15 / bne loop). We cannot - a
     * malformed packet would walk off the end of pkt->data. */
    uint8_t avail = (pkt->length > 3u) ? (uint8_t)(pkt->length - 3u) : 0u;
    if (n > avail) n = avail;

    pmgr_adb_queue(cmd, st, (n != 0u) ? &pkt->data[3] : 0, n);
}

/* readADB, pmgr.asm 991-1002. WriteLocation = $0005 and ByteCount =
 * ADBCMDLength + 3, so ReturnDataToHost2 walks up from $5:
 *     LastADBCMD, ADBStatus, ADBCMDLength, then the data bytes.
 * THIS is how the Macintosh retrieves keyboard and mouse data. */
void pmgr_cmd_read_adb(pmgr_packet_t *pkt)
{
    uint8_t reply[3u + ADB_RX_MAX];
    uint8_t n = pmgr_adb_reply(reply);

    if (!pmgr_return_data(pkt->command, reply, n)) return;   /* bcs .exit */
    if (pmgr_int_pending()) return;                          /* bbs 1,$0,.exit */
    pmgr_clear_int_flags(PMGR_INT_ADB_bm);                   /* clb ADBInt */
}

/* .ADBAction, pmgr.asm 255-292. Called from the 60 Hz tick. The ADBInt guard
 * is Apple's: bbs ADBInt,IntFlags,.noADBAction - never start a new
 * transaction while the host still has an uncollected result. */
void pmgr_adb_poll(void)
{
    if (pmgr_int_flags() & PMGR_INT_ADB_bm) return;
    if (!pmgr_adb_pending()) return;

    if (pmgr_adb_service()) pmgr_set_int_flag(PMGR_INT_ADB_bm);
}

/* --- groups still to implement ------------------------------------------- */
/* --- Time / PRAM ($3x) -----------------------------------------------------
 * Literal translation of Time_PRAM_Command (pmgr.asm 1040-1121).
 *
 * The dispatch is a fall-through chain, not a switch: readBit picks the half,
 * then two compares, and ANYTHING ELSE lands on the xPram variant. Reproduced
 * exactly - a $3C write, say, is an xPramWrite to the original and must be
 * here too.
 *
 * The original overrides the received ByteCount (`ldm #2,ByteCount / jsr
 * WriteToLocation`) and reads its operands inline from the bus. Our packet
 * layer has already buffered the whole payload, so the same bytes arrive in
 * pkt->data and the effect on the wire is identical.
 *
 * Verified against a real boot capture: `3A 02 46 01` -> `3A 01 00`
 * (xPRAM read, location $46, 1 byte). See MEASUREMENTS.md 2.9. */
void pmgr_cmd_time_pram(pmgr_packet_t *pkt)
{
    const uint8_t c = pkt->command;
    uint8_t i;

    if (!PMGR_CMD_ISREAD(c)) {
        /* ---------------- write path ---------------- */
        if (c == CMD_TIME_WRITE) {                    /* .timeWrite */
            if (pkt->length < 4u) return;
            /* 4 bytes to $25, MSB first: timeD timeC timeB timeA */
            /* :1050-1051  jsr CombineTime / sta Time is now done INSIDE
             * pmgr_time_set(), so the checksum can never be left stale by a
             * caller that forgets it - and a stale checksum would make the
             * next reset discard a perfectly good clock. */
            pmgr_time_set(((uint32_t)pkt->data[0] << 24) |
                          ((uint32_t)pkt->data[1] << 16) |
                          ((uint32_t)pkt->data[2] <<  8) |
                           (uint32_t)pkt->data[3]);
            return;
        }
        if (c == CMD_PRAM_WRITE) {                    /* .pramWrite */
            if (pkt->length < (PRAM_LEN_3F + PRAM_LEN_37)) return;
            for (i = 0; i < PRAM_LEN_3F; i++)         /* 16 bytes -> $3F */
                pmgr_pram_write((uint8_t)(PRAM_OFF_3F + i), pkt->data[i]);
            for (i = 0; i < PRAM_LEN_37; i++)         /* then 4 -> $37 */
                pmgr_pram_write((uint8_t)(PRAM_OFF_37 + i), pkt->data[PRAM_LEN_3F + i]);
            pmgr_pram_commit();                       /* FUN_EF84 / sta $AF */
            return;
        }
        /* .xPramWrite - the fall-through case */
        if (pkt->length < 2u) return;
        {
            const uint8_t loc = pkt->data[0];         /* $13, then adc #$2F */
            uint8_t len = pkt->data[1];               /* $14 -> ByteCount */
            if ((uint16_t)len + 2u > pkt->length) len = (uint8_t)(pkt->length - 2u);
            for (i = 0; i < len; i++)
                pmgr_pram_write((uint8_t)(loc + i), pkt->data[2u + i]);
            pmgr_pram_commit();
        }
        return;
    }

    /* ---------------- read path ---------------- */
    if (c == CMD_TIME_READ) {                         /* .timeRead */
        const uint32_t t = pmgr_time_get();
        const uint8_t r[4] = { (uint8_t)(t >> 24), (uint8_t)(t >> 16),
                               (uint8_t)(t >>  8), (uint8_t)t };
        (void)pmgr_return_data(c, r, 4);              /* 4 bytes from timeD */
        return;
    }
    if (c == CMD_PRAM_READ) {                         /* .pramRead */
        /* The original hand-rolls this - SendByte the command, SendByte $14
         * (=20), then two ReturnDataLoops - because the source bytes are not
         * contiguous. Same bytes on the wire from one assembled buffer. */
        uint8_t r[PRAM_LEN_3F + PRAM_LEN_37];
        for (i = 0; i < PRAM_LEN_3F; i++) r[i] = pmgr_pram_read((uint8_t)(PRAM_OFF_3F + i));
        for (i = 0; i < PRAM_LEN_37; i++) r[PRAM_LEN_3F + i] = pmgr_pram_read((uint8_t)(PRAM_OFF_37 + i));
        (void)pmgr_return_data(c, r, sizeof r);
        return;
    }
    /* .xPramRead - the fall-through case */
    if (pkt->length < 2u) return;
    {
        const uint8_t loc = pkt->data[0];
        uint8_t len = pkt->data[1];
        uint8_t r[PMGR_MAX_PAYLOAD];
        if (len > PMGR_MAX_PAYLOAD) len = PMGR_MAX_PAYLOAD;
        for (i = 0; i < len; i++) r[i] = pmgr_pram_read((uint8_t)(loc + i));
        (void)pmgr_return_data(c, r, len);
    }
}
/* Contrast / PMGR_PWM.  ⭐ MEASURED 2026-08-28 - MEASUREMENTS.md 2.15.
 *
 *   PWM period : 1074 us  (931 Hz), CONSTANT across all contrast settings
 *   duty       : ContrastTable[X] / 66,  X = host byte & 0x1F
 *   table      : 2, 4, 6, ... i.e. 2 + 2*X, giving 2..64 of 66 = 3.0%..97.0%
 *
 * From pmgr.asm:1177-1189 - SetContrast writes BOTH timer halves,
 * Timer_1 = table[X] and Timer_2 = 66 - table[X], so the period is fixed at
 * 66 counts and only the duty moves.
 *
 * The 1074 us comes from 66 counts x prescaler 16 x 1.01723 us. Note the
 * M50753's Timer_1_2_Prescaler value of 4 divides by 2^4 = 16, NOT by 4 -
 * that was ambiguous in the datasheet and was settled by this measurement
 * (predicted 1074.19 us, measured 1074.00, 0.02%).
 *
 * Reproduce the FREQUENCY and DUTY, not the original's register values -
 * same principle as PMGR_ONESEC_PULSE_US and PMGR_INT_PULSE_US.
 *
 * GetContrast (readBit set) returns the stored value from $2E - pmgr.asm:1166.
 *
 * IMPLEMENTED in pmgr_pwm.c. Note PD6 has no timer output of its own, so the
 * waveform reaches it as TCA0 -> CCL LUT2 -> PD6; see pmgr_pwm.h. */

/* Contrast_Command - pmgr.asm 1164-1189.
 *
 *   1165  bbc readBit,CommandByte,.SetContrastReceive
 *   1167  .GetContrast   lda $2E / sta $13 / ByteCount 1 / ReturnDataToHost
 *   1173  .SetContrastReceive  ByteCount 1 / WriteToLocation
 *   1175  ;bra SetContrast      - falls THROUGH into SetContrast
 *
 * Read returns the stored setting ($2E), not the duty. Write takes one byte
 * and applies it; no reply, like the other write paths.
 *
 * Duty and frequency are measured, not guessed - MEASUREMENTS.md 2.15. */
void pmgr_cmd_contrast(pmgr_packet_t *pkt)
{
    if (PMGR_CMD_ISREAD(pkt->command)) {                /* 1166 .GetContrast */
        uint8_t a = pmgr_contrast_get();
        (void)pmgr_return_data(pkt->command, &a, 1);
        return;
    }

    if (pkt->length < 1u) return;                       /* 1173 ByteCount 1 */
    pmgr_contrast_set(pkt->data[0]);                    /* 1177 SetContrast */
}
/* ---- Modem_Command - pmgr.asm 1224-1268 -------------------------------- */

/* ⚠️⚠️ CLASS D - THE WEAKEST EVIDENCE IN THIS FILE. READ BEFORE TRUSTING.
 *
 * NONE of these six payload bit names is equated anywhere in pmgr.asm. They
 * are referenced and never defined, like cmd_sleepReq and cmd_timerSet - but
 * unlike those, there are SIX of them and one of them switches MODEM_PWR.
 *
 * The ordering below is inferred from the sequence the READ path builds them
 * in (:1248-1264), which is the only ordering evidence that exists:
 *
 *      1249 ModemPwr   1252 ModemAorB   1255 RingWakeEnable
 *      1258 ModemInstalled   1261 RingDetect   1264 ModemHook
 *
 * Source order is a WEAK proxy for bit number. It happens to be right often
 * enough to be tempting and there is no guarantee here.
 *
 * WHAT IS SAFE ANYWAY: read and write use the same names, so whatever
 * numbering we pick is SELF-CONSISTENT - a host that writes bit N reads bit N
 * back. The exposure is only to the real Mac ROM using different positions,
 * and then only for the three bits the WRITE path acts on (ModemPwr,
 * ModemAorB, RingWakeEnable).
 *
 * TO CONFIRM: one captured $5x exchange from a real machine settles all six at
 * once - exactly how 2.9 confirmed $3A. Until then treat modem behaviour as
 * unverified, and suspect this table first if it misbehaves. */
#define MODEM_B_PWR         0x01u   /* 1249 - inferred bit 0 */
#define MODEM_B_AORB        0x02u   /* 1252 - inferred bit 1 */
#define MODEM_B_RINGWAKE    0x04u   /* 1255 - inferred bit 2 */
#define MODEM_B_INSTALLED   0x08u   /* 1258 - inferred bit 3, read-only */
#define MODEM_B_RINGDETECT  0x10u   /* 1261 - inferred bit 4, read-only */
#define MODEM_B_HOOK        0x20u   /* 1264 - inferred bit 5, read-only */

/* $0 bit 7. Also a WAKE SOURCE: :1371-1372 wakes when this is set and
 * RING_DETECT reads LOW. */
static bool s_ring_wake_on;

bool pmgr_ring_wake_armed(void) { return s_ring_wake_on; }

void pmgr_cmd_modem(pmgr_packet_t *pkt)
{
    if (PMGR_CMD_ISREAD(pkt->command)) {                 /* 1225 .modemRead */
        uint8_t a = 0;                                   /* 1247 ldm #0,$13 */

        /* Four of the six are INVERTED - each is a `bbs ... skip the seb`, so
         * the reply bit is set when the pin reads LOW. Three of those are
         * genuinely active-low signals (MODEM_PWR*, MODEM_INS*, OFF_HOOK*), so
         * the reply reports the SENSE, not the pin: "powered", "installed",
         * "off hook". RING_DETECT carries no _N in our pin map yet is treated
         * the same way here - consistent with :1372, where a LOW RING_DETECT
         * is what wakes the machine. */
        /* MODEM_PWR* is an OUTPUT, so read the latch, as the original's
         * `bbs MODEM_PWR,Port_P0` does. Latch LOW = modem powered. */
        if ((MODEM_PWR_N_PORT.OUT & MODEM_PWR_N_bm) == 0u)
            a |= MODEM_B_PWR;                            /* 1248-1249 */
        if ((MODEM_AB_PORT.OUT & MODEM_AB_bm) != 0u)
            a |= MODEM_B_AORB;                           /* 1251-1252 */
        if (s_ring_wake_on)
            a |= MODEM_B_RINGWAKE;                       /* 1254-1255 */
        if (PIN_IS_LOW(MODEM_INS_N_PORT, MODEM_INS_N_bm))
            a |= MODEM_B_INSTALLED;                      /* 1257-1258 */
        if (PIN_IS_LOW(RING_DETECT_PORT, RING_DETECT_bm))
            a |= MODEM_B_RINGDETECT;                     /* 1260-1261 */
        if (PIN_IS_LOW(OFF_HOOK_PORT, OFF_HOOK_bm))
            a |= MODEM_B_HOOK;                           /* 1263-1264 */

        (void)pmgr_return_data(pkt->command, &a, 1);     /* 1266-1267 */
        return;
    }

    if (pkt->length < 1u) return;                        /* 1226 ByteCount 1 */
    const uint8_t d = pkt->data[0];

    s_ring_wake_on = (d & MODEM_B_RINGWAKE) != 0u;       /* 1228-1230 */

    if (d & MODEM_B_AORB) MODEM_AB_PORT.OUTSET = MODEM_AB_bm;   /* 1233 */
    else                  MODEM_AB_PORT.OUTCLR = MODEM_AB_bm;   /* 1236 */

    /* MODEM_PWR* is ACTIVE LOW: the original clears the bit to power the modem
     * ON (:1239) and sets it to power OFF (:1243). Inverting this would leave
     * the modem energised whenever the host asked for it to be off. */
    if (d & MODEM_B_PWR) ASSERT_LOW(MODEM_PWR_N_PORT, MODEM_PWR_N_bm);      /* on  */
    else                 DEASSERT_HIGH(MODEM_PWR_N_PORT, MODEM_PWR_N_bm);   /* off */
    /* 1240/1244  clc / rts - the write path sends no reply. */
}
/* Sleep_Command, pmgr.asm 1303-1327.
 *
 * ⭐ SLEEP IS PASSWORD-PROTECTED. The host must supply four payload bytes
 * matching the literal "MATT" (`string_Matt`, pmgr.asm:121-122) or the PMGR
 * refuses to go down:
 *
 *   1307  ldm #4,ByteCount          ; take exactly 4 bytes
 *   1309  ldm #$70,CommandByte      ; provisionally OK
 *   1310  ldx #4
 *   1311 .CompareLoop
 *   1312  dex / bmi .LAB_F0A3       ; all four matched -> keep $70
 *   1314  lda string_Matt,X
 *   1315  cmp $13,X
 *   1316  beq .CompareLoop
 *   1317  ldm #$AA,CommandByte      ; ANY mismatch -> $AA, and no sleep
 *   1319  ldm #0,ByteCount
 *   1320  jsr ReturnDataToHost2     ; reply goes out FIRST
 *   1324  cmp #$70 / beq Sleep
 *
 * The reply must precede the sleep: once SYS_RST* is asserted the host is held
 * in reset and could never receive it.
 *
 * The reply carries the VERDICT in its command byte - $70 accepted, $AA
 * rejected - with a zero-length payload either way.
 *
 * ⚠️ `cmd_sleepReq` (:1305) is referenced but never defined in the listing,
 * ⭐ `cmd_sleepReq` = 0x7F - MEASURED 2026-08-28, MEASUREMENTS.md 2.16. It is
 * referenced but never equated in the listing, and an earlier revision here
 * guessed 0x70 and then, worse, accepted ANY $7x rather than checking. The
 * captured exchange settles it outright:
 *
 *     7F 04 4D 41 54 54    host: cmd 0x7F, count 4, payload "MATT"
 *     70 00                PMGR: verdict 0x70 (accepted), count 0
 *
 * Note 0x7F and 0x70 are BOTH real and NOT the same thing: 0x7F is the opcode
 * the host sends, 0x70 is the verdict byte the reply carries (:1309). */
#define CMD_SLEEP_REQ  0x7Fu

void pmgr_cmd_sleep(pmgr_packet_t *pkt)
{
    static const uint8_t magic[4] = { 'M', 'A', 'T', 'T' };
    uint8_t verdict = 0x70u;                    /* 1309 ldm #$70,CommandByte */

    /* 1304-1306  lda CommandByte / cmp cmd_sleepReq / bne .fail - any other
     * $7x fails outright, before the payload is even read. */
    if (pkt->command != CMD_SLEEP_REQ) {
        /* ⛔ SILENCE, NOT $AA. `bne .fail` jumps PAST ReturnDataToHost2
         * entirely - `.fail` is just `sec / rts`. The host gets NO REPLY.
         *
         * We used to answer $AA here. $AA is the right verdict for a $7F whose
         * MATT payload is wrong (that path falls through .LAB_F0A3 and DOES
         * reply), but a non-$7F command in the group never reaches the reply
         * at all. Answering it invents a response the original never sends. */
        return;
    }
    if (pkt->length != 4u) {
        verdict = 0xAAu;                        /* wrong length cannot match */
    } else {
        for (uint8_t i = 0; i < 4u; i++) {
            if (pkt->data[i] != magic[i]) {     /* 1317 */
                verdict = 0xAAu;
                break;
            }
        }
    }

    /* 1319-1320  reply with the verdict, zero payload, BEFORE going down.
     * A one-byte dummy rather than NULL: length 0 means the buffer is never
     * read, and this file has no <stddef.h> in its include set. */
    const uint8_t none = 0;
    bool sent = pmgr_return_data(verdict, &none, 0);

    /* 1321  bcs .fail - if the reply did not get out, do NOT sleep. Going
     * down after a failed handshake would strand the host with the machine
     * held in reset and no way to have learned why.
     * 1322-1324  and only $70 sleeps. */
    if (sent && verdict == 0x70u)
        pmgr_sleep();                 /* parks; returns once awake again */
}
/* ---- wake-up timer state ($2A..$2D and two bits of $0) ------------------ */

/* MSB-first, like the main clock. ⭐ Establishing that took the COMPARISON
 * code, not the read/write paths:
 *
 *      599  lda $2D / cmp timeA        ; $2D has timeA's significance = LSB
 *      608  lda $2A / cmp timeD        ; $2A has timeD's significance = MSB
 *
 * so the disassembler's wakeupTimeA..D labels are POSITIONAL ($2A..$2D), not
 * by significance - the opposite convention to timeA..timeD, where timeA is
 * the LSB at the HIGHEST address ($28) and timeD the MSB at $25.
 *
 * Two things fall out. `ldm #wakeupTimeA,WriteLocation` + WriteBytesRAM (which
 * ascends - `iny` at :318) writes $2A..$2D, which is correct; read as
 * "A = LSB" it would have walked into contrast at $2E and PRAM at $2F. And the
 * wire order is MSB-first for BOTH the clock and the wake timer, not opposite
 * as the label names suggest. */
static uint32_t s_wakeup_time;      /* $2A..$2D */
static bool     s_wake_time_on;     /* $0 bit 4 - wake_time_on, armed      */
static bool     s_wake_time_hit;    /* $0 bit 5 - the time has been reached */

bool pmgr_wake_timer_expired(void) { return s_wake_time_hit; }
void pmgr_wake_timer_ack(void)     { s_wake_time_hit = false; }  /* :1376 clb 5,$0 */
void pmgr_wake_timer_disarm(void)  { s_wake_time_on  = false; }  /* :1377 clb wake_time_on */

/* pmgr.asm 596-610, inside the 1 Hz tick. Only meaningful while asleep: $0
 * bit 6 is set on sleep entry (:1356) and cleared on wake (:1380). */
void pmgr_wake_timer_tick(bool asleep)
{
    /* `s_wake_time_hit` is cleared at the top of every tick (see tick_core),
     * so on any given tick it is already false here. The test is kept because
     * the original's structure implies set-once WITHIN a tick, and it costs
     * nothing. */
    if (!s_wake_time_on || !asleep || s_wake_time_hit)
        return;

    if (pmgr_time_get() == s_wakeup_time)
        s_wake_time_hit = true;                          /* :610 seb 5,$0 */
}

/* Timer_Command - pmgr.asm 1412-1444.
 *
 *   1414  cmp cmd_timerSet        -> take 4 bytes, arm      (:1418-1422)
 *   1425  cmp cmd_disableWakeUp   -> disarm                 (:1427)
 *   1429  anything else           -> read back 5 bytes
 *
 * Dispatch is on the WHOLE command byte, not on readBit - which is why the
 * `bbs readBit,CommandByte,.timerRead` at :1417 is dead code, and the listing
 * says so ("Useless check"): reaching it means CommandByte already equalled
 * cmd_timerSet exactly, so readBit is known. Not reproduced.
 *
 * ⚠️ CLASS D: `cmd_timerSet` and `cmd_disableWakeUp` are referenced but never
 * equated, exactly like cmd_sleepReq and cmd_timeWrite. The VALUES below are
 * inferred from the $8x group with readBit = bit 3; the STRUCTURE around them
 * is Class A. A single captured $8x exchange would confirm them, the way 2.9
 * confirmed $3A. Until then, read is the fallback for any unrecognised $8x,
 * which is what the original does anyway. */
#define CMD_TIMER_SET      0x80u
/* ⭐ $82 - VERIFIED IN THE BINARY, 2026-08-30. `pmuv1.bin` (CRC32 01DAE148,
 * SHA1 29D2FC...) contains `C9 82` = `CMP #$82` at $F162.
 *
 * ⛔ THIS WAS $81 AND THAT WAS WRONG. An $81 command falls through to the
 * TIMER-READ path on original silicon, so we answered a read where the host
 * expected a disable.
 *
 * ⚠️ MEASUREMENTS.md 2.31 recorded a single `81 00` capture as "corroborated"
 * evidence for $81. It was a FALSE POSITIVE - $81 is emitted constantly as
 * Power_Command disk-poll payload, and one observation plus a plausible shape
 * is not evidence. The binary settles it. */
#define CMD_TIMER_DISABLE  0x82u

void pmgr_cmd_timer(pmgr_packet_t *pkt)
{
    /* ✅ cmd_timerSet = $80 - MEASURED, MEASUREMENTS.md 2.32. Captured as
     * `80 04 00 00 32 64` with ByteCount 4, found structurally (an $8x byte
     * followed by count 4) because $80 also occurs constantly as Power_Command
     * disk-poll payload and cannot be isolated by value.
     * Payload byte order MSB-first is confirmed by the same capture. */
    if (pkt->command == CMD_TIMER_SET) {                 /* 1416 .timerSet */
        if (pkt->length < 4u) return;                    /* 1418 ByteCount 4 */
        s_wakeup_time = ((uint32_t)pkt->data[0] << 24) | /* $2A = MSB */
                        ((uint32_t)pkt->data[1] << 16) |
                        ((uint32_t)pkt->data[2] <<  8) |
                         (uint32_t)pkt->data[3];         /* $2D = LSB */
        s_wake_time_on  = true;                          /* 1422 seb wake_time_on */
        s_wake_time_hit = false;
        return;                                          /* 1423 rts - no reply */
    }

    if (pkt->command == CMD_TIMER_DISABLE) {             /* 1424 .disableWakeUp */
        s_wake_time_on = false;                          /* 1427 clb wake_time_on */
        return;                                          /* 1428 rts - no reply */
    }

    /* 1429 .timerRead - 4 time bytes then a flag byte (1430 ByteCount 5). */
    uint8_t reply[5];
    reply[0] = (uint8_t)(s_wakeup_time >> 24);           /* 1431 $13 */
    reply[1] = (uint8_t)(s_wakeup_time >> 16);           /* 1433 $14 */
    reply[2] = (uint8_t)(s_wakeup_time >>  8);           /* 1435 $15 */
    reply[3] = (uint8_t)(s_wakeup_time);                 /* 1437 $16 */
    reply[4] = s_wake_time_on ? 0x01u : 0x00u;           /* 1439-1441 $17 bit 0 */
    (void)pmgr_return_data(pkt->command, reply, sizeof reply);
}
/* Sound_Command - pmgr.asm 1446-1471.
 *
 * WRITE (one payload byte):
 *   1450  bbc 0,$13,.TurnSoundOn   ; bit 0 mirrors straight onto SOUND_OFF
 *   1451  seb SOUND_OFF,Port_P3    ;   set   -> muted
 *   1454  clb SOUND_OFF,Port_P3    ;   clear -> unmuted
 *   1456  bbc 1,$13,.exit          ; bit 1 -> pulse SOUND_PWR, else nothing
 *   1457  seb SOUND_PWR,Port_P0
 *   1458  clb SOUND_PWR,Port_P0
 *   1460  rts                      ; NO reply on the write path
 *
 * READ:
 *   1462  ldm #0,$13
 *   1463  bbc SOUND_OFF,Port_P3,.LAB_F1AA / 1464 seb 0,$13
 *   1466  bbs SOUND_LATCH,In_Reg,.LAB_F1AF / 1467 seb 1,$13
 *   1469  one byte back
 *
 * ⭐ Note the asymmetry: bit 0 reads back the SOUND_OFF latch directly, but
 * bit 1 reports SOUND_LATCH **INVERTED** - `bbs ... skip the seb` means the
 * reply bit is set when the input is LOW. Bit 1 is also not a read-back of
 * what bit 1 wrote: writing it fires a one-shot pulse, while reading it
 * samples an unrelated input. Easy to "tidy" into a mirror and be wrong.
 *
 * SOUND_OFF is read from OUT, not IN: it is an output, and the original's
 * `bbc SOUND_OFF,Port_P3` reads the port latch for output pins.
 *
 * ⚠️ SOUND_LATCH is PC0, which is MVIO on the AVR-DB (see
 * MCU-BOARD-SCHEMATIC.md). PORTC reads are only meaningful when VDDIO2 is
 * powered; with it unpowered this bit is not trustworthy. */
void pmgr_cmd_sound(pmgr_packet_t *pkt)
{
    if (PMGR_CMD_ISREAD(pkt->command)) {                 /* 1447 .soundRead */
        uint8_t a = 0;
        if ((SOUND_OFF_PORT.OUT & SOUND_OFF_bm) != 0u)
            a |= 0x01u;                                  /* 1464 */
        if (PIN_IS_LOW(SOUND_LATCH_PORT, SOUND_LATCH_bm))
            a |= 0x02u;                                  /* 1467 - INVERTED */
        (void)pmgr_return_data(pkt->command, &a, 1);     /* 1469-1470 */
        return;
    }

    if (pkt->length < 1u) return;                        /* 1448 ldm #1 */
    const uint8_t d = pkt->data[0];

    if (d & 0x01u) SOUND_OFF_PORT.OUTSET = SOUND_OFF_bm; /* 1451 */
    else           SOUND_OFF_PORT.OUTCLR = SOUND_OFF_bm; /* 1454 */

    if (d & 0x02u) {                                     /* 1456 */
        SOUND_PWR_N_PORT.OUTSET = SOUND_PWR_N_bm;        /* 1457 */
        SOUND_PWR_N_PORT.OUTCLR = SOUND_PWR_N_bm;        /* 1458 */
    }
    /* 1460 rts - the write path sends no reply. */
}

/* ===========================================================================
 * VIRTUAL M50753 ADDRESS SPACE  --  $E0 write / $E8 read
 *
 * `.LAB_F200` uses a real 16-bit pointer (`lda ($1B),Y`), so the host can reach
 * RAM, I/O AND the program ROM at $E800-$FFFF. This is the abstraction those
 * two commands go through.
 *
 * ⛔ DO NOT MAP ONTO AVR SRAM. Our memory map has nothing in common with an
 * M50753's; a naive mapping hands the host meaningless bytes while appearing to
 * work, which is worse than replying nothing.
 *
 * Addresses below are from the equates in PMGR_VERIFIED_LISTING_2026-08-30.txt
 * and, where an equate was absent, from instruction operands in the binary:
 *
 *     Port_P0    $E0   `85 E0 sta Port_P0`   at $E8A8
 *     BatteryLevel $1E `A5 1E lda ...`       at $EAAD
 *     timeD      $25   `C5 25 cmp timeD`     at $EBE2
 *
 * The `$24..$2D` zeroing loop at ResetEntry confirms the block layout:
 *     $24 divider · $25-$28 time D,C,B,A · $29 Time checksum · $2A-$2D wakeup
 * ======================================================================== */

/* ⭐⭐ EVERY THRESHOLD COMPARISON GOES THROUGH HERE - design/HYBRID-CALIBRATION.md
 * §5: "applied to EVERY threshold comparison (LOW, STAY_ASLEEP, DEAD, the knee,
 * HICHGLevel) AND to the level reported to the Mac".
 *
 * ⛔ 2.350 CORRECTED ONLY THE KNEE. LOW, STAY_ASLEEP, DEAD and the state-machine
 * reset kept comparing against Apple's raw constants, so on a calibrated board
 * they fired at THIS UNIT'S shifted voltages, not the nominal ones. Found by
 * review 2026-09-28 (MEASUREMENTS 2.362), and visible in 2.361's own data: the
 * reserve warning appeared near 6.18 V, which is the SHIFTED low threshold -
 * evidence the correction was NOT being applied, not that it was.
 *
 * One helper, used everywhere, so no site can drift from another. It is the
 * IDENTITY with no valid record, so an uncalibrated board is bit-for-bit as
 * before. `nominal_raw` is Apple's constant + 512. */
static bool level_below(uint8_t level, uint16_t nominal_raw)
{
    return (uint16_t)(level + 512u) < pmgr_calib_threshold(nominal_raw);
}

uint8_t pmgr_mem_read(uint16_t addr)
{
    switch (addr) {
    /* ---- zero page: live PMGR state at its AUTHENTIC addresses ---------- */
    case 0x0000u: {                    /* $0 flag byte */
        uint8_t v = 0;
        if (s_wake_time_on)  v |= (uint8_t)(1u << 4);   /* wake_time_on  */
        if (s_wake_time_hit) v |= (uint8_t)(1u << 5);
        if (s_asleep)        v |= (uint8_t)(1u << 6);
        if (pmgr_ring_wake_armed()) v |= (uint8_t)(1u << 7);
        if (pmgr_int_pending())     v |= (uint8_t)(1u << 1);
        return v;
    }
    case 0x0001u: return pmgr_int_flags();          /* IntFlags     */
    case 0x0006u: return pmgr_adb_status();         /* ADBStatus    */
    case 0x001Du: return s_power_flags;             /* PowerFlags   */
    case 0x001Eu: return s_battery_level;           /* BatteryLevel */
    case 0x001Fu: return s_ref5v_level;             /* ref5V_Level  */
    /* $20 - the COMPUTED stay-asleep midpoint, not a stored constant:
     *   :438-445  $20 = (lowBatteryLevel>>1) + (deadBatteryLevel>>1) = 70
     * It appears in no equate because the original derives it at runtime. We
     * hold it as PMGR_ADC_STAY_ASLEEP_LEVEL; same value, same meaning. */
    case 0x0020u: return PMGR_ADC_STAY_ASLEEP_LEVEL;

    /* ⭐ $21/$22/$23 - the HICHG state machine, mapped 2026-08-31 once the
     * state actually existed. Resolved by tracing every use site in the binary
     * ($EAE9-$EB38): $21 is the state (0 idle, 1 charging to the knee, 2
     * top-off, 3 done) and $22/$23 are its 16-bit counter, low byte first.
     *
     * These were deliberately UNMAPPED while the machine was unimplemented -
     * exposing them then would have meant inventing the variables. */
    case 0x0021u: return s_hichg_state;
    case 0x0022u: return s_hichg_lo;
    case 0x0023u: return s_hichg_hi;

    case 0x0024u: return s_sec_divider;             /* /60 divider  */

    case 0x0025u: case 0x0026u: case 0x0027u: case 0x0028u: {
        /* time D,C,B,A - D is the MSB and lives at the LOWEST address */
        uint32_t t = pmgr_time_get();
        return (uint8_t)(t >> (8u * (addr - 0x0025u)));
    }
    case 0x002Au: case 0x002Bu: case 0x002Cu: case 0x002Du:
        return (uint8_t)(s_wakeup_time >> (8u * (3u - (addr - 0x002Au))));

    case 0x002Eu: return pmgr_contrast_get();       /* $2E contrast */

    case 0x0071u: return PMGR_ADC_LOW_LEVEL;        /* lowBatteryLevel  */
    case 0x0072u: return PMGR_ADC_DEAD_LEVEL;       /* deadBatteryLevel */
    case 0x0073u: return PMGR_ADC_HICHG_LEVEL;      /* HICHGLevel       */

    /* ---- I/O registers -------------------------------------------------- */
    case 0x00E0u: return p0_read();                 /* Port_P0 */

    /* Port_P3 ($E8) - handshake and system control. Bit assignments confirmed
     * against Apple sheet 12 (PMGR-PROTOCOL-REFERENCE.md):
     *
     *   0 RESET*  1 SYS_RST*  2 VIA_TEST  3 SOUND_OFF
     *   4 1SEC*   5 PMINT*    6 PMACK*    7 PMREQ*
     *
     * Reconstructed from the AVR pins that carry those same signals, read via
     * the IN registers so a driven output reports its ACTUAL level rather than
     * what we last wrote. Every bit has a real signal behind it - this is a
     * truthful view, not a plausible one. */
    /* Port_P1 ($E2) - status inputs plus four PMGR-driven lines. Bit numbers
     * are EQUATES IN THE VERIFIED LISTING, not inference:
     *   0 NC  1 AKD  2 STOP_CLK  3 CHRG_ON*  4 KBD_RST*
     *   5 HICHG  6 RING_DETECT  7 MODEM_A/B
     * NC is "not connected, probably for testing" (listing:111) - reported 0. */
    case 0x00E2u: {
        uint8_t v = 0;
        if (!PIN_IS_LOW(AKD_PORT,         AKD_bm))         v |= (1u << 1);
        if ((STOP_CLK_PORT.IN & STOP_CLK_bm) != 0u)        v |= (1u << 2);
        if (!PIN_IS_LOW(CHRG_ON_N_PORT,   CHRG_ON_N_bm))   v |= (1u << 3);
        if (!PIN_IS_LOW(KBD_RST_N_PORT,   KBD_RST_N_bm))   v |= (1u << 4);
        if ((HICHG_PORT.IN & HICHG_bm) != 0u)              v |= (1u << 5);
        if (!PIN_IS_LOW(RING_DETECT_PORT, RING_DETECT_bm)) v |= (1u << 6);
        if ((MODEM_AB_PORT.IN & MODEM_AB_bm) != 0u)        v |= (1u << 7);
        return v;
    }

    /* Port_P4 ($EA) - FOUR BITS WIDE. Equates from the verified listing:
     *   0 ADB_Out  1 ADB_In  2 DISP_BLANK*  3 MODEM_INS*
     *
     * ⚠️ An external M50753 mapping claimed P4.2 = "system power control" and
     * P4.3 = unidentified. BOTH ARE WRONG for this firmware: P4.2 is
     * DISP_BLANK* and P4.3 is MODEM_INS*, and SYS_PWR is bit 7 of Port_P0 -
     * established from the binary-verified InitPorts value %10011111. Bits 4-7
     * do not exist on this port and report 0. */
    case 0x00EAu: {
        uint8_t v = 0;
        if ((PMGR_ADB_PORT.IN & PMGR_ADB_bm) != 0u)          v |= (1u << 0);
        if ((FDB_PORT.IN & FDB_bm) != 0u)                    v |= (1u << 1);
        if (!PIN_IS_LOW(DISP_BLANK_N_PORT, DISP_BLANK_N_bm)) v |= (1u << 2);
        if (!PIN_IS_LOW(MODEM_INS_N_PORT,  MODEM_INS_N_bm))  v |= (1u << 3);
        return v;
    }

    case 0x00E8u: {
        uint8_t v = 0;
        if (!PIN_IS_LOW(RESET_N_PORT,      RESET_N_bm))      v |= (1u << 0);
        if (!PIN_IS_LOW(SYS_RST_N_PORT,    SYS_RST_N_bm))    v |= (1u << 1);
        if (!PIN_IS_LOW(VIA_TEST_PORT,     VIA_TEST_bm))     v |= (1u << 2);
        if ((SOUND_OFF_PORT.IN & SOUND_OFF_bm) != 0u)        v |= (1u << 3);
        if (!PIN_IS_LOW(ONESEC_N_PORT,     ONESEC_N_bm))     v |= (1u << 4);
        if (!PIN_IS_LOW(PMINT_N_PORT,      PMINT_N_bm))      v |= (1u << 5);
        if (!PIN_IS_LOW(PMACK_N_PORT,      PMACK_N_bm))      v |= (1u << 6);
        if (!PIN_IS_LOW(PMREQ_N_PORT,      PMREQ_N_bm))      v |= (1u << 7);
        return v;
    }
    /* AD_Reg ($EF) - the LAST COMPLETED conversion, any channel.
     * ⛔ This returned s_battery_level, which is only conditionally truthful:
     * the register holds whichever channel converted most recently, and the
     * firmware converts ref5V as well as battery. */
    case 0x00EFu: return pmgr_adc_last_result();

    default:
        /* xPRAM $2F..$AE, checksum $AF */
        if (addr >= 0x002Fu && addr <= 0x00AEu)
            return pmgr_pram_read((uint8_t)(addr - 0x002Fu));
        if (addr == 0x00AFu) {
            bool ok;
            return pmgr_pram_checksum(&ok);
        }

        /* ⭐ $300/$301 - OURS, not the M50753's. §5's visible "uncalibrated"
         * flag and the pairing id of the record in force. Above the original's
         * map, so nothing is invented at an authentic address. */
        if (addr == PMGR_CALIB_STATUS_ADDR)  return pmgr_calib_status();
        if (addr == PMGR_CALIB_PAIRING_ADDR) return pmgr_calib_pairing();
        /* ⭐ $302 - the CHARGING fault, ours, outside the M50753's map (2.374).
         * bit 0: the ADC-scale interlock is currently holding fast charge off. */
        if (addr == 0x0302u) return s_hichg_faulted ? 0x01u : 0x00u;

        /* $E800-$FFFF - the program ROM the host can dump through $E8.
         * Synthetic in the public build, exact pmuv1.bin in the faithful one;
         * see src/pmgr_rom.c. */
        if (addr >= PMGR_ROM_BASE)
            return pmgr_rom_read(addr);

        return 0x00u;                   /* unmapped: reads 0 */
    }
}

void pmgr_mem_write(uint16_t addr, uint8_t value)
{
    /* ⭐ WRITES INTO THE ROM RANGE ARE IGNORED AND COMPLETE NORMALLY.
     *
     * The original executes the store; real ROM simply does not change, and the
     * command still returns success. It does NOT report an error, so neither do
     * we - reporting a failure the original never reports would send the host
     * down a recovery path that does not exist on real silicon. */
    if (addr >= 0xE800u) return;                    /* ignored, not failed */

    switch (addr) {
    case 0x001Du: s_power_flags   = value; return;
    case 0x001Eu: s_battery_level = value; return;
    case 0x001Fu: s_ref5v_level   = value; return;
    case 0x0024u: s_sec_divider   = value; return;
    case 0x002Eu: pmgr_contrast_set(value); return;
    case 0x00E0u: p0_write(value); return;
    default:
        if (addr >= 0x002Fu && addr <= 0x00AEu) {
            pmgr_pram_write((uint8_t)(addr - 0x002Fu), value);
            pmgr_pram_commit();
            return;
        }
        /* ⚠️ Unmapped: ignored, like a write to an absent register. Extending
         * the map is additive - each address added here is one more thing an
         * $E8 host diagnostic can see truthfully.
         *
         * ⭐ IDENTIFIED BUT NOT MAPPED - deliberately. A scan of the verified
         * binary for RAW zero-page operands (immediates and bit numbers
         * excluded) found these referenced but not understood:
         *
         *   $21   lda, ldm, sta      cleared at ResetEntry beside PowerFlags
         *   $22   dec, inc, ldm      a counter
         *   $23   dec, inc, ldm      a counter
         *
         * We know they EXIST and are live; we do not know what they MEAN.
         * Mapping them onto plausible AVR state would invent semantics, and a
         * host reading an invented value cannot tell it is invented - which is
         * strictly worse than reading 0 from an address we admit is unmapped.
         *
         * Also unmapped, and deliberately: $07/$12/$13-$17/$1B/$1C. Those are
         * the transient command/reply buffer and the WriteLocation pointer,
         * live only DURING a transaction. An $E8 read of them would mostly
         * observe the $E8 transaction itself - self-referential and useless. */
        return;
    }
}

/* --- $Ex PMGR_Command -- BINARY-VERIFIED at $F1B6, 2026-08-30 --------------
 *
 *   F1B8  cmp $EF  cmd_PmgrSoftReset -> ReturnDataToHost2, then jmp InitPorts
 *   F1C4  cmp $EC  cmd_PmgrSelfTest  -> SelfTest1/2/3, ONE packed byte
 *   F1EB  cmp $EA  cmd_readPmgrVers  -> two bytes {$02,$B5} from $E802/$E803
 *   F200  else     .LAB_F200         -> M50753 internal memory read/write
 *
 * ⛔ THIS WAS A STUB THAT ANSWERED EVERY $Ex READ WITH THE VERSION BYTES. With
 * three distinct read opcodes that meant $E8 (memory read) and $EC (self-test)
 * both received `02 B5` - a version reply where the host expected memory
 * contents or a status byte. */
#define CMD_PMGR_WRITE_MEM   0xE0u
#define CMD_PMGR_READ_MEM    0xE8u
#define CMD_PMGR_VERS        0xEAu
#define CMD_PMGR_SELFTEST    0xECu
#define CMD_PMGR_SOFTRESET   0xEFu

/* $E802/$E803 in pmuv1.bin - VERIFIED: bytes are 02 B5.
 * ⚠️ The old comment said $E803/$E804. That was off by one. */
#define PMGR_VERS1           0x02u
#define PMGR_VERS2           0xB5u

void pmgr_cmd_pmgr(pmgr_packet_t *pkt)
{
    switch (pkt->command) {

    case CMD_PMGR_SOFTRESET: {                  /* F1BC .PmgrSoftReset */
        /* F1BC-F1C1: reply FIRST, and only re-init if the reply got out.
         * `bcs .fail` - a failed handshake must NOT re-init the ports, or the
         * host is left with a machine that reset for no reason it can see. */
        const uint8_t none = 0;
        if (!pmgr_return_data(pkt->command, &none, 0)) return;

        /* F1C1  jmp InitPorts - the PORTS only. NOT a full cold entry: it does
         * not clear PowerFlags, re-check PRAM or touch the battery levels.
         *
         * ✅ Port_P0 = $00 - TRACED THROUGH THE BINARY, 2026-08-30.
         *
         * `jmp InitPorts` lands on the `sta Port_P0` INSTRUCTION, so the value
         * written is whatever the accumulator holds. For $EF the host sends
         * count 0, so ReturnDataToHost2 runs:
         *
         *     EA16  lda CommandByte     A = $EF
         *     EA18  jsr SendByte
         *     EA1D  lda ByteCount       A = 0
         *     EA1F  jsr SendByte
         *     EA24  tst $12             ByteCount == 0
         *     EA26  beq ReturnDataExit  <- exits HERE, A = 0, carry clear
         *
         * ⭐ So Port_P0 = $00. The ACTIVE-LOW rails (SYS_PWR*, HD_PWR*,
         * MODEM_PWR*, SERIAL_PWR*, SOUND_PWR*) all come ON - but bit 6,
         * MINUS_5V, is ACTIVE HIGH, so $00 turns -5 V OFF. Not the cold $9F
         * (everything off), and not the reset-switch (P0 & 4) | $40, which
         * SETS bit 6.
         * ⛔ CORRECTED 2026-09-19 (MEASUREMENTS 2.251): this said "a soft reset
         * turns EVERYTHING ON", which is wrong for -5 V. The code was always
         * right - it writes $00, as Apple does; only this comment was not.
         *
         * ⛔ We previously used the cold value here - wrong in the most
         * consequential direction, powering the machine DOWN on a command that
         * brings it up.
         *
         * ⚠️ This holds for count 0. A non-zero count would exit at EA36 with A
         * = the last byte sent; no host is expected to do that on a soft reset,
         * but it is where the behaviour would differ. */
        pmgr_ports_init_softreset();

        /* ⭐ AND THEN THE RELEASE - the half we were missing until 2026-09-20.
         *
         * ⛔ THE BUG (MEASUREMENTS 2.266, found after 2.262/2.263/2.265):
         * ports_init_p0() ends by ASSERTING SYS_RST* and leaves KBD_RST* LOW.
         * Until 2026-09-20 this case returned right here - so $EF put the
         * 68000 into reset and NOTHING ever took it out. Measured 4/4: the
         * correct EF 00 reply, then the host stops issuing PMREQ* entirely,
         * the bus sits at $FF, our AVR keeps ticking, and the programmer's key
         * is dead because the keyboard controller is in reset too.
         *
         * `jmp InitPorts` does not RETURN in the original - InitPorts falls
         * THROUGH into the bring-up and releases both lines before
         * CommandReceive (:203 seb KBD_RST, :216 seb SYS_RST). We already have
         * that code, factored from those very line ranges; this path simply
         * never called it.
         *
         * ⚠️ INTERRUPTS. Both functions require them already OFF and leave
         * them off (see the INTERRUPT-STATE CONTRACT in pmgr_handlers.h).
         * Their other callers are main() and the reset-switch service, both of
         * which are off already; $EF arrives from the poll loop with them ON.
         * Save/disable/restore is the pattern the contract prescribes and that
         * pmgr_adb_transact() and pmgr_sleep() already use.
         *
         * ⚠️ This BLOCKS for ~33 ms (pmgr_system_release waits two real 60 Hz
         * edges). Acceptable here and only here: SYS_RST* is already asserted,
         * so no host transaction can be missed - and the original blocks in
         * exactly the same place, inline, from the same jmp.
         *
         * ⛔⛔ THE PARK BRANCH WAS MISSING UNTIL 2026-09-24, AND THE GATE RUN
         * FOUND IT (MEASUREMENTS 2.325). An earlier revision read:
         *
         *     if (!pmgr_cold_should_park())
         *         pmgr_system_release();
         *
         * with NO else. When VIA_TEST was set - which is what actually happened
         * on the bench - that did NOTHING: it returned to the poll loop with
         * SYS_RST* still asserted and NO PARK LOOP RUNNING, so nothing watched
         * for a wake event. Measured: KBD_RST* HIGH (pre_dispatch ran),
         * SYS_RST* LOW (system_release skipped), and a keypress changed
         * nothing, because there was no sleep loop to service AKD.
         * ⚠️ The machine was not parked. It was ABANDONED.
         *
         * ⛔ The comment here previously called that branch "faithful but
         * UNTESTED". It was ABSENT. A desk audit against main.c:262-265 would
         * have caught it; the gate caught it on hardware instead.
         *
         * ⭐ This now mirrors main()'s dispatch EXACTLY - both branches, same
         * order, same functions. The original's `jmp Sleep` does not return
         * either: it falls through the park loop into the wake path and ends at
         * `jmp CommandReceive`, which is what returning to our poll loop after
         * pmgr_sleep() reproduces.
         *
         * ⚠️ pmgr_sleep() is callable with interrupts on or off and restores
         * the caller's state (see the contract in pmgr_handlers.h), so calling
         * it from inside this cli() block is safe.
         *
         * ⭐ VALIDATED ON HARDWARE 2026-09-25 (MEASUREMENTS 2.327/2.329).
         * Baseline 2026-09-25-ef-soft-reset.
         *
         *   G1  ✅ x4  park branch. Injection verified by `dm a0` = 00EF every
         *              run. Parked: SYS_RST* LOW, KBD_RST* HIGH, PMREQ* silent,
         *              60 Hz control valid, -5 V at +0.040 V. A KEYPRESS BOOTS
         *              the machine - and that is the ONLY discriminator, because
         *              a correct park and the abandoned state of the rejected
         *              fix are IDENTICAL on the reset lines.
         *   G2  ✅     the wake reaches the Finder
         *   G3  ⭐     -5 V: -5.20 V (Finder) -> +0.040 V (parked) -> -5.21 V
         *              (restored by the boot)
         *   G4  ✅     cold boot | Special > Sleep + wake | reset switch.
         *              ⭐ The sleep path matters most: $EF now calls
         *              pmgr_sleep() from the poll loop, a caller it never had.
         *
         * ⛔⛔ TWO LIMITS, AND NEITHER IS CLOSED:
         *
         *   G5 IS NOT MEASURED ON THIS BUILD. It passed on 2.325 on the
         *   REJECTED code, and the reply is emitted BEFORE ports_init_softreset()
         *   and before this dispatch, so nothing here can affect the
         *   reply-versus-reset ordering. ⚠️ That is an argument from code
         *   structure, accepted deliberately by the operator (2.330). It must
         *   NEVER be cited as a G5 pass. Four capture attempts failed and the
         *   cause is unexplained.
         *
         *   THE RELEASE BRANCH IS UNTESTED. VIA_TEST never cleared in four
         *   runs, so pmgr_system_release() on this path has never executed.
         *   ⛔ Do not claim it works. */
        {
            const uint8_t sreg = SREG;
            cli();
            pmgr_pre_dispatch_init();           /* :190-203, releases KBD_RST* */
            if (pmgr_cold_should_park())        /* :204-207 */
                pmgr_sleep();                   /* :207 jmp Sleep */
            else
                pmgr_system_release();          /* :208-218, releases SYS_RST* */
            SREG = sreg;
        }
        return;
    }

    case CMD_PMGR_SELFTEST: {                   /* F1C4 .PmgrSelfTest */
        /* F1C8-F1E4. $13 starts 0; each SelfTest returns its result in CARRY
         * and `ror $13` shifts it in at bit 7. Three tests, then FIVE more
         * `ror`s slide the trio down to bits 0-2:
         *
         *     after ror x3   c3 c2 c1 0 0 0 0 0
         *     after ror x8    0  0  0 0 0 c3 c2 c1
         *
         * so bit0 = SelfTest1, bit1 = SelfTest2, bit2 = SelfTest3.
         *
         * ✅ POLARITY CONFIRMED, 2026-08-30:
         *
         *     bit 0 = SelfTest1   0 = PASS, 1 = FAIL
         *     bit 1 = SelfTest2   0 = PASS, 1 = FAIL
         *     bit 2 = SelfTest3   0 = PASS, 1 = FAIL
         *     bits 3-7 = 0
         *
         * SelfTest1/2/3 ($F23D/$F280/$F2F0) exercise M50753 INTERNALS - its
         * RAM, its ROM checksum, its own timer. An AVR has none of those, so
         * there is nothing here that could fail in the way the original means.
         *
         * ⭐ ZERO IS THEREFORE THE CORRECT REPLY, not a placeholder: it reports
         * three passes, which is the truthful answer for a replacement whose
         * M50753 internals do not exist to be faulty. Reporting a failure would
         * invite the host into a recovery path for hardware that is not there.
         *
         * ⚠️ If a future revision adds real AVR self-tests (flash CRC, SRAM
         * march, timer sanity), map them onto these three bits and remember the
         * polarity is INVERTED from the intuitive one - a SET bit is a FAULT. */
        const uint8_t status = 0x00u;   /* all three pass */
        (void)pmgr_return_data(pkt->command, &status, 1);   /* F1E4 ByteCount 1 */
        return;
    }

    case CMD_PMGR_VERS: {                       /* F1EB .readPmgrVers */
        static const uint8_t v[2] = { PMGR_VERS1, PMGR_VERS2 };
        (void)pmgr_return_data(pkt->command, v, sizeof v);  /* F1F9 ByteCount 2 */
        return;
    }

    default: {
        /* ⭐ .LAB_F200 - $E0 write / $E8 read of the virtual M50753 address
         * space. FULLY DECODED FROM THE BINARY, $F200-$F23B, 2026-08-30:
         *
         *   F20F  ldx $2          X = ByteCount
         *   F211  beq .fail       BC == 0 -> sec, rts   no reply
         *   F213  dex
         *   F214  beq .fail       BC == 1 -> sec, rts   no reply
         *   F216  dex
         *   F217  beq .LAB_F22A   BC == 2 -> clc, rts   SUCCESS, no data
         *   F21B  cmp cmd_writePmgrRAM ($E0)
         *   F21F  ldy #0
         *   F221  lda $15,Y / sta ($1B),Y / iny / dex / bne   WRITE LOOP
         *   F22E  ldx $15 / stx $12 / jsr ReturnDataToHost2    READ
         *
         * Payload is [addrHi, addrLo, ...]. $13=hi, $14=lo, and the firmware
         * stores them as $1C=hi, $1B=lo - the pointer is LOW byte first.
         *
         * ⭐ TWO THINGS THAT ARE EASY TO GET WRONG:
         *
         *  1. BC == 2 is ACCEPTED, not rejected. It is a zero-data write that
         *     completes normally. Only 0 and 1 fail.
         *  2. The READ length comes from $15 - the THIRD PAYLOAD BYTE - not
         *     from ByteCount-2. The host may legitimately ask for a different
         *     number of bytes than it sent.
         *
         * Neither path replies except the read: the write ends `clc; rts` and
         * `.fail` ends `sec; rts`, and neither calls ReturnDataToHost2. */

        if (pkt->length < 2u) return;             /* F211/F214 .fail */

        /* $13 -> $1C (high), $14 -> $1B (low) */
        const uint16_t addr = (uint16_t)(((uint16_t)pkt->data[0] << 8) |
                                          (uint16_t)pkt->data[1]);

        if (pkt->length == 2u) return;            /* F217 - success, no data */

        if (pkt->command == CMD_PMGR_WRITE_MEM) { /* F21B - $E0 */
            /* F221-F228: X = ByteCount-2 data bytes, starting at $15.
             * Writes into $E800-$FFFF are IGNORED and still complete normally -
             * pmgr_mem_write() enforces that. */
            const uint8_t n = (uint8_t)(pkt->length - 2u);
            for (uint8_t k = 0; k < n; k++)
                pmgr_mem_write((uint16_t)(addr + k), pkt->data[2u + k]);
            return;                               /* F22A clc, rts - no reply */
        }

        /* F22E - the READ path. $E8 canonically, and anything else unmatched. */
        {
            uint8_t n = pkt->data[2];             /* F22E ldx $15 */
            if (n > PMGR_MAX_PAYLOAD) n = PMGR_MAX_PAYLOAD;   /* our bound */
            uint8_t buf[PMGR_MAX_PAYLOAD];
            for (uint8_t k = 0; k < n; k++)
                buf[k] = pmgr_mem_read((uint16_t)(addr + k));
            /* F230 beq .LAB_F238 - a length of 0 still replies, with count 0 */
            (void)pmgr_return_data(pkt->command, buf, n);
            return;
        }
    }
    }
}


/* FUN_EB97 equivalent (pmgr.asm 571-585): runs once per SIXTYHZ edge.
 *
 *     clb Int1_Req      -> the ISR clears the pin flag
 *     dec $24 / beq     -> divide by 60
 *     clb/seb OneSec    -> pulse 1SEC*
 *     ldm #$3C,$24      -> reload
 *
 * $0 bits 5 and 6 ARE now modelled (an earlier note here said they were not):
 *   bit 6  -> s_asleep, set at :1356 and cleared at :1380
 *   bit 5  -> s_wake_time_hit, set by the compare at :610
 *
 * ⚠️ ONE DIVERGENCE, deliberate. `:573 clb 5,$0` clears bit 5 at the TOP of
 * every FUN_EB97, so in the original it is a PER-TICK TRANSIENT: set by the
 * compare, tested at :1370, gone by the next tick. Ours LATCHES until
 * pmgr_wake_timer_ack(). Equivalent in practice - the compare is an exact
 * match, so it is true for exactly one second, and we ack on waking - but a
 * latched flag would survive a tick the original's would not. If a spurious
 * timer wake is ever seen, this is the place to look.
 *
 * STILL NOT implemented: the conditional writes at :574-577. They fire only
 * when bit 6 is CLEAR (awake) and VIA_TEST is CLEAR, writing Port_P0 =
 * %01001000 and setting NC on Port_P1. VIA_TEST clear is the bench/test case,
 * and NC is documented as "probably for testing" (pmgr.asm:77), so this looks
 * like a diagnostic path rather than anything a fitted machine relies on. */
/* The timekeeping half of FUN_EB97, without .ADBAction.
 *
 * The split matters during SLEEP. The original keeps calling FUN_EB97 on every
 * 60 Hz tick while parked (pmgr.asm 1360, and again in the AKD wait at 1391),
 * so the clock and the 1SEC* pulse never stop - MEASUREMENTS.md 2.10 measured
 * the core waking for ~85 us on every tick to do exactly this. But ADB must NOT
 * run while parked: the system is held in reset and there is nothing to poll
 * for. FUN_EB97 itself never calls .ADBAction; that is a separate dispatch. */
static void tick_core(bool poll_adb)
{
    /* ⭐ 571-572  FUN_EB97 opens with:
     *
     *     clb Int1_Req,Int_Ctrl_Reg
     *     clb 5,$0                    <- HERE
     *
     * **BIT 5 IS A PER-TICK TRANSIENT, NOT AN EVENT LATCH.** It is cleared at
     * the top of every 60 Hz tick, set later in the SAME tick by the 1 Hz
     * compare (:610) if the armed time matches, and consumed by the park loop
     * immediately afterwards. It never survives a tick boundary.
     *
     * ⛔ WE MODELLED IT AS A LATCH. s_wake_time_hit stayed set until something
     * explicitly acknowledged it, so a hit that was not consumed persisted -
     * and after an AKD wake (which does not clear it) the NEXT sleep would see
     * it still set and wake immediately.
     *
     * ⚠️ An earlier comment in this file called that persistence "arguably a bug
     * in Apple's code, reproduced deliberately". That was wrong: Apple clears it
     * on the next tick, so the persistence was OURS. Fidelity was being claimed
     * for a divergence.
     *
     * This line is also the second, independent reason the `bbc 5,$0` guard at
     * :598 must be a disassembly error: with bit 5 cleared here, a branch-if-
     * clear guard there would skip the compare on every tick and the wake timer
     * could never fire at all. */
    pmgr_wake_timer_ack();          /* 572  clb 5,$0 */

    /* ⭐ BOTH of these are the ADB SERVICE PATH, and PMINT is gated on it.
     *
     * 291-297 `.noADBAction` sits immediately after `.ADBAction` and is reached
     * ONLY through it. `poll_adb` is true exactly when we run that path, so
     * gating the pulse on it reproduces Apple's reachability rather than just
     * its cadence.
     *
     * ⛔ THIS GATE WAS MISSING WHEN THE 60 Hz RE-PULSE WAS FIRST ADDED, and the
     * bug it caused is worth spelling out:
     *
     *     sleep_wait_tick() calls tick_core(FALSE) once per tick throughout the
     *     park. An ungated pulse therefore drove PMINT* at 60 Hz WHILE
     *     SYS_RST* WAS ASSERTED AND THE 68000 WAS HELD IN RESET - hammering the
     *     VIA's interrupt input on a machine that cannot service it.
     *
     * Apple cannot do this: its park loop (.LAB_F0EA) calls FUN_EB97 for
     * timekeeping and never enters CommandReceive or .ADBAction, so
     * .noADBAction is unreachable while parked. A parked PMGR raises no
     * interrupts at all.
     *
     * The catch-up tick also passes false, and that is right for the same
     * reason: it deliberately skips .ADBAction to avoid a burst of autopolls,
     * so Apple would not reach .noADBAction on those passes either. */
#ifdef PMGR_ADB_HOST_SERVICE
    if (poll_adb) {
        pmgr_adb_poll();        /* .ADBAction   */
        /* Fix A (design §10): a command byte latched during the ADB work is served from the main loop straight
         * after this tick returns. Until then do nothing slow: no PMINT* pulse this tick (it repeats next tick
         * while flags are pending) and the 1 Hz work waits in s_1s_deferred - the divider still advances. */
        if (pmgr_bus_has_latched()) {
            (void)pmgr_tick_divider_step(&s_sec_divider, true, &s_1s_deferred);
            return;
        }
        pmgr_int_service();     /* .noADBAction */
    }
    if (pmgr_tick_divider_step(&s_sec_divider, false, &s_1s_deferred))
        pmgr_tick_1s();
#else
    if (poll_adb) {
        pmgr_adb_poll();        /* .ADBAction   */
        pmgr_int_service();     /* .noADBAction */
    }
    if (--s_sec_divider != 0u) return;
    s_sec_divider = 60u;
    pmgr_tick_1s();
#endif
}

#ifdef PMGR_ADB_HOST_SERVICE
/* Fix A: the 1 Hz work of a tick that latched a command byte, run once that transaction has been served. */
void pmgr_tick_run_deferred(void)
{
    if (!s_1s_deferred) return;
    s_1s_deferred = false;
    pmgr_tick_1s();
}
#endif

void pmgr_tick_60hz(void)
{
    tick_core(true);          /* .ADBAction runs off the 60 Hz tick */
}

/* A tick being replayed from a backlog: advance time, but do NOT poll ADB.
 *
 * If the main loop was starved past a 60 Hz period - a multi-byte reply with
 * several 2 ms bus timeouts will do it - the elapsed ticks must still reach the
 * /60 divider or the Mac's clock loses time. But firing .ADBAction once per
 * backlogged tick would burst several autopolls back to back, which is not what
 * a 60 Hz poll rate means. So the first tick of a batch is a full tick and the
 * rest are timekeeping only. */
void pmgr_tick_catchup(void)
{
    tick_core(false);
}

/* One 60 Hz period, servicing the tick exactly as the original does inside its
 * wait loops (`bit Int_Ctrl_Reg / bpl / jsr FUN_EB97`). Falls back to a nominal
 * delay if SIXTYHZ is absent, so a bench board still makes progress. */
#ifdef PMGR_PARK_SLEEP
/* ⭐⭐ LOW-POWER PARK (design/AVR-PARK-SLEEP.md, MEASUREMENTS 2.199).
 *
 * Replaces the 1 us busy-poll of SIXTYHZ with a sleep until its next falling
 * edge. PC2 is a FULLY ASYNCHRONOUS pin, so its edge interrupt wakes the part
 * from POWER-DOWN. The RTC PIT (64 Hz, 32 kHz oscillator, runs in power-down)
 * is the fallback: two PIT periods with no edge count as one tick, as the
 * polled path's 25 ms + 17 ms fallback did.
 *
 * Only while s_asleep (rails off). Called with interrupts DISABLED (pmgr_sleep
 * :441) and returns with them disabled; they are enabled only across the sleep
 * instruction itself, and while g_parked is set the ISRs record a wake cause
 * instead of counting ticks, so no tick is counted twice.
 *
 * If the PIT is not running (tick_init() could not start the RTC) the polled path
 * is used, so a board with neither source cannot sleep forever.
 *
 * ⛔ THREE OUTCOMES, NOT TWO (review, MEASUREMENTS 2.201). The first version
 * returned bool and the caller ignored it, so the POLLED timeout (~25 ms) lost
 * the 17 ms it used to add - a no-clock tick every ~25 ms instead of ~42 ms.
 *   PARK_EDGE           a SIXTYHZ edge: run the tick now
 *   PARK_PIT_FALLBACK   no edge for two PIT periods (~31 ms): run the tick now.
 *                       Deliberately ~31 ms, not the polled path's 25 + 17 ms,
 *                       which was a search timeout plus an approximation, not a
 *                       specified period; bench-only (no 60 Hz source)
 *   PARK_POLL_TIMEOUT   no PIT, no edge in ~25 ms: the caller adds the same
 *                       17 ms as before, so this path is unchanged */
typedef enum { PARK_EDGE, PARK_PIT_FALLBACK, PARK_POLL_TIMEOUT } park_wake_t;

static park_wake_t park_sleep_until_edge(void)
{
    if ((RTC.PITCTRLA & RTC_PITEN_bm) == 0u)
        return pmgr_wait_sixtyhz_edge() ? PARK_EDGE : PARK_POLL_TIMEOUT;

    PORTC.INTFLAGS  = SIXTYHZ_bm;       /* wait for the NEXT edge, as the poll does */
    RTC.PITINTFLAGS = RTC_PI_bm;
    g_park_edge = false;
    g_park_pit  = false;
    g_parked    = true;

#ifdef PMGR_PARK_VREF_ALWAYSON
    VREF.ADC0REF |= VREF_ALWAYSON_bm;   /* 2.199 decision 2b */
#endif
    uint8_t pits = 0;
    bool edge = false;
    for (;;) {
#ifdef PMGR_PARK_SLEEP_IDLE
        SLPCTRL.CTRLA = SLPCTRL_SMODE_IDLE_gc | SLPCTRL_SEN_bm;
#else
        SLPCTRL.CTRLA = SLPCTRL_SMODE_PDOWN_gc | SLPCTRL_SEN_bm;
#endif
        sei();                          /* the next instruction runs before any ISR */
        sleep_cpu();
        cli();
        SLPCTRL.CTRLA = 0u;

        if (g_park_edge) { edge = true; break; }
        if (g_park_pit) {
            g_park_pit = false;
            if (++pits >= 2u) break;    /* no SIXTYHZ for two PIT periods */
        }
    }
    g_parked = false;
#ifdef PMGR_PARK_VREF_ALWAYSON
    VREF.ADC0REF &= (uint8_t)~VREF_ALWAYSON_bm;
#else
    pmgr_adc_note_wake();               /* 2.199 decision 2a */
#endif
    return edge ? PARK_EDGE : PARK_PIT_FALLBACK;
}
#endif

static void sleep_wait_tick(void)
{
#ifdef PMGR_PARK_SLEEP
    if (s_asleep) {                     /* scope: the park only (2.199 decision 3) */
        if (park_sleep_until_edge() == PARK_POLL_TIMEOUT)
            _delay_ms(17);              /* exactly the polled path below */
        tick_core(false);
        return;
    }
#endif
    if (!pmgr_wait_sixtyhz_edge())
        _delay_ms(17);
    tick_core(false);
}

/* Called every 60th SIXTYHZ edge. Mirrors the structure of the
 * original's BatteryManage (pmgr.asm 400-458).
 *
 * IntFlags bit 1 is raised by THREE distinct events, and only the charger one
 * also sets charger_changed:
 *     416  charger connected/disconnected  -> bit 1 + charger_changed
 *     450  battery recovered from low      -> bit 1 only
 *     454  battery went low                -> bit 1 only
 */
void pmgr_tick_1s(void)
{
    pmgr_time_tick();                        /* inc timeA, carry upward */

    /* :596-610 - the wake-timer compare lives in the 1 Hz path and only runs
     * while parked ($0 bit 6). */
    pmgr_wake_timer_tick(s_asleep);
    /* pmgr.asm 583-584. The delay is NOT padding - see PMGR_ONESEC_PULSE_US. */
    ASSERT_LOW(ONESEC_N_PORT, ONESEC_N_bm);
    _delay_us(PMGR_ONESEC_PULSE_US);
    DEASSERT_HIGH(ONESEC_N_PORT, ONESEC_N_bm);

    /* ⭐⭐ THE CONVERSION AND THE SCALE VERDICT COME FIRST - :401, BEFORE the
     * charger state at 409-418 and before the HICHG machine.
     *
     * ⛔ THEY USED TO SIT AT THE END OF THE TICK, and the comment below them
     * claimed they ran "BEFORE the interlock reads it" - which was FALSE once
     * the interlock existed. The interlock therefore read a verdict from the
     * PREVIOUS tick, so a conversion failing THIS tick left HICHG asserted for
     * a further second on readings already known bad. Found by review
     * 2026-09-28 (MEASUREMENTS 2.376).
     *
     * ⭐ Moving them here also RESTORES APPLE'S OWN ORDER: the listing calls
     * ReadBattery at :401, ahead of the charger state at 409-418. The old
     * placement was a deviation that predated the interlock.
     *
     * ⚠️ SMOOTHING IS UNCHANGED - this is still pmgr_battery_read(), the
     * single-pole IIR, called exactly once per tick. Only its POSITION moved.
     *
     * ⛔ AD_Ref is only present while the machine is on (2.56), so the verdict
     * cannot be reached at init and must be retried every tick. A reading taken
     * with the rail down leaves the previous verdict untouched.
     * SYS_PWR* is ACTIVE LOW: the bit clear in OUT means asserted, machine on,
     * switched rail up, AD_Ref present - precisely the condition under which a
     * low AD_Ref reading is a fault rather than an expectation. */
    pmgr_battery_read();
    pmgr_adc_recheck_scale((SYS_PWR_N_PORT.OUT & SYS_PWR_N_bm) == 0u);

    /* --- charger state (pmgr.asm 409-418) ---
     * CHRG_ON* is active low: pin low == charger connected. */
    bool charger_now  = PIN_IS_LOW(CHRG_ON_N_PORT, CHRG_ON_N_bm);
    bool charger_was  = (s_power_flags & PF_CHARGER_CONNECTED) != 0u;

    if (charger_now != charger_was) {
        pmgr_set_int_flag(PMGR_INT_BATTERY_bm);      /* seb 1,IntFlags   */
        s_power_flags |= PF_CHARGER_CHANGED;         /* only HERE        */
        pmgr_battery_now();                          /* jsr batteryNow   */
    }

    if (charger_now) s_power_flags |= PF_CHARGER_CONNECTED;
    else             s_power_flags &= (uint8_t)~PF_CHARGER_CONNECTED;

    /* ⭐ HICHG STATE MACHINE - $EAE9-$EB38, implemented 2026-08-31.
     *
     * $EA83  bbc CHRG_ON,Port_P1,.HICHG_OnCheck - CHRG_ON* is active low, so a
     * connected charger takes this path INSTEAD of the battery-low path below.
     *
     * The algorithm is a SYMMETRIC TOP-OFF TIMER:
     *
     *   state 0  idle. Charger appears -> enable HICHG, seed counter to 1,
     *            clear overflow, state 1
     *   state 1  charging to the knee. Each second the level is below 208,
     *            the 16-bit counter counts UP. Reaching 208 -> state 2.
     *            If the counter WRAPS first, set hichg_overflow - a battery
     *            that never reaches the knee
     *   state 2  top-off. Counter counts back DOWN by the same amount it
     *            counted up, so high charge continues for as long again as it
     *            took to reach the knee. Reaching zero -> disable HICHG,
     *            state 3
     *   state 3  done. Nothing further until reset
     *
     * $EB32: at the END of every pass, a level below 90 resets the state to 0
     * ($EAD5) - and .LAB_EAD5 is also where every no-charger path lands, so
     * unplugging the charger resets the machine too.
     *
     * ⚠️ THE KNEE IS A LITERAL 208 ($EB09 cmp #720-512), NOT HICHGLevel ($73,
     * which is 200). They are different numbers and the binary uses the
     * literal. Do not "correct" this to HICHGLevel. The docs made exactly that
     * substitution for weeks; check_docs.py now enforces it.
     *
     * ⭐ WHAT 208 MEANS. It converts to 7.20 V = 2.400 V/cell (MEASUREMENTS.md
     * 2.50 - swept 5.83-7.20 V, and 208 was observed crossing at 7.20 V both
     * ascending and descending), which sits in the
     * GAP between the original Gates/EnerSys Cyclon pack's float ceiling
     * (2.30 VPC) and its cyclic floor (2.45 VPC) - EnerSys US-CYC-AM-007 §6.6.
     * So the knee is a FLOAT-VERSUS-CYCLIC DISCRIMINATOR: unreachable while
     * floating, comfortably crossed during a real cyclic charge. Reaching it
     * proves an active charge is under way, which is what makes a symmetric
     * top-off timer the right response. The same holds for a modern AGM
     * substitute (EXP-645: standby 6.85-6.95 V, cycle 7.30-7.40 V), so this is
     * not an artefact of one datasheet.
     *
     * ⚠️ NOT MODELLED: `seb 5,$14` / `clb 5,$14` alongside the HICHG enable.
     * $14 is the reply-buffer region and its role here is unexplained, so it
     * is left alone rather than guessed at. */
    /* ⛔⛔ THE CALIBRATION GATE - design/HYBRID-CALIBRATION.md §5, N1.9 of 2.349.
     *
     * ⭐ WHY THIS IS HERE AND NOT A WARNING. Apple's overflow guard only raises a
     * FLAG; if the knee is never reached, NOTHING in the PMGR ends high charge
     * (2.246). 2.347 MEASURED that failure on this pairing: at 7.1994 V TRUE the
     * board reports level 179 against the knee literal 208, so an uncalibrated
     * unit counts on forever and fast charge never terminates.
     *
     * With no valid record we go STRAIGHT TO STATE 3 - Apple's own float regime
     * (2.246) - with HICHG deasserted. That is a DIVERGENCE from the original,
     * and a deliberate one: the original would charge.
     *
     * ⚠️ THIS DOES NOT CERTIFY STATE 3. Its voltage is set by the HYBRID, not by
     * us, and #1's recreation hybrid's float voltage is UNMEASURED. The gate
     * removes the dependence on the PMGR's reading; validating the regime is B3.
     * ⛔ Do not rely on it unattended before then. */
    if (charger_now && !pmgr_calib_valid()) {
        /* ⭐ THE GATE FIRST - it is STATIC (the record is read once at boot and
         * cannot change at runtime), so an uncalibrated board must never reach
         * the ADC-fault logic below: it is not trusting the level at all. */
        pmgr_calib_note_gated();            /* ⭐ latch: the gate RAN (visible at $300) */
        if (s_hichg_state != 3u) {
            HICHG_PORT.OUTCLR = HICHG_bm;
            s_power_flags &= (uint8_t)~PF_HICHG_ON;
            s_hichg_state  = 3u;
        }
        s_hichg_valid_run = 0u;
    } else if (charger_now && !pmgr_adc_scale_ok()) {
        /* ⛔ P1's INTERLOCK. The readings are on an unknown scale, so every
         * charging decision below would be taken on numbers we cannot trust. */
        if (!s_hichg_faulted) {
            HICHG_PORT.OUTCLR = HICHG_bm;
            s_power_flags &= (uint8_t)~PF_HICHG_ON;
            s_hichg_state   = 3u;           /* Apple's own "done", NOT a state 4 */
            s_hichg_faulted = true;
        }
        s_hichg_valid_run = 0u;
    } else if (charger_now && s_hichg_faulted) {
        /* ⭐ RECOVERY IS DEBOUNCED and RE-SEEDS. N consecutive valid samples, then
         * back to state 0 - a fresh cycle, never a resumption of a cycle whose
         * counter was accumulated against untrusted readings. */
        if (++s_hichg_valid_run >= PMGR_HICHG_RECOVER_SAMPLES) {
            s_hichg_faulted   = false;
            s_hichg_state     = 0u;
            s_hichg_valid_run = 0u;
        }
    } else if (charger_now) {
        s_hichg_valid_run = 0u;
        if (s_hichg_state == 0u) {                  /* $EAF1-$EB02 */
            HICHG_PORT.OUTSET = HICHG_bm;           /* seb HICHG,Port_P1 */
            s_power_flags |= PF_HICHG_ON;
            s_hichg_state  = 1u;
            s_hichg_lo     = 1u;                    /* $EAFA ldm #1,$22 */
            s_hichg_hi     = 1u;                    /* $EAFD ldm #1,$23 */
            s_power_flags &= (uint8_t)~PF_HICHG_OVERFLOW;
        } else if (s_hichg_state == 1u) {           /* $EB07-$EB1E */
            /* ⭐ THE KNEE IS CONVERTED INTO THIS UNIT'S SCALE, not compared raw
             * (2.349). pmgr_calib_threshold() is the IDENTITY with no valid
             * record, so the uncalibrated path is bit-for-bit as before - but
             * that path cannot get here, because the gate above took it. */
            if (!level_below(s_battery_level, 720u)) {
                s_hichg_state = 2u;                 /* $EB1B - reached knee */
            } else if ((s_power_flags & PF_HICHG_OVERFLOW) == 0u) {
                /* $EB10-$EB18: inc $22; only on wrap inc $23; only on ITS
                 * wrap set overflow. */
                if (++s_hichg_lo == 0u && ++s_hichg_hi == 0u)
                    s_power_flags |= PF_HICHG_OVERFLOW;
            }
        } else if (s_hichg_state == 2u) {           /* $EB1F-$EB30 */
            /* $EB21-$EB27: dec $22; only if it became 0, dec $23; done only
             * when BOTH became 0 in this pass. */
            if (--s_hichg_lo == 0u && --s_hichg_hi == 0u) {
                s_hichg_state = 3u;
                HICHG_PORT.OUTCLR = HICHG_bm;       /* clb HICHG,Port_P1 */
                s_power_flags &= (uint8_t)~PF_HICHG_ON;
            }
        }
        /* state 3: nothing - $EB1F `bne .LAB_EB32` skips straight to the tail */
    }

    /* $EB32-$EB36 - the shared tail. Below 90 resets the machine, and every
     * no-charger path lands on the same reset ($EAD5). */
    /* ⛔ The gate's state 3 must survive this. Resetting to 0 while the charger
     * is still connected and uncalibrated would re-enter state 0 on the next
     * tick and assert HICHG - the exact failure the gate exists to prevent.
     * (The same shape of bug was found by review in the P1 interlock's tail.) */
    if (s_hichg_faulted) {
        /* ⛔ A low level must NOT clear the fault - that would re-enter state 0 on
         * the next tick and re-assert HICHG on readings still untrusted. Only the
         * charger going away clears it. (The same shape of bug the operator's
         * review caught in P1's own tail, and in the gate's.) */
        if (!charger_now) {
            s_hichg_state     = 0u;
            s_hichg_faulted   = false;
            s_hichg_valid_run = 0u;
        }
    } else if (!charger_now || (level_below(s_battery_level, 90u + 512u) && pmgr_calib_valid())) {
        s_hichg_state = 0u;
    }

    /* --- battery level (pmgr.asm 437-458) ---
     * The original compares against lowBatteryLevel and uses the midpoint of
     * low/dead as a second threshold. Transitions raise bit 1 WITHOUT
     * charger_changed.
     *
     * ⚠️ The listing at 446-447 reads  lda BatteryLevel / bcc .LAB_EABC  with
     * no comparison between them, so the branch would test carry left over
     * from the  adc  at 444. That cannot be the intent; a  cmp lowBatteryLevel
     * appears to be missing from the disassembly. Implemented as the obvious
     * intent - verify against real silicon before trusting it. */

    /* ⭐ INTERLOCK: never act on a battery level whose SCALE is unverified.
     *
     * PMGR_ADC_LOW_LEVEL and _DEAD_LEVEL are Apple's byte-scale constants
     * (78 / 62). They mean nothing unless our conversions land on the same
     * scale, and pmgr_adc_recheck_scale() checks exactly that against AD_Ref -
     * which measures 2.3 V with the machine on, giving 113, NOT the 128 an
     * earlier revision assumed from a nominal 2.5 V divider.
     *
     * If that check failed, the readings are on an unknown scale and could sit
     * anywhere - including below the DEAD threshold, which would report a flat
     * battery on a healthy machine and invite a shutdown. Reporting nothing is
     * strictly safer than reporting a fault we cannot substantiate.
     *
     * The level is still measured and still returned to the host in the
     * battery reply, so the value remains visible for diagnosis; we simply
     * decline to draw a CONCLUSION from it. Both flags are cleared rather than
     * left stale for the same reason. */
    if (!pmgr_adc_scale_ok()) {
        s_power_flags &= (uint8_t)~(PF_BATTERY_LOW | PF_BATTERY_DEAD);
    } else {
        bool low_was = (s_power_flags & PF_BATTERY_LOW) != 0u;
        bool low_now = level_below(s_battery_level, PMGR_ADC_LOW_LEVEL + 512u);

        if (low_now != low_was) {
            pmgr_set_int_flag(PMGR_INT_BATTERY_bm);      /* 450 / 454 - no  */
            if (low_now) s_power_flags |= PF_BATTERY_LOW;/* charger_changed */
            else         s_power_flags &= (uint8_t)~PF_BATTERY_LOW;
        }

        /* ⭐ THE MIDPOINT LEVEL - pmgr.asm 438-445, 455-457.
         *
         *      438  lda lowBatteryLevel / 439 lsr A          ; 78 >> 1 = 39
         *      441  lda deadBatteryLevel / 442 lsr A         ; 62 >> 1 = 31
         *      444  adc $20                                  ; $20 = 70
         *      455  cmp $20 / 456 bcs .LAB_EAD5
         *      457  seb 6,PowerFlags
         *
         * PF_STAY_ASLEEP is set at the MIDPOINT of low and dead - 70 - which
         * is not either named constant and appears nowhere in the equates
         * because it is COMPUTED. An earlier comment here said the condition
         * was "not yet pinned down"; this is it.
         *
         * ⭐ OBSERVED ON HARDWARE 2026-08-28 (MEASUREMENTS.md 2.18): at 5.7 V
         * the machine refused ~30 s of keypresses while PMGCLK_F kept bursting,
         * then woke instantly at 6.5 V. That is :1366 skipping every wake test
         * while this bit is set. */
        if (level_below(s_battery_level, PMGR_ADC_STAY_ASLEEP_LEVEL + 512u))
            s_power_flags |= PF_STAY_ASLEEP;             /* 457 */
        else
            s_power_flags &= (uint8_t)~PF_STAY_ASLEEP;   /* 422 clears it */

        if (level_below(s_battery_level, PMGR_ADC_DEAD_LEVEL + 512u)) {
            s_power_flags |= PF_BATTERY_DEAD;            /* 460 */

            /* 461-463  bbs 6,$0 / bbc VIA_TEST / jmp Sleep - the PMGR puts the
             * machine down ITSELF on a dead battery. Not done if already
             * parked, and only when VIA_TEST is set.
             *
             * The original JUMPS (its stack is reset at :1335). We cannot, and
             * calling pmgr_sleep() from here would re-enter it - this runs
             * inside tick_core(), which pmgr_sleep()'s own wait loop calls. So
             * raise a request and let the main loop act on it, outside any
             * tick. */
            if (!s_asleep && pmgr_cold_should_park())
                s_sleep_requested = true;
        } else {
            s_power_flags &= (uint8_t)~PF_BATTERY_DEAD;
        }
    }

    /* HICHG is an OUTPUT (the high-charge enable), so this flag reports what
     * we are driving, not anything sensed. */
    if ((HICHG_PORT.OUT & HICHG_bm) != 0u) s_power_flags |= PF_HICHG_ON;
    else                                   s_power_flags &= (uint8_t)~PF_HICHG_ON;
}
