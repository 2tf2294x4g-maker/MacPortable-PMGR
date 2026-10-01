/* N1 — the calibration record, its CRC, and THE GATE.
 * Cases and decision rule pre-registered in MEASUREMENTS.md 2.349, BEFORE this
 * file existed. ⛔ All twelve must pass; any failure blocks the change.
 *
 * The numbers under test are 2.347's MEASURED fit for #1's pairing:
 *     raw = 99.35*V - 23.26, residual 1.19 counts, span 0.915 V, rail 5.251 V
 */
#include "pmgr_calib.h"
#include "pmgr_adc.h"   /* PMGR_ADC_LOW_LEVEL - the scale the assertions speak in */
#include "eesim.h"
#include <stdio.h>
#include <string.h>

static int fails;
static void ok(int cond, const char *what)
{
    printf("  %s  %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) fails++;
}

/* 2.347's record. ⭐ a_q8 = round(99.35*256) = 25434, b_q8 = round(-23.26*256) = -5955 */
static pmgr_calib_t good(void)
{
    pmgr_calib_t c = {0};
    c.magic = PMGR_CALIB_MAGIC; c.version = PMGR_CALIB_VERSION; c.pairing_id = 1u;
    c.a_q8 = 25434u; c.b_q8 = -5955; c.resid_q4 = 19u;      /* 1.19 counts */
    c.span_mv = 915u; c.rail_mv = 5251u;
    return c;
}

static void put(const pmgr_calib_t *c)
{
    uint8_t b[PMGR_CALIB_SIZE];
    pmgr_calib_encode(c, b);
    memcpy(&EE[PMGR_CALIB_EE_BASE], b, sizeof b);
}


/* ---- N1.9 proper: drive the REAL charging path ---------------------------- */
#include "pins_native.h"
#include "pmgr_handlers.h"
#include "handlers_stub.h"

static bool hichg_on(void) { return (HICHG_PORT.OUT & HICHG_bm) != 0u; }

static void charger(bool on)
{
    if (on) CHRG_ON_N_PORT.IN &= (uint8_t)~CHRG_ON_N_bm;   /* active LOW */
    else    CHRG_ON_N_PORT.IN |= CHRG_ON_N_bm;
}

static void ticks(int n) { for (int i = 0; i < n; i++) pmgr_tick_1s(); }

static void gate_through_the_tick(void)
{
    /* --- uncalibrated: the charger is present and the level is deep in the
     *     knee-seeking range. An ungated machine asserts HICHG here. --- */
    sim_erase();
    pmgr_calib_load();
    stub_reset(); pmgr_handlers_init();
    stub_scale_ok = true; stub_battery_level = 150u;   /* well below any knee */
    charger(false); pmgr_tick_1s();
    charger(true);  ticks(5);
    ok(!hichg_on(),
       "N1.9  UNCALIBRATED + charger: HICHG is NOT asserted");
    ok(pmgr_mem_read(0x21u) == 3u,
       "N1.9  UNCALIBRATED: the machine sits in STATE 3 (float), not 0/1/2");

    /* ⛔ and it must STAY there - the shared tail must not reset it to 0 and
     *    let the next tick assert HICHG. */
    stub_battery_level = 50u;                          /* < 90, the tail's reset case */
    ticks(5);
    ok(!hichg_on() && pmgr_mem_read(0x21u) == 3u,
       "N1.9  UNCALIBRATED: stays in state 3 even below the 90 reset threshold");

    /* --- calibrated: the SAME conditions must charge normally, or the gate
     *     has broken the feature rather than guarded it. --- */
    sim_erase();
    { pmgr_calib_t g = good(); put(&g); }
    pmgr_calib_load();
    stub_reset(); pmgr_handlers_init();
    stub_scale_ok = true; stub_battery_level = 150u;
    charger(false); pmgr_tick_1s();
    charger(true);  ticks(2);
    ok(hichg_on(), "N1.9  CALIBRATED + charger: HICHG IS asserted (charging works)");
    ok(pmgr_mem_read(0x21u) == 1u, "N1.9  CALIBRATED: enters state 1, seeking the knee");

    /* --- and it terminates at the CONVERTED knee (180), not at 208 ---
     * ⛔ NEGATIVE CONTROL FIRST. Without it this pair of assertions would pass
     * on a build that ignored the calibration and used 208, because 185 < 208
     * would leave state 1 and the "crossed" case could be reached by any drift.
     * 175 must NOT cross; 185 must. Only both together distinguish 180 from 208.
     * ⚠️ ticks() must outlast the IIR: s_battery_level is (new+old)>>1, so it
     * approaches the stub value by halves - 2 ticks from 150 only reaches 176,
     * which is what made this assertion fail before the dwell was fixed. */
    stub_battery_level = 175u;                         /* < 180: must NOT cross */
    ticks(8);
    ok(pmgr_mem_read(0x21u) == 1u,
       "N1.10 CALIBRATED: level 175 is BELOW the converted knee - stays in state 1");
    stub_battery_level = 185u;                         /* >= 180, still < 208 */
    ticks(8);
    ok(pmgr_mem_read(0x21u) == 2u,
       "N1.10 CALIBRATED: level 185 crosses the CONVERTED knee 180 -> state 2");
    ok(185u < 208u,
       "N1.10 ...and 185 < 208, so an UNCALIBRATED build could not have crossed");
}


static void visible_flag(void)
{
    /* uncalibrated, gate not yet run */
    sim_erase(); pmgr_calib_load();
    stub_reset(); pmgr_handlers_init();
    stub_scale_ok = true; stub_battery_level = 150u;
    charger(false); pmgr_tick_1s();
    ok(pmgr_mem_read(PMGR_CALIB_STATUS_ADDR) == PMGR_CALIB_ST_UNCAL,
       "N1.13 no record -> $300 reports UNCAL, and GATED not yet set");

    /* the gate fires */
    charger(true); ticks(3);
    ok(pmgr_mem_read(PMGR_CALIB_STATUS_ADDR) ==
           (PMGR_CALIB_ST_UNCAL | PMGR_CALIB_ST_GATED),
       "N1.13 gate fires -> GATED is set as well");

    /* ⭐ THE LATCH IS THE POINT. Remove the charger: the machine settles into a
     * state that looks ordinary, and UNCAL stays true - but the evidence that
     * the gate ACTUALLY FIRED must not evaporate with it. A flag that cleared
     * itself could not answer "did this ever happen?", which is exactly what
     * B3 will need to ask. */
    charger(false); ticks(3);
    ok((pmgr_mem_read(PMGR_CALIB_STATUS_ADDR) & PMGR_CALIB_ST_GATED) != 0u,
       "N1.13 GATED stays LATCHED after the charger is removed");

    /* calibrated: neither bit, and the pairing id is readable */
    sim_erase();
    { pmgr_calib_t g = good(); g.pairing_id = 42u; put(&g); }
    pmgr_calib_load();
    stub_reset(); pmgr_handlers_init();
    stub_scale_ok = true; stub_battery_level = 150u;
    charger(false); pmgr_tick_1s();
    charger(true);  ticks(3);
    ok(pmgr_mem_read(PMGR_CALIB_STATUS_ADDR) == 0u,
       "N1.13 valid record + charger -> NEITHER flag set");
    ok(pmgr_mem_read(PMGR_CALIB_PAIRING_ADDR) == 42u,
       "N1.13 the pairing id of the record IN FORCE is readable ($301)");
    ok(PMGR_CALIB_STATUS_ADDR > 0x00FFu,
       "N1.13 the flag sits ABOVE the M50753 map - invents nothing authentic");
}


/* N1.14 - §5 requires the calibration on EVERY threshold and on the level sent
 * to the Mac. ⛔ 2.350 did only the knee, and N1.1-N1.13 passed anyway because
 * not one of them exercised LOW, STAY_ASLEEP, DEAD or the reported level. The
 * omission was found by REVIEW, not by this suite (MEASUREMENTS 2.362). */
static void all_thresholds(void)
{
    sim_erase();
    { pmgr_calib_t g = good(); put(&g); }
    pmgr_calib_load();

    /* Apple's constants, and where THIS unit's fit says each should now fire */
    struct { const char *nm; uint16_t nom_raw; } t[] = {
        {"DEAD",         62u + 512u},
        {"STAY_ASLEEP",  70u + 512u},
        {"LOW",          78u + 512u},
        {"reset-90",     90u + 512u},
        {"knee",              720u},
    };
    int moved = 0;
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++) {
        uint16_t conv = pmgr_calib_threshold(t[i].nom_raw);
        printf("        %-12s nominal raw %3u -> this unit %3u  (level %d -> %d)\n",
               t[i].nm, t[i].nom_raw, conv,
               (int)t[i].nom_raw - 512, (int)conv - 512);
        if (conv != t[i].nom_raw) moved++;
    }
    ok(moved == 5, "N1.14 ALL FIVE thresholds are converted, not just the knee");

    /* ⛔ THE NEGATIVE CONTROL. Without a record every one must be the identity,
     * or an uncalibrated board would silently get different thresholds. */
    sim_erase(); pmgr_calib_load();
    int same = 0;
    for (unsigned i = 0; i < sizeof t / sizeof t[0]; i++)
        if (pmgr_calib_threshold(t[i].nom_raw) == t[i].nom_raw) same++;
    ok(same == 5, "N1.14 with NO record every threshold is the IDENTITY");

    /* the level reported to the Mac */
    sim_erase();
    { pmgr_calib_t g = good(); put(&g); }
    pmgr_calib_load();
    uint8_t raw_lvl = 79u;                       /* what 2.361 measured on battery */
    uint8_t sent    = pmgr_calib_to_nominal(raw_lvl);
    printf("        measured level %u -> reported to the Mac as %u\n", raw_lvl, sent);
    ok(sent > raw_lvl + 20u,
       "N1.14 the level SENT TO THE MAC is corrected to the nominal scale");
    sim_erase(); pmgr_calib_load();
    ok(pmgr_calib_to_nominal(raw_lvl) == raw_lvl,
       "N1.14 with NO record the reported level is passed through unchanged");
}


