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

PORT_t PORTA, PORTB, PORTC, PORTD, PORTE, PORTF, PORTG;
ADC_t  ADC0;
VREF_t VREF;

int fails = 0;
#define CHECK(c,msg) do{ if(c) printf("  PASS  %s\n",msg); \
                         else { printf("  FAIL  %s\n",msg); fails++; } }while(0)

/* Machine-off and machine-on rechecks, named for what they model. */
static void tick_machine_off(uint8_t count) { adcstub_set(count); pmgr_adc_recheck_scale(false); }
static void tick_machine_on (uint8_t count) { adcstub_set(count); pmgr_adc_recheck_scale(true ); }

/* pmgr_adc_init() also converts, so it needs a value staged too. */
static void init_with(uint8_t count) { adcstub_set(count); pmgr_adc_init(); }

int main(void)
{
    /* ---- 1. the P0 itself: init with the machine off must NOT latch a fail -- */
    init_with(0);                         /* AD_Ref reads ~0: rail is down */
    CHECK(!pmgr_adc_scale_ok(),
          "init with machine OFF: scale not yet verified (correctly)");

    tick_machine_off(1);                  /* many ticks, machine still off */
    tick_machine_off(0);
    tick_machine_off(2);
    CHECK(!pmgr_adc_scale_ok(),
          "repeated ticks with rail DOWN: still unverified, not failed");

    /* ⛔ THE REGRESSION. The shipped defect latched FAILED here, so the machine
     * could come on and the verdict would never recover. */
    tick_machine_on(PMGR_ADC_REF5V_EXPECT);
    CHECK(pmgr_adc_scale_ok(),
          "UNKNOWN -> OK once the machine is on and AD_Ref is valid  [P0 regression]");

    /* ---- 2. the pass is LATCHED across power-down -------------------------- */
    tick_machine_off(0);
    tick_machine_off(0);
    CHECK(pmgr_adc_scale_ok(),
          "verdict SURVIVES the machine going off (pass is latched)");

    /* ---- 3. a low reading WITH the machine on is a real failure ------------ */
    /* Fresh module state: re-init to clear the latch. */
    init_with(0);
    tick_machine_on(0);                   /* rail up but ADC reads 0 */
    CHECK(!pmgr_adc_scale_ok(),
          "machine ON but AD_Ref ~0: FAILS  [wrong shift / open MUX]");

    /* ---- 4. an out-of-window reading fails; a later good one recovers ------ */
    init_with(0);
    tick_machine_on((uint8_t)(PMGR_ADC_REF5V_EXPECT + PMGR_ADC_REF5V_TOL + 10u));
    CHECK(!pmgr_adc_scale_ok(), "reading above the window FAILS");
    tick_machine_on(PMGR_ADC_REF5V_EXPECT);
    CHECK(pmgr_adc_scale_ok(), "FAILED is not latched: a valid reading recovers");

    /* ---- 5. window edges --------------------------------------------------- */
    init_with(0);
    tick_machine_on((uint8_t)(PMGR_ADC_REF5V_EXPECT - PMGR_ADC_REF5V_TOL));
    CHECK(pmgr_adc_scale_ok(), "lower window edge PASSES");
    init_with(0);
    tick_machine_on((uint8_t)(PMGR_ADC_REF5V_EXPECT - PMGR_ADC_REF5V_TOL - 1u));
    CHECK(!pmgr_adc_scale_ok(), "one count below the window FAILS");

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

    printf("\n  %d failure(s)\n", fails);
    return fails ? 1 : 0;
}
