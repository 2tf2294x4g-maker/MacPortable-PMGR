/* ⭐ ADC SCALE-VERDICT LIFECYCLE — the state machine behind a P0.
 *
 * pmgr_adc_scale_ok() gates whether PF_BATTERY_LOW / PF_BATTERY_DEAD are ever
 * acted on. A defect here does not crash and does not show on a scope: it makes
 * the battery protection quietly never engage. MEASUREMENTS.md 2.56 records the
 * shipped version of exactly that.
 *
 * Nothing executed this logic before — the suite compiled the module and stopped
 * there. These cases exist because "it compiles" was the only evidence the
 * lifecycle had. */
#include "pmgr_adc.h"
#include "adcstub.h"
#include <stdio.h>

uint8_t adc_next_count = 0;
int     adc_hang       = 0;
uint8_t adc_int_hi     = 99u;   /* 2.427's prediction on #1 (VDD 5.255 V) */
uint8_t adc_int_lo     = 49u;

PORT_t PORTA, PORTB, PORTC, PORTD, PORTE, PORTF, PORTG;
ADC_t  ADC0;
VREF_t VREF;
AC_t   AC0;

int fails = 0;
#define CHECK(c,msg) do{ if(c) printf("  PASS  %s\n",msg); \
                         else { printf("  FAIL  %s\n",msg); fails++; } }while(0)

/* Machine-off and machine-on rechecks, named for what they model. */
static void tick_machine_off(uint8_t count) { adcstub_set(count); pmgr_adc_recheck_scale(false); }
static void tick_machine_on (uint8_t count) { adcstub_set(count); pmgr_adc_recheck_scale(true ); }

/* pmgr_adc_init() also converts, so it needs a value staged too. */
static void init_with(uint8_t count) { adcstub_set(count); pmgr_adc_init(); }

static void internal_returns(uint8_t hi, uint8_t lo) { adc_int_hi = hi; adc_int_lo = lo; }
/* The AD_Ref verdict, which now REPORTS ONLY ($305 bit 1) - sections 1-5 test
 * its lifecycle, which is unchanged. */
static bool in0_ok(void) { return (pmgr_adc_scale_bits() & PMGR_ADC_BIT_IN0_OK) != 0u; }