/* N1.15 - P1's ADC interlock after reconciliation. ⛔ The POINT of these cases is
 * that the BEHAVIOUR is P1's and the BOOKKEEPING is not: no state 4 reaches $21. */
static void reconciled_interlock(void)
{
    sim_erase();
    { pmgr_calib_t g = good(); g.a_q8 = PMGR_CALIB_A_NOMINAL; g.b_q8 = 0; put(&g); }
    pmgr_calib_load();                       /* CALIBRATED - so the gate stays out of it */
    stub_reset(); pmgr_handlers_init();
    stub_scale_ok = true; stub_battery_level = 150u;
    charger(false); pmgr_tick_1s();
    charger(true);  ticks(2);
    ok(hichg_on() && pmgr_mem_read(0x21u) == 1u,
       "N1.15 calibrated + good ADC -> charging normally (state 1)");

    /* the ADC goes bad */
    stub_scale_ok = false; ticks(2);
    ok(!hichg_on(), "N1.15 ADC scale bad -> HICHG DEASSERTED   [P1's behaviour]");
    ok(pmgr_mem_read(0x21u) == 3u,
       "N1.15 ⭐ $21 reports 3 - Apple's own 'done', NOT a state 4");
    ok(pmgr_mem_read(0x0302u) == 0x01u,
       "N1.15 ⭐ the fault is visible at $302, OUTSIDE Apple's map");

    /* ⛔ a low level must NOT clear the fault and let it re-enter */
    stub_battery_level = 50u; ticks(4);
    ok(!hichg_on() && pmgr_mem_read(0x0302u) == 0x01u,
       "N1.15 a low level does NOT clear the fault or re-assert HICHG");

    /* recovery is debounced: N-1 valid samples must NOT be enough */
    stub_battery_level = 150u; stub_scale_ok = true;
    pmgr_tick_1s(); pmgr_tick_1s();          /* 2 of the required 3 */
    ok(pmgr_mem_read(0x0302u) == 0x01u,
       "N1.15 ⭐ TWO valid samples do NOT clear it - the debounce is real");
    pmgr_tick_1s();                          /* the third */
    ok(pmgr_mem_read(0x0302u) == 0x00u,
       "N1.15 THREE valid samples clear the fault   [P1's N=3 rule preserved]");
    ok(pmgr_mem_read(0x21u) == 0u || pmgr_mem_read(0x21u) == 1u,
       "N1.15 recovery RE-SEEDS from state 0 rather than resuming");

    /* ⭐ and $21 must NEVER have held a value Apple cannot produce */
    ok(pmgr_mem_read(0x21u) <= 3u,
       "N1.15 ⭐⭐ $21 never exceeds 3 - no non-Apple value in Apple's map");

    /* the gate still takes precedence over the interlock */
    sim_erase(); pmgr_calib_load();          /* UNCALIBRATED */
    stub_reset(); pmgr_handlers_init();
    stub_scale_ok = false; stub_battery_level = 150u;
    charger(false); pmgr_tick_1s();
    charger(true);  ticks(3);
    ok(!hichg_on() && pmgr_mem_read(0x21u) == 3u,
       "N1.15 uncalibrated AND bad ADC -> still blocked, state 3");
    ok((pmgr_mem_read(PMGR_CALIB_STATUS_ADDR) & PMGR_CALIB_ST_GATED) != 0u,
       "N1.15 ⭐ the GATE claims it, not the interlock - static beats dynamic");
}


