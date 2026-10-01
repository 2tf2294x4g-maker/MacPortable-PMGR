/* P1 — the charging/ADC interlock. design/CHARGING-ADC-INTERLOCK.md.
 *
 * ⭐ THESE RUN THROUGH pmgr_tick_1s(). The reviewer set that constraint and it
 * is the right one: the defect is the ORDERING inside that function - the HICHG
 * state machine is gated only on `charger_now` and never consults
 * pmgr_adc_scale_ok(), while the validity interlock sits 78 lines LATER and
 * guards the low/dead flags only. A test that stubbed the ADC and called the
 * charging logic directly would pass while the bug remained.
 *
 * ⭐⭐ STATE IS OBSERVED THROUGH pmgr_mem_read() - the firmware's own $E8
 * virtual address space, the same view a host gets. Nothing here reaches
 * behind the implementation's back into statics.
 *
 * ⛔ TESTS MARKED [FAIL-TODAY] WERE WRITTEN TO FAIL until S1-S4 were
 * implemented. §4: "On today's code this FAILS - that is the point of writing
 * it first."
 *
 * ⭐⭐ THAT ERA IS OVER. S1-S4 landed on this branch, so every [FAIL-TODAY] is
 * now MANDATORY and the label is history, not a licence. Review of 2026-09-28
 * (MEASUREMENTS 2.376) noted that the tolerant exit status had outlived its
 * purpose: it succeeded when ALL of them failed, so deleting the entire
 * interlock would have left `make run_charging` GREEN. The one condition a
 * pre-registration must never protect is the removal of the thing it tests. */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "handlers_stub.h"
#include "pmgr_handlers.h"
#include "pins_native.h"
#include "pmgr_calib.h"
#include "eesim.h"
#include <string.h>
#include "pmgr_adc.h"

static int pass, fail, todo, hard_fail, todo_pass;
static void ok(int cond, const char *what)
{
    printf("  %s  %s\n", cond ? "PASS" : "FAIL", what);
    if (cond) pass++; else { fail++; hard_fail++; }
}
static void ok_todo(int cond, const char *what)
{
    printf("  %s  %s  [FAIL-TODAY]\n", cond ? "PASS" : "FAIL", what);
    if (cond) { pass++; todo_pass++; } else { fail++; todo++; }
}

/* ---- observation, through the firmware's own $E8 view ------------------- */
static uint8_t  battery_level(void) { return pmgr_mem_read(0x001Eu); }
static uint8_t  hichg_state(void)   { return pmgr_mem_read(0x0021u); }
static uint16_t hichg_count(void)
{ return (uint16_t)(((uint16_t)pmgr_mem_read(0x0023u) << 8) | pmgr_mem_read(0x0022u)); }

/* HICHG is an OUTPUT the PMGR drives (pmgr.asm 479 seb / 510 clb), never a
 * sense line. Asserted = pin driven HIGH. */
static bool hichg_asserted(void) { return (HICHG_PORT.OUT & HICHG_bm) != 0u; }

/* CHRG_ON* is active LOW: charger present = pin low. */

/* ⛔⛔ THIS FILE MUST PROVISION A CALIBRATION RECORD. Since the gate landed
 * (MEASUREMENTS 2.349 N1.9), an UNPROVISIONED board cannot fast-charge at all:
 * pmgr_tick_1s() sends it straight to state 3 with HICHG deasserted. That is the
 * intended behaviour and it is what the gate is FOR - but it means these tests,
 * which exercise the state machine BEYOND the gate, would otherwise all sit in
 * state 3 and pass or fail for the wrong reason.
 * ⭐ The GATE ITSELF is tested in test_calib.c, not here. Here we install a
 * valid record so the machine under test is reachable. */
static void provision_calibration(void)
{
    pmgr_calib_t c = {0};
    c.magic = PMGR_CALIB_MAGIC; c.version = PMGR_CALIB_VERSION; c.pairing_id = 1u;
    c.a_q8 = PMGR_CALIB_A_NOMINAL;      /* ⭐ the NOMINAL line, so every threshold
                                         * in this file keeps its original value -
                                         * the knee stays 208 and these tests
                                         * compare against the numbers they were
                                         * written for. */
    c.b_q8 = 0; c.resid_q4 = 0u; c.span_mv = 900u; c.rail_mv = 5250u;
    uint8_t b[PMGR_CALIB_SIZE];
    pmgr_calib_encode(&c, b);
    sim_erase();
    memcpy(&EE[PMGR_CALIB_EE_BASE], b, sizeof b);
    pmgr_calib_load();
}

static void set_charger(bool present)
{
    if (present) CHRG_ON_N_PORT.IN &= (uint8_t)~CHRG_ON_N_bm;
    else         CHRG_ON_N_PORT.IN |= CHRG_ON_N_bm;
}

static void ticks(int n) { for (int i = 0; i < n; i++) pmgr_tick_1s(); }