int main(void)
{
    /* ---- 1. the P0 itself: init with the machine off must NOT latch a fail -- */
    init_with(0);                         /* AD_Ref reads ~0: rail is down */
    CHECK(!in0_ok(),
          "init with machine OFF: scale not yet verified (correctly)");

    tick_machine_off(1);                  /* many ticks, machine still off */
    tick_machine_off(0);
    tick_machine_off(2);
    CHECK(!in0_ok(),
          "repeated ticks with rail DOWN: still unverified, not failed");

    /* ⛔ THE REGRESSION. The shipped defect latched FAILED here, so the machine
     * could come on and the verdict would never recover. */
    tick_machine_on(PMGR_ADC_REF5V_EXPECT);
    CHECK(in0_ok(),
          "UNKNOWN -> OK once the machine is on and AD_Ref is valid  [P0 regression]");

    /* ---- 2. the pass is LATCHED across power-down -------------------------- */
    tick_machine_off(0);
    tick_machine_off(0);
    CHECK(in0_ok(),
          "verdict SURVIVES the machine going off (pass is latched)");

    /* ---- 3. a low reading WITH the machine on is a real failure ------------ */
    /* Fresh module state: re-init to clear the latch. */
    init_with(0);
    tick_machine_on(0);                   /* rail up but ADC reads 0 */
    CHECK(!in0_ok(),
          "machine ON but AD_Ref ~0: FAILS  [wrong shift / open MUX]");

    /* ---- 4. an out-of-window reading fails; a later good one recovers ------ */
    init_with(0);
    tick_machine_on((uint8_t)(PMGR_ADC_REF5V_EXPECT + PMGR_ADC_REF5V_TOL + 10u));
    CHECK(!in0_ok(), "reading above the window FAILS");
    tick_machine_on(PMGR_ADC_REF5V_EXPECT);
    CHECK(in0_ok(), "FAILED is not latched: a valid reading recovers");

    /* ---- 5. window edges --------------------------------------------------- */
    init_with(0);
    tick_machine_on((uint8_t)(PMGR_ADC_REF5V_EXPECT - PMGR_ADC_REF5V_TOL));
    CHECK(in0_ok(), "lower window edge PASSES");
    init_with(0);
    tick_machine_on((uint8_t)(PMGR_ADC_REF5V_EXPECT - PMGR_ADC_REF5V_TOL - 1u));
    CHECK(!in0_ok(), "one count below the window FAILS");

    /* ---- 6. the fault flags: RECOVERABLE vs LATCHED ----------------------
     * Reachable now that the mock models write-1-to-clear. This is the
     * defect-class sweep's finding: one flag latched forever, with the same
     * consequence as the 2.56 P0. */
    init_with(0);
    tick_machine_on(PMGR_ADC_REF5V_EXPECT);
    CHECK(pmgr_adc_scale_ok(), "baseline good before the timeout case");
    CHECK(!pmgr_adc_faulted(), "no fault recorded yet");

    /* ⛔ recheck_scale() returns early once the pass is latched, so it cannot
     * be used to reach convert(). Use the battery read, which always converts. */
    adc_hang = 1;                          /* conversion never completes */
    adcstub_set(PMGR_ADC_REF5V_EXPECT);
    (void)pmgr_adc_read_battery();
    CHECK(pmgr_adc_faulted(), "timeout raises the LATCHED diagnostic flag");
    CHECK(!pmgr_adc_scale_ok(), "timeout suppresses scale_ok while it persists");

    adc_hang = 0;                          /* ADC works again */
    adcstub_set(PMGR_ADC_REF5V_EXPECT);
    (void)pmgr_adc_read_battery();
    CHECK(pmgr_adc_scale_ok(),
          "⭐ a completing conversion RECOVERS scale_ok  [sweep regression]");
    CHECK(pmgr_adc_faulted(),
          "but the diagnostic flag stays LATCHED for the session");

    /* ---- 7. ⛔ A BATTERY TIMEOUT MUST NOT BE CLEARED BY A REFERENCE SUCCESS --
     *
     * The gate in pmgr_tick_1s() runs in this order:
     *
     *     pmgr_battery_read()          <- AIN0, fills s_battery_level
     *     pmgr_adc_recheck_scale(...)  <- AIN7, a DIFFERENT channel
     *     if (!pmgr_adc_scale_ok()) ... else act on s_battery_level
     *
     * ⛔ s_adc_fault was cleared by ANY completing conversion, so the reference
     * conversion in the middle wiped the fault raised by the battery timeout
     * either side of it - and the gate then acted on PMGR_ADC_SAFE_VALUE (128,
     * mid-scale) as though it were a real measurement. 128 reads as "not low,
     * not dead", so a dead ADC presents as a HEALTHY battery.
     *
     * ⚠️ The window is narrower than every tick: recheck_scale() returns early
     * once SCALE_OK latches, so it only converts while the verdict is UNKNOWN
     * or FAILED. That is precisely the first ticks after the machine comes on -
     * and after any recovery - which is not a rare state. */
    init_with(0);                          /* s_scale UNKNOWN, nothing latched */
    adc_hang = 1;
    adcstub_set(PMGR_ADC_REF5V_EXPECT);
    (void)pmgr_adc_read_battery();         /* battery conversion TIMES OUT */
    CHECK(!pmgr_adc_scale_ok(), "battery timeout suppresses the gate");

    adc_hang = 0;
    tick_machine_on(PMGR_ADC_REF5V_EXPECT); /* reference converts fine, verdict OK */
    CHECK(!pmgr_adc_scale_ok(),
          "⛔ a REFERENCE success must NOT clear a BATTERY timeout  [P1]");

    adcstub_set(PMGR_ADC_REF5V_EXPECT);
    (void)pmgr_adc_read_battery();         /* a real battery sample at last */
    CHECK(pmgr_adc_scale_ok(),
          "gate recovers once the BATTERY channel itself converts");

    /* ---- 8. the floor is where 2.56 measured it ---------------------------- */
    CHECK(PMGR_ADC_REF5V_FLOOR > 2u && PMGR_ADC_REF5V_FLOOR < PMGR_ADC_REF5V_EXPECT,
          "FLOOR sits above the machine-off reading and below the expected count");

    /* ---- 9. ⭐ THE INTERNAL CHECK (2.427) - Portable build ------------------ */
    internal_returns(99u, 49u);
    init_with(0);
    CHECK((pmgr_adc_scale_bits() & PMGR_ADC_BIT_INTERNAL_OK) != 0u,
          "internal check settles AT INIT, machine off (needs no rail)");
    CHECK(pmgr_adc_scale_ok(),
          "⭐ protection is trusted AT INIT, machine off - no rail needed");
    CHECK(pmgr_adc_internal_hi() == 99u && pmgr_adc_internal_lo() == 49u,
          "HI/LO readings exposed for $303/$304");
    CHECK(AC0.CTRLA == 0u && AC0.MUXCTRL == 0u && AC0.DACREF == 0xFFu &&
          VREF.ACREF == 0u && ADC0.SAMPCTRL == 0u,
          "AC0, ACREF, DACREF and SAMPCTRL restored after the check");
    tick_machine_on(PMGR_ADC_REF5V_EXPECT);
    CHECK(pmgr_adc_scale_ok() && pmgr_adc_scale_bits() == 0x03u,
          "both pass -> scale_ok, bits 0 and 1");

    /* 9b. each gross fault the check exists for fails it, even with AD_Ref fine */
    {
        static const struct { uint8_t hi, lo; const char *what; } bad[] = {
            {127u,  63u, "wrong VREF 4.096 V (HI 127) FAILS"},
            {208u, 104u, "wrong VREF 2.5 V (HI 208) FAILS"},
            {255u, 255u, "wrong VREF 2.048 V (clips 255) FAILS"},
            {141u,  56u, "wrong shift >>2 (wraps) FAILS"},
            { 49u,  24u, "wrong shift >>5 (HI ~50) FAILS"},
            { 99u,  99u, "stuck MUX - LO does not halve - FAILS"},
            { 99u,  70u, "wrong MUX onto an external node FAILS"},
            {  0u,   0u, "DACREF not reaching the ADC (0/0) FAILS"},
        };
        for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            internal_returns(bad[i].hi, bad[i].lo);
            init_with(0);
            tick_machine_on(PMGR_ADC_REF5V_EXPECT);   /* AD_Ref passes */
            CHECK(!pmgr_adc_scale_ok() &&
                  (pmgr_adc_scale_bits() & PMGR_ADC_BIT_INTERNAL_OK) == 0u, bad[i].what);
        }
    }

    /* 9c. FAILED is retried; OK latches */
    internal_returns(127u, 63u);
    init_with(0);
    tick_machine_on(PMGR_ADC_REF5V_EXPECT);
    CHECK(!pmgr_adc_scale_ok(), "bad internal reading: not OK");
    internal_returns(99u, 49u);
    tick_machine_on(PMGR_ADC_REF5V_EXPECT);
    CHECK(pmgr_adc_scale_ok(), "FAILED is not latched: a good pair recovers");
    internal_returns(0u, 0u);
    tick_machine_on(PMGR_ADC_REF5V_EXPECT);
    CHECK(pmgr_adc_scale_ok() && pmgr_adc_internal_hi() == 99u,
          "OK latches: the check does not run again after a pass");

    /* 9d. ⛔ AD_Reg ($EF) never shows an internal conversion */
    internal_returns(127u, 63u);              /* keep the check re-running */
    init_with(0);
    tick_machine_on(PMGR_ADC_REF5V_EXPECT);   /* IN0 OK, internal FAILED */
    adcstub_set(77u);
    (void)pmgr_adc_read_battery();
    tick_machine_on(77u);                     /* only the internal check converts */
    CHECK(pmgr_adc_last_result() == 77u,
          "$EF still holds the battery reading after the internal check ran");

    /* 9e. a timeout inside the check is not a verdict */
    internal_returns(99u, 49u);
    adcstub_set(0u);
    adc_hang = 1;
    pmgr_adc_init();
    CHECK((pmgr_adc_scale_bits() & PMGR_ADC_BIT_INTERNAL_OK) == 0u && !pmgr_adc_scale_ok(),
          "internal timeout: no pass, gate held by the fault");
    CHECK(pmgr_adc_internal_hi() != 128u && pmgr_adc_internal_lo() != 128u,
          "the timeout's stand-in (128) is never published at $303/$304");
    adc_hang = 0;
    tick_machine_on(PMGR_ADC_REF5V_EXPECT);
    adcstub_set(80u);
    (void)pmgr_adc_read_battery();
    CHECK(pmgr_adc_scale_ok(), "ADC answers again: the check passes next tick");

    /* ---- 10. ⭐ ONE IMAGE, BOTH MACHINES: IN0 reports, never decides ------ */
    internal_returns(99u, 49u);
    init_with(200u);                          /* PB100: whatever J12-19 carries */
    tick_machine_on(200u);
    CHECK(pmgr_adc_scale_ok() && !in0_ok(),
          "an IN0 that fails (LCD line on a PB100) does NOT disable protection");
    internal_returns(127u, 63u);
    init_with(PMGR_ADC_REF5V_EXPECT);
    tick_machine_on(PMGR_ADC_REF5V_EXPECT);
    CHECK(!pmgr_adc_scale_ok() && in0_ok(),
          "⛔ a 'perfect' IN0 (a knob at 113) can NOT pass a bad ADC  [2.69]");

    /* ---- 11. the pure verdict, at its edges (both builds) ------------------ */
    CHECK( pmgr_adc_internal_in_spec( 90u, 45u), "HI 90 (window floor) passes");
    CHECK(!pmgr_adc_internal_in_spec( 89u, 45u), "HI 89 fails");
    CHECK( pmgr_adc_internal_in_spec(115u, 57u), "HI 115 (window top) passes");
    CHECK(!pmgr_adc_internal_in_spec(116u, 58u), "HI 116 fails");
    CHECK( pmgr_adc_internal_in_spec(100u, 49u), "2*LO = HI-2 passes");
    CHECK(!pmgr_adc_internal_in_spec(100u, 48u), "2*LO = HI-4 fails");
    CHECK( pmgr_adc_internal_in_spec(100u, 51u), "2*LO = HI+2 passes");
    CHECK(!pmgr_adc_internal_in_spec(100u, 52u), "2*LO = HI+4 fails");

    printf("\n  %d failure(s)\n", fails);
    return fails ? 1 : 0;
}