/* N1.16 - ⛔ THE ORDERING CASE. Every other test sets validity BEFORE entering
 * the tick, so the guard already sees the new value and an ordering bug is
 * invisible. Here the conversion fails DURING the tick, which is what actually
 * happens on hardware. Found by review 2026-09-28 (MEASUREMENTS 2.376). */
static void fault_within_the_tick(void)
{
    sim_erase();
    { pmgr_calib_t g = good(); g.a_q8 = PMGR_CALIB_A_NOMINAL; g.b_q8 = 0; put(&g); }
    pmgr_calib_load();
    stub_reset(); pmgr_handlers_init();
    stub_scale_ok = true; stub_battery_level = 150u;
    charger(false); pmgr_tick_1s();
    charger(true);  ticks(2);
    ok(hichg_on(), "N1.16 charging normally before the fault");

    /* ⭐ arm the failure so it happens INSIDE the next tick, at the recheck */
    stub_fail_at_recheck = true;
    pmgr_tick_1s();                          /* ⛔ EXACTLY ONE tick */
    ok(!hichg_on(),
       "N1.16 ⭐⭐ a conversion failing DURING the tick deasserts HICHG in THAT tick");
    ok(pmgr_mem_read(0x0302u) == 0x01u,
       "N1.16 and the fault is recorded in the same tick");
    ok(pmgr_mem_read(0x21u) == 3u,
       "N1.16 with the state at 3, not a value Apple never produces");
}


