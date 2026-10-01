/* Native stubs for everything pmgr_handlers.c calls but does not define.
 *
 * ⭐ WHY THIS EXISTS. design/CHARGING-ADC-INTERLOCK.md §4 requires the P1 tests
 * to run THROUGH pmgr_tick_1s(), not against the ADC in isolation: the defect
 * is the ORDERING inside that function, so a test that stubs the ADC and calls
 * the charging logic directly would pass while the bug remained.
 *
 * ⭐⭐ The harness turned out far cheaper than the design doc estimated.
 * pmgr_handlers.c compiles natively UNCHANGED against mockinc/ once an
 * <avr/sleep.h> stub exists - no sed rewriting, no pins stub (the generated
 * pmgr_pins.h compiles against mock_avr_io.h as-is). Only 46 symbols need
 * providing, and all of them are mechanical.
 *
 * ⛔ THESE STUBS MUST STAY DUMB. The point is to drive the REAL charging code.
 * Anything clever here is a place for the test to pass for the wrong reason.
 * The one stub with behaviour is the ADC, because the ADC's behaviour is the
 * input the tests vary.
 */
#ifndef HANDLERS_STUB_H
#define HANDLERS_STUB_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ---- the ADC: the ONE stub the tests steer -------------------------------
 *
 * PMGR_ADC_SAFE_VALUE = 128 is the sentinel a failed conversion returns -
 * "mid-scale: not low, not dead". ⭐ That is correct for the low/dead decision
 * and WRONG for charging: 128 >= 90 so the state reset never fires, 128 < 208
 * so the knee is never reached, and the counter runs while HICHG stays enabled
 * (MEASUREMENTS 2.27x, design/CHARGING-ADC-INTERLOCK.md §2). */
extern uint8_t stub_battery_level;   /* what a conversion yields */
extern bool    stub_scale_ok;        /* pmgr_adc_scale_ok()'s answer */
extern bool    stub_adc_faulted;     /* pmgr_adc_faulted()'s answer */
extern int     stub_battery_reads;   /* counts conversions, for the C3 staleness test */

/* ⭐ Fail the scale check AT THE RECHECK, i.e. DURING the tick - not before it.
 * ⛔ Setting stub_scale_ok before pmgr_tick_1s() cannot catch an ordering bug,
 * because the value is already in place when the guard runs. That is exactly
 * how the stale-validity window survived (MEASUREMENTS 2.376). */
extern bool stub_fail_at_recheck;

/* ⭐⭐ THE BATTERY CHANNEL, injected SEPARATELY - review 2026-09-28.
 * The real ADC does NOT have one failure flag. pmgr_adc_scale_ok() is
 *     (s_scale == SCALE_OK) && !s_adc_fault && !s_batt_fault
 * and s_batt_fault exists precisely because a battery-channel timeout raised
 * s_adc_fault and the AIN7 reference conversion in the NEXT breath cleared it
 * (the P1 of 2026-09-07). stub_fail_at_recheck injects at AIN7, the LATER of
 * the two calls; this one injects at AIN0, the EARLIER, and returns the same
 * PMGR_ADC_SAFE_VALUE (128) the real converter substitutes on timeout.
 * ⛔ 128 is the DANGEROUS value: mid-scale reads as neither low nor dead, and
 * its raw 640 sits BELOW the 720 knee - so the level looks perfectly
 * chargeable. Nothing but the fault flag can stop high charge here. */
extern bool stub_fail_at_battery_read;
#define STUB_ADC_SAFE_VALUE 128u

void stub_reset(void);

#endif