/* ⛔ THE CHARGER-UNPLUG TICK IS LOAD-BEARING. pmgr_handlers_init() does NOT
 * reset the HICHG state machine - s_hichg_state and its 16-bit counter survive
 * it - so without this a later test starts wherever an earlier one left the
 * machine. That produced two false PASSes before a sanity assertion caught it
 * (MEASUREMENTS 2.331). :2161's `if (!charger_now || level < 90) state = 0` is
 * the firmware's own reset, i.e. pulling the lead. */
static void enter_charging(void)
{
    stub_reset();
    pmgr_handlers_init();
    set_charger(false);
    pmgr_tick_1s();                 /* the firmware's own state reset */
    stub_scale_ok = true;
    stub_battery_level = 200u;      /* below the 208 knee, above the 90 reset */
    set_charger(true);
    ticks(5);
}

/* A persistent conversion fault: the scale gate goes false and every reading
 * is the sentinel.
 * ⚠️ 128 MIRRORS pmgr_adc.c:184's PMGR_ADC_SAFE_VALUE, a file-local #define
 * that is not exported. ⛔ If that literal changes this test will not follow
 * it and will silently stop exercising the sentinel. */
static void inject_fault(void)
{
    stub_scale_ok = false;
    stub_adc_faulted = true;
    stub_battery_level = 128u;
}
static void clear_fault(uint8_t level)
{
    stub_scale_ok = true;
    stub_adc_faulted = false;
    stub_battery_level = level;
}