static uint8_t battery_level(void) { return pmgr_mem_read(0x001Eu); }

/* ⛔⛔ WHAT THE HOST ACTUALLY RECEIVES, review 2026-09-30. `$1E` returns
 * s_battery_level RAW; the battery reply sends pmgr_calib_to_nominal() of it
 * (pmgr_handlers.c:921). N1.18 first compared against the RAW value and so
 * measured a gap that the bench could never have seen. */
static uint8_t reported_level(void)
{
    return (uint8_t)pmgr_calib_to_nominal(pmgr_mem_read(0x001Eu));
}

static bool level_is_low_or_dead_flagged(void)
{
    uint8_t f = pmgr_mem_read(0x001Du);   /* ⭐ $1D. $1F is ref5V_Level - I had it wrong */
    return (f & (PF_BATTERY_LOW | PF_BATTERY_DEAD)) != 0u;
}

/* N1.17 - the OTHER injection site. N1.16 fails AIN7 (the reference); this
 * fails AIN0 (the battery), which the real ADC tracks in its own flag because
 * the two are genuinely different failures. Raised by review 2026-09-28.
 *
 * ⛔⛔ THE POINT: on timeout the converter substitutes 128, and 128 is a
 * PERFECTLY PLAUSIBLE reading - not low, not dead, and raw 640 is below the
 * 720 knee, so every value the charging machine looks at says "keep charging".
 * If the fault flag did not gate it, a dead battery channel would present as a
 * healthy pack under high charge. */
static void battery_channel_fault_within_the_tick(void)
{
    sim_erase();
    { pmgr_calib_t g = good(); g.a_q8 = PMGR_CALIB_A_NOMINAL; g.b_q8 = 0; put(&g); }
    pmgr_calib_load();
    stub_reset(); pmgr_handlers_init();
    stub_scale_ok = true; stub_battery_level = 150u;
    charger(false); pmgr_tick_1s();
    charger(true);  ticks(2);
    ok(hichg_on(), "N1.17 charging normally before the battery channel fails");

    stub_fail_at_battery_read = true;
    pmgr_tick_1s();                          /* ⛔ EXACTLY ONE tick */
    ok(!hichg_on(),
       "N1.17 ⭐⭐ an AIN0 timeout deasserts HICHG in THAT tick, not the next");
    ok(pmgr_mem_read(0x0302u) == 0x01u,
       "N1.17 the fault is recorded in the same tick");
    ok(pmgr_mem_read(0x21u) == 3u,
       "N1.17 state 3, not a value Apple never produces");
    /* ⭐ THE CONTROL that gives this test its meaning: the level the machine is
     * now holding is a PLAUSIBLE, CHARGEABLE one, so nothing about the value
     * itself could have stopped the charge.
     * ⚠️ NOT asserted as == 128: the truncating IIR averages the substituted
     * 128 against the previous 150, giving 139. Asserting 128 would have been
     * asserting that the smoothing is absent. The claim that matters is the
     * REGION, which holds for 128 and 139 alike. */
    ok(battery_level() > PMGR_ADC_LOW_LEVEL,
       "N1.17 the level now held is ABOVE low - reads as a healthy pack");
    ok((uint16_t)(battery_level() + 512u) < 720u,
       "N1.17 ⛔ and BELOW the 720 knee - every level test says 'keep charging'");
    ok(!level_is_low_or_dead_flagged(),
       "N1.17 and no low/dead conclusion is drawn from the substituted value");
}


/* N1.18 - ⛔⛔ WHY B3's TOP-OFF WAS 12 MINUTES SHORT (MEASUREMENTS 2.386/2.387).
 *
 * B3 measured state 1 = 2924 s and state 2 = 2202 s, ratio 0.753, against a
 * counter that simulation shows is EXACTLY symmetric (1.000).
 *
 * ⛔⛔ THIS TEST REFUTES THE CALIBRATION EXPLANATION. 2.387 claimed the missing
 * time came from comparing two different quantities:
 *
 *   what the bench TIMED   the REPORTED level reaching 208
 *                          = pmgr_calib_to_nominal(s_battery_level)
 *   what STOPS the counter s_battery_level vs pmgr_calib_threshold(720)
 *
 * ⭐ Those ARE different quantities - but the gap between them is ONE COUNT, not
 * the tens that 722 s would need. 2.387 measured 28 only because its helper read
 * `$1E` (s_battery_level RAW) instead of applying pmgr_calib_to_nominal() the way
 * the battery reply does (pmgr_handlers.c:921). Corrected here.
 *
 * ⬜ So B3's asymmetry is UNEXPLAINED and the question is reopened. */