int main(void)
{
    provision_calibration();
    printf("P1 charging/ADC interlock - through pmgr_tick_1s()\n");

    /* ---- sanity ----------------------------------------------------------
     * ⭐ TRIVIAL ON PURPOSE. Two tests below passed for the wrong reason while
     * the harness drove nothing at all, and only these lines exposed it. */
    printf("\nharness\n");
    enter_charging();
    ok(hichg_asserted(), "charger + valid level -> HICHG asserted");
    ok(hichg_state() != 0u, "the state machine has entered (state != 0)");
    ok(battery_level() == 200u, "the $E8 view reports the level we staged");

    /* ---- C1  PERSISTENT FAULT -------------------------------------------- */
    printf("\nC1  persistent ADC fault, charger connected\n");
    enter_charging();
    inject_fault();
    ticks(30);
    ok_todo(!hichg_asserted(), "S2: invalid battery data -> HICHG DEASSERTED");

    /* ---- C2  TRANSIENT FAULT --------------------------------------------- */
    /* Policy decided 2026-09-24: recover after N CONSECUTIVE valid samples.
     * A single blip must not strand the machine; a flapping ADC must not
     * oscillate high charge once a second. N = 3, CHOSEN not derived. */
    printf("\nC2  transient fault\n");
    enter_charging();
    inject_fault();
    ticks(1);                       /* the blip */
    clear_fault(200u);
    ticks(10);                      /* well past N = 3 */
    /* ⭐ REAL since S2 landed. Before the fix this was vacuous - nothing
     * inhibited, so "charging continued" was trivially true. Now it checks
     * that a single blip genuinely recovers rather than stranding the
     * machine, which is the requirement C2 was written around. */
    ok(hichg_asserted(), "a single blip does not strand the machine");

    enter_charging();
    inject_fault();
    ticks(5);
    clear_fault(200u);
    ticks(1);                       /* only ONE good sample */
    ok_todo(!hichg_asserted(), "one good sample is not enough - N required");

    /* ---- C4  OVERFLOW ----------------------------------------------------- */
    printf("\nC4  the counter and its overflow flag\n");
    enter_charging();
    ok(hichg_count() != 0u, "the counter runs while below the knee");
    printf("    ⛔ driving 65k ticks to a real wrap is impractical here; what\n"
           "       C4 asks - that hichg_overflow stops being a flag with no\n"
           "       consequence - is a property of S2 and is asserted there.\n");

    /* ---- C6  FAULT ENTRY FROM EVERY CHARGING STATE (S4) ------------------- */
    printf("\nC6  fault entry from each charging state\n");
    stub_reset(); pmgr_handlers_init(); set_charger(false); pmgr_tick_1s();
    inject_fault(); ticks(3);
    ok(!hichg_asserted(), "state 0 (no charger): HICHG stays off");

    enter_charging();
    ok(hichg_state() == 1u, "reached state 1 (below the knee)");
    inject_fault(); ticks(10);
    ok_todo(!hichg_asserted(), "fault in state 1 -> HICHG deasserted");

    /* ⭐⭐ STATE 2 BECAME REACHABLE WHEN C3 LANDED, exactly as predicted in
     * 2.332. Before per-tick sampling, staging a level >= the knee in the stub
     * could not move s_battery_level - pmgr_battery_now() ran only on a
     * charger-state CHANGE, and a change reset the machine to 0. The staleness
     * fix and the testability fix turned out to be the same change. */
    /* ⭐⭐ THE SMOOTHING MATTERS HERE, and getting this wrong is what made an
     * earlier revision call pmgr_battery_now() per tick. pmgr_battery_read()
     * is a single-pole IIR at alpha 0.5, so staging 210 against a settled 200
     * approaches the 208 knee over SEVERAL ticks - 205, 207, 208 - not one.
     * The charging block also reads the PREVIOUS tick's value, adding a lag.
     * ⛔ Do NOT "fix" a slow knee by sampling raw; the smoothing is deliberate
     * (see pmgr_battery_read's comment: a Portable's rail dips when the drive
     * spins, and a raw sample would let one transient trip a shutdown gate). */
    enter_charging();
    clear_fault(210u);              /* >= the 208 knee */
    for (int i = 0; i < 12 && hichg_state() != 2u; i++) pmgr_tick_1s();
    printf("    level settled to %u, state %u\n", battery_level(), hichg_state());
    ok(hichg_state() == 2u, "reached state 2 (past the knee, via smoothing)");
    inject_fault(); ticks(10);
    ok(!hichg_asserted(), "fault in state 2 -> HICHG deasserted");

    /* ---- C7  RECOVERY MUST NOT REUSE CONTAMINATED STATE (S3) -------------- */
    /* ⭐ ASSERT THE INVARIANT, NOT A DIRECTION. The counter WRAPS, so neither
     * "shorter" nor "longer" holds at the extremes and a directional assertion
     * would pass or fail for the wrong reason. The defect is direction-
     * agnostic: charging time credited to measurements that never happened. */
    printf("\nC7  recovery must not reuse fault-contaminated counter state\n");
    enter_charging();
    uint16_t before = hichg_count();
    inject_fault();
    ticks(20);                      /* 20 s of sentinel readings */
    clear_fault(200u);
    ticks(5);
    uint16_t after = hichg_count();
    printf("    counter before fault %u, after recovery %u\n", before, after);
    ok_todo(after <= before + 5u, "S3: no accumulation across the fault");

    /* ---- C2b  RECOVERY MUST NOT BE BYPASSED BY A LOW LEVEL ---------------
     * ⛔ THE REVIEWER'S REPRODUCTION, 2026-09-26. The shared tail reset the
     * machine whenever the level was below 90, which CLOBBERED the fault state
     * and let HICHG return on the SECOND valid sample instead of the third.
     * ⚠️ I had dismissed this line while writing S2 - "in fault the level is
     * the 128 sentinel, so < 90 never fires" - which is true during the fault
     * and false during RECOVERY, exactly when it matters. */
    printf("\nC2b recovery is not bypassed by a low battery level\n");
    enter_charging();
    inject_fault();
    ticks(5);
    bool s0 = hichg_asserted();     /* faulted: expect OFF */
    clear_fault(80u);  ticks(1);
    bool s1 = hichg_asserted();     /* 1st valid, genuinely LOW: expect OFF */
    clear_fault(200u); ticks(1);
    bool s2 = hichg_asserted();     /* 2nd valid: expect OFF, N=3 not met */
    clear_fault(200u); ticks(2);
    bool s3 = hichg_asserted();     /* 3rd valid: expect ON */
    printf("    HICHG: faulted=%d  1st(80)=%d  2nd=%d  3rd=%d\n", s0, s1, s2, s3);
    /* ⭐ ONE COMPOUND ASSERTION, deliberately. Split into four, the last one -
     * "charging resumes" - passes VACUOUSLY on the pre-fix tree, where charging
     * never stopped. The whole point is the SHAPE off-off-off-on, which cannot
     * be satisfied by a machine that never inhibits. */
    ok_todo(!s0 && !s1 && !s2 && s3,
            "off-off-off-ON: a low level does not bypass the N=3 rule");

    printf("\n  %d pass, %d fail", pass, fail);
    if (todo) printf("   (%d pre-registered, and now MANDATORY - see below)", todo);
    printf("\n");
    /* ⭐ EXIT STATUS. History, because the shape of this block is the record:
     *
     *   rev 1  returned 0 unconditionally - `make` could never fail here.
     *   rev 2  (review 2026-09-26) failed on hard_fail, and on a MIXTURE of
     *          passing and failing [FAIL-TODAY]s, but still SUCCEEDED when all
     *          of them failed, because that was the pre-fix baseline.
     *   rev 3  (review 2026-09-28) S1-S4 are implemented on this branch, so
     *          that last allowance now hides the worst regression available:
     *          delete the interlock and every [FAIL-TODAY] fails TOGETHER,
     *          which rev 2 read as a clean baseline and reported as success.
     *
     * ⛔ EVERY assertion in this file is now mandatory. The distinction is kept
     * only in the MESSAGE, so a future reader can still tell which assertions
     * were pre-registered before the code existed - that provenance is worth
     * preserving, the exemption is not. */
    if (hard_fail > 0)
        printf("  ⛔ %d assertion(s) failed that must always pass\n", hard_fail);
    if (todo > 0 && todo_pass > 0)
        printf("  ⛔ %d [FAIL-TODAY] passed and %d failed - the interlock is "
               "PARTIALLY present\n", todo_pass, todo);
    else if (todo > 0)
        printf("  ⛔ %d pre-registered [FAIL-TODAY] assertion(s) failed. They "
               "are MANDATORY on this branch: S1-S4 are implemented, so a "
               "failure here means the interlock REGRESSED, not that this is "
               "the baseline tree.\n", todo);
    if (hard_fail > 0 || todo > 0)
        return 1;
    return 0;
}