static void knee_fires_before_the_reported_level(void)
{
    sim_erase();
    /* #1's own fit - a = 99.35, b = -23.26 (2.347) */
    { pmgr_calib_t g = good();
      g.a_q8 = (uint16_t)(99.35 * 256.0 + 0.5);
      g.b_q8 = (int16_t)(-23.26 * 256.0 - 0.5);
      put(&g); }
    pmgr_calib_load();
    stub_reset(); pmgr_handlers_init();
    stub_scale_ok = true;

    ok(pmgr_calib_valid(), "N1.18 the per-unit record loaded");

    /* ⭐ The threshold the CODE uses, on this unit's scale. */
    uint16_t thr = pmgr_calib_threshold(720u);
    ok(thr != 720u,
       "N1.18 the knee is CONVERTED for this unit - it is not a raw 720");

    /* Walk the level up one count per tick with the charger on, and record
     * BOTH events: the state 1 -> 2 transition, and the reported level hitting
     * 208. ⛔ The stub feeds s_battery_level through the same IIR the firmware
     * uses, so this includes the smoothing, not just the arithmetic. */
    charger(false); pmgr_tick_1s();
    charger(true);  pmgr_tick_1s();          /* state 0 -> 1 */
    ok(pmgr_mem_read(0x21u) == 1u, "N1.18 charging, state 1");

    int t_state2 = -1, t_reported208 = -1;
    for (int t = 0; t < 400; t++) {
        stub_battery_level = (uint8_t)(120u + t);   /* a steady climb */
        pmgr_tick_1s();
        if (t_state2 < 0 && pmgr_mem_read(0x21u) == 2u) t_state2 = t;
        if (t_reported208 < 0 && reported_level() >= 208u) t_reported208 = t;
        if (t_state2 >= 0 && t_reported208 >= 0) break;
    }
    ok(t_state2 >= 0, "N1.18 the knee was reached (state 2 entered)");
    ok(t_reported208 >= 0, "N1.18 and the REPORTED level reached 208");

    /* ⭐⭐ THE POINT: the counter stops at the FIRST of these, and the bench
     * timed the SECOND. Any gap is top-off time that never got counted. */
    printf("      ⭐ state 1->2 at tick %d; reported level hit 208 at tick %d"
           "  -> gap %d tick(s)\n", t_state2, t_reported208,
           t_reported208 - t_state2);
    /* ⛔⛔ ASSERT THE MAGNITUDE, NOT THE DIRECTION (review 2026-09-30).
     * The first version asserted only `state2 <= reported208` and
     * `reported208 != state2`. Both hold at a gap of 1 AND at a gap of 28, so
     * the test could not tell a correct implementation from the buggy one it
     * was written against - and it passed while supporting a WRONG conclusion
     * (2.387, retracted in 2.388). A test that cannot distinguish the claim
     * from its negation is not evidence for the claim. */
    ok(t_state2 <= t_reported208,
       "N1.18 the knee fires no later than the reported level reaches 208");
    ok(t_reported208 - t_state2 <= 3,
       "N1.18 ⭐⭐ the lag is a COUNT OR TWO, not tens - so the calibration "
       "CANNOT explain B3's 722 missing seconds");

    /* ⛔ CONTROL: a NOMINAL record - a = 100.00, b = 0 - so the gate PASSES but
     * the conversion is the identity. The two events must then coincide.
     * ⚠️ An EMPTY record is NOT the control: with no record the gate blocks
     * fast charge outright and state 2 is never entered at all, so the
     * comparison is undefined. That mistake was made first. */
    sim_erase();
    { pmgr_calib_t g = good();
      g.a_q8 = PMGR_CALIB_A_NOMINAL; g.b_q8 = 0; put(&g); }
    pmgr_calib_load();
    stub_reset(); pmgr_handlers_init(); stub_scale_ok = true;
    charger(false); pmgr_tick_1s(); charger(true); pmgr_tick_1s();
    int u2 = -1, u208 = -1;
    for (int t = 0; t < 400; t++) {
        stub_battery_level = (uint8_t)(120u + t);
        pmgr_tick_1s();
        if (u2 < 0 && pmgr_mem_read(0x21u) == 2u) u2 = t;
        if (u208 < 0 && reported_level() >= 208u) u208 = t;
        if (u2 >= 0 && u208 >= 0) break;
    }
    printf("      ⭐ NOMINAL control: state 1->2 at tick %d; reported 208 "
           "at tick %d  -> gap %d tick(s)\n", u2, u208, u208 - u2);
    ok(u2 == u208,
       "N1.18 ⭐ NOMINAL control: the two events COINCIDE, so the gap above is "
       "the per-unit calibration and nothing else");
}

int main(void)
{
    pmgr_calib_t c, d;
    uint8_t b[PMGR_CALIB_SIZE];

    puts("N1 - calibration record, CRC and gate (2.349)");

    /* ---- N1.1 mapping ---------------------------------------------------- */
    c = good(); pmgr_calib_encode(&c, b);
    ok(pmgr_calib_decode(b, &d), "N1.1  a good record decodes");
    ok(d.a_q8 == 25434u && d.b_q8 == -5955 && d.span_mv == 915u && d.rail_mv == 5251u,
       "N1.1  fields round-trip (a, b, span, RAIL)");

    /* ---- N1.2 threshold mapping ------------------------------------------ */
    sim_erase(); put(&c); pmgr_calib_load();
    ok(pmgr_calib_valid(), "N1.2  record loads as valid");
    uint16_t knee = pmgr_calib_threshold(720u);
    printf("        knee nominal 720 -> %u (level %d); 2.347 MEASURED 179.36\n",
           knee, (int)knee - 512);
    ok(knee == 692u, "N1.2  knee 720 -> 692, i.e. stored level 180");
    ok(pmgr_calib_threshold(590u) < 590u && pmgr_calib_threshold(574u) < 574u,
       "N1.2  LOW and DEAD convert downward, consistently");

    /* ---- N1.10 valid path uses the converted knee ------------------------- */
    ok(pmgr_calib_threshold(720u) != 720u,
       "N1.10 the knee comparison uses 180, NOT 208");

    /* ---- N1.11 the level reported to the Mac ------------------------------ */
    uint8_t back = pmgr_calib_to_nominal(180u);
    printf("        measured level 180 -> reported %u (expect ~208)\n", back);
    ok(back >= 206u && back <= 210u,
       "N1.11 a measured level converts BACK to the nominal scale");

    /* ---- N1.3 CRC: every single byte matters ------------------------------ */
    int bad = 0;
    for (uint8_t i = 0; i < 14u; i++) {
        pmgr_calib_encode(&c, b);
        b[i] ^= 0x01u;                       /* one bit, one byte */
        if (!pmgr_calib_decode(b, &d)) bad++;
    }
    printf("        %d of 14 single-byte flips rejected\n", bad);
    ok(bad == 14, "N1.3  flipping ANY byte of the record invalidates it");

    /* ---- N1.4 absent ------------------------------------------------------ */
    sim_erase();                             /* erased EEPROM = 0xFF */
    ok(!pmgr_calib_load(), "N1.4  erased EEPROM -> INVALID (gate engages)");

    /* ---- N1.5 torn: magic and version survive, CRC does not --------------- */
    c = good(); put(&c);
    EE[PMGR_CALIB_EE_BASE + 9] ^= 0xFFu;     /* span byte torn AFTER the crc was made */
    ok(!pmgr_calib_load(), "N1.5  torn record (valid magic, stale CRC) -> INVALID");

    /* ---- N1.6 slope out of range, CRC GOOD -------------------------------- */
    c = good(); c.a_q8 = 12800u; put(&c);    /* slope 50 - re-CRC'd, so only the bound catches it */
    ok(!pmgr_calib_load(), "N1.6  a out of range -> INVALID despite a valid CRC");

    /* ---- N1.7 residual: 2.338's own limit, enforced in firmware ----------- */
    c = good(); c.resid_q4 = 48u; put(&c);   /* 3.0 counts */
    ok(!pmgr_calib_load(), "N1.7  residual > 2 counts -> INVALID (2.338's limit)");
    c = good(); c.resid_q4 = 32u; put(&c);   /* exactly 2.0 - the boundary is INCLUSIVE */
    ok(pmgr_calib_load(), "N1.7  residual of exactly 2.00 counts is ACCEPTED");

    /* ---- N1.8 span: 2.339's own limit ------------------------------------- */
    c = good(); c.span_mv = 500u; put(&c);
    ok(!pmgr_calib_load(), "N1.8  span < 0.80 V -> INVALID (2.339's limit)");
    c = good(); c.span_mv = 800u; put(&c);
    ok(pmgr_calib_load(), "N1.8  span of exactly 0.80 V is ACCEPTED");

    /* ---- N1.9 THE GATE ----------------------------------------------------
     * ⛔ The record being rejected is NOT the safety claim. The claim is that
     * THE CHARGING PATH does not enter fast charge - so this must run THROUGH
     * pmgr_tick_1s(), exactly as design/CHARGING-ADC-INTERLOCK.md §4 requires
     * of the P1 tests. Testing pmgr_calib_valid() alone would pass while HICHG
     * was merrily asserted. */
    sim_erase();
    ok(!pmgr_calib_load(), "N1.9  no record -> pmgr_calib_valid() false");
    ok(pmgr_calib_threshold(720u) == 720u,
       "N1.9  with no record the NOMINAL line is used (identity), not a guess");
    ok(pmgr_calib_to_nominal(180u) == 180u,
       "N1.9  and the reported level is passed through unconverted");

    gate_through_the_tick();

    /* ---- N1.14 EVERY threshold, not just the knee -------------------------- */
    all_thresholds();

    /* ---- N1.13 §5's VISIBLE FLAG ------------------------------------------
     * ⛔ §5 requires the gate to be VISIBLE, not merely effective. After 2.352
     * a routine reflash produces an uncalibrated board that is otherwise
     * indistinguishable from a calibrated one. */
    visible_flag();

    /* ---- N1.15 P1's interlock, RECONCILED (2.374 option A) ---------------- */
    reconciled_interlock();

    /* ---- N1.16 the conversion fails DURING the tick ----------------------- */
    fault_within_the_tick();

    /* ---- N1.17 the BATTERY channel fails during the tick ------------------ */
    battery_channel_fault_within_the_tick();

    /* ---- N1.18 the knee fires on the INTERNAL level, not the reported one -- */
    knee_fires_before_the_reported_level();

    /* ---- N1.12 no overlap with PRAM --------------------------------------- */
    sim_erase();
    memset(&EE[0], 0x5Au, 264u);             /* PRAM's two 132-byte slots */
    c = good(); put(&c);
    int clean = 1;
    for (uint16_t i = 0; i < 264u; i++) if (EE[i] != 0x5Au) clean = 0;
    ok(clean, "N1.12 writing the record leaves PRAM 0-263 byte-identical");
    ok(PMGR_CALIB_EE_BASE >= 464u && PMGR_CALIB_EE_BASE + PMGR_CALIB_SIZE <= 512u,
       "N1.12 the record sits at 464..479, clear of the parked recorder (264-463)");

    printf("\n  %s - %d failure(s)\n", fails ? "N1 FAILED" : "N1 PASSED", fails);
    return fails ? 1 : 0;
}
