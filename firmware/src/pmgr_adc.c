#include "pmgr_adc.h"
#include "pmgr_pins.h"
#include <util/delay.h>

/* A/D SCALING - what is true now, then how it was got wrong twice.
 *
 * ⛔ READ THIS FIRST: PMGR_ADC_REF5V_EXPECT IS 113, NOT 128 (MEASUREMENTS.md
 * 2.56). AD_Ref hangs off the SWITCHED (5/0) rail and measures 2.3 V with the
 * machine on, 0.029 V with it off - never the 2.5 V a nominal 100k/100k divider
 * on a 5 V rail would give. 2.3 / 5.203 * 255 = 113, and the ORIGINAL PMGR
 * reports 114-115.
 *
 * ✅ THE BATTERY-CHANNEL SCALING IS CORRECT - measured 2026-08-28, 2.20.
 *
 * A/D_FILTER (chip pin 52, J21 pin 40) was swept 6.5 -> 5.8 V in 0.1 V steps.
 * It is NOT a resistive divider - it is LEVEL-SHIFTED WITH GAIN:
 *
 *   ORIGINAL / PUBLISHED (2.20)    A/D_FILTER = (Vbatt - 5.169 ) * 2.041
 *   ⭐ MEASURED (2.51)             A/D_FILTER = (Vbatt - 5.1249) * 2.0324
 *
 * ⛔ BUILD TO THE MEASURED LINE. An earlier revision of this comment displayed
 * only the 2.20 form, so the SUPERSEDED reference was the one a reader took
 * away even though the correction was written directly beneath it.
 *
 * ⭐ The GAIN agrees: 2.0324 ± 0.0068 against 2.041 is -1.3 sigma, and that is
 * the first independent check 2.041 has ever had. The REFERENCE does not:
 * measured it is 5.125 V, a 44 mV correction. The replacement reads this same
 * node, so building to 5.169 leaves its ADC input ~78 mV off - 38 mV referred
 * to the pack, about 4 levels, permanently, in every reading.
 * ⚠️ That offset rests on 11 opportunistic pairs from one session; reproduce
 * before cutting copper to it.
 *
 * linear across every plateau (6.4 -> 2.513 predicted vs 2.507 measured;
 * 6.2 -> 2.105 vs 2.107; 6.0 -> 1.696 vs 1.687). That is sensible design: it
 * spends the ADC's range on 5.2-7 V, where a 6 V pack actually lives, instead
 * of 0-7 V.
 *
 * Feeding that into an 8-BIT conversion with a 5 V reference predicts Apple's
 * thresholds where they were independently observed:
 *
 *       count 70 (wake lockout) -> 5.841 V predicted, 5.85 measured
 *       count 62 (force-sleep)  -> 5.764 V predicted, 5.75 measured
 *
 * Both within 15 mV. A 10-bit model misses by 5x, so `>> 4` is right. ⭐ That
 * conclusion stands; it is about the BATTERY channel and is untouched by the
 * AD_Ref correction above.
 *
 * ---- two superseded conclusions, kept because each was instructive ----
 *
 * ⛔ SUPERSEDED: "PMGR_ADC_REF5V_EXPECT = 128 is right (2.5 V -> 2.5/5*255 =
 * 127.5)". The arithmetic was fine and the PREMISE was false - AD_Ref is not
 * 2.5 V. ⭐ A correct calculation on an unchecked assumption looks exactly like
 * a verified result, which is why this survived for weeks.
 *
 * ⛔ SUPERSEDED, EARLIER AND SEPARATE: a revision that marked the >> 4 shift
 * wrong. It took the ratio measured at ONE point (2.7214/6.5 = 0.4187) and
 * treated it as a DIVIDER. With an offset line that ratio changes at every
 * voltage and means nothing - extrapolating from a single point of a
 * two-parameter function inverted the conclusion. The sweep settled it; one
 * point never could have.
 *
 * ------------------------------------------------------------------------
 * SCALING - >> 4 is self-consistent, and there is a one-number check for it.
 *
 * The M50753 result is a SINGLE byte: `lda AD_Reg` ($EF) with no second
 * register read (pmgr.asm 528, 542, 556, 567). So Apple's thresholds are plain
 * 8-bit values - lowBatteryLevel 78, deadBatteryLevel 62, HICHGLevel 200,
 * max HICHG 220. (They are WRITTEN as `590-512` etc, which documents the
 * author's physical scale; what is stored and compared is the difference.)
 *
 * Our ADC is 12-bit with VREF = VDD, so >> 4 yields an 8-bit VDD-referenced
 * value - the same scale, IF the M50753's A/D reference was also VDD.
 *
 * ⛔⛔ THE 128 IN AN EARLIER VERSION OF THIS BLOCK IS RETRACTED. It read the
 * schematic's 100k/100k as a clean 2.5 V divider and concluded "read
 * ref5V_Level; 128 +/- a few counts confirms the entire scale". Every step of
 * that is now contradicted by measurement:
 *
 *   - the divider ratio is 0.450, not 0.500, on TWO boards (2.56, 2.59)
 *   - the top leg is fed from the SWITCHED (5/0) rail, so the check CANNOT be
 *     read at bring-up with the machine off - it reads ~0 V (2.56)
 *   - the expectation is 113, not 128, and it is settled from the 1 Hz tick
 *
 * ⚠️ This block SURVIVED the header's correction to 113 and went on instructing
 * 128 in the same file. See pmgr_adc.h for the live contract; do not re-derive
 * the expectation here.
 *
 * ⭐ What is still true and worth keeping: the host receives ref5V_Level in
 * every battery reply and can normalise BatteryLevel ratiometrically, so an
 * absolute scale error largely cancels FOR THE HOST. It does NOT cancel for the
 * PMGR's own low/dead comparisons, which are absolute. That is why a scale
 * check matters at all. */
/* ⛔ TRI-STATE, NOT A BOOL - see the self-check note in pmgr_adc_init().
 * AD_Ref hangs off the SWITCHED (5/0) rail, so it reads ~0 V whenever the
 * machine is off. A zero reading means "cannot verify right now", NOT "scale is
 * wrong", and latching a failure from it disables the battery thresholds for
 * the life of the session (MEASUREMENTS.md 2.56). */
typedef enum { SCALE_UNKNOWN = 0, SCALE_OK, SCALE_FAILED } scale_state_t;
static scale_state_t s_scale;
/* AD_Reg ($EF) - the most recent COMPLETED conversion, any channel. */
static uint8_t s_last_result;
/* ⛔ TWO FLAGS, DELIBERATELY. Found by a defect-class sweep 2026-09-06 looking
 * for the shape of the 2.56 P0: a verdict latched where recovery is possible.
 *
 *   s_adc_fault        RECOVERABLE. Gates pmgr_adc_scale_ok(), so it gates
 *                      whether PF_BATTERY_LOW/DEAD are ever acted on. Cleared
 *                      by any conversion that completes.
 *   s_adc_ever_faulted LATCHED, diagnostic only. Drives DBG1 and
 *                      pmgr_adc_faulted() so a transient stays visible at a pin
 *                      long after the ADC recovers.
 *
 * ⭐ WHY THE SPLIT. A single flag latched forever, and its consequence was
 * identical to the P0: pmgr_tick_1s() clears the battery flags on every tick,
 * so ONE transient timeout silently disabled low-battery protection for the
 * life of the session - no LOW, no sleep at STAY_ASLEEP, pack run flat.
 *
 * ⚠️ Refusing to act on a reading that is GOOD NOW, because of a past timeout,
 * is the same error shape the P0 fix rejected for s_scale ("a bad reading taken
 * once should not be permanent"). The module had two failure paths with
 * opposite recovery semantics and nothing explaining why. Now it has one rule.
 *
 * ⭐ The safety argument runs the same way: with the fault latched the machine
 * NEVER sleeps at DEAD battery. Acting on a currently-valid reading is safer
 * than acting on none. */
static bool s_adc_fault;
static bool s_adc_ever_faulted;

/* ⛔⛔ A THIRD FLAG, and the reason is a P1 found by review 2026-09-07.
 *
 * s_adc_fault is cleared by ANY completing conversion - which is correct for
 * "is the converter answering", and WRONG for "is the battery sample real".
 * pmgr_tick_1s() runs:
 *
 *     pmgr_battery_read()          AIN0  - fills s_battery_level
 *     pmgr_adc_recheck_scale(...)  AIN7  - a DIFFERENT channel
 *     if (!pmgr_adc_scale_ok()) ... else act on s_battery_level
 *
 * So a battery timeout raised s_adc_fault, the reference conversion in the
 * middle CLEARED it, and the gate then drew a low/dead conclusion from
 * PMGR_ADC_SAFE_VALUE (128) as though it were measured. ⛔ 128 is mid-scale -
 * "not low, not dead" - so a dead ADC presented as a HEALTHY battery, which is
 * the failure direction that runs a pack flat.
 *
 * ⚠️ Reachability is narrower than every tick, but not rare: recheck_scale()
 * returns early once SCALE_OK latches, so it only converts while the verdict is
 * UNKNOWN or FAILED - the first ticks after the machine comes on, and after any
 * recovery.
 *
 * ⭐ s_batt_fault tracks the BATTERY channel alone and clears only when that
 * channel itself completes. Same recovery rule as s_adc_fault - a bad reading
 * taken once is not permanent - applied to the right question. */
static bool s_batt_fault;
/* Did the conversion just attempted complete? ⛔ The RETURN VALUE cannot say:
 * PMGR_ADC_SAFE_VALUE is 128, which is also a perfectly legal reading. */
static bool s_last_convert_ok;

/* ⛔ THE WAIT MUST BE BOUNDED.
 *
 * An earlier revision polled RESRDY forever. If ADC0 ever fails to complete -
 * misconfiguration, clock problem, silicon state, a future edit - the PMGR
 * hangs, and convert() is reached TWICE BEFORE THE MACHINE IS RELEASED:
 *
 *     pmgr_adc_init()        the scale self-check
 *     pmgr_system_release()  via pmgr_battery_now(), pmgr.asm:202
 *
 * So an ADC that never finishes leaves the Macintosh PERMANENTLY HELD IN
 * RESET, with no indication of why - DBG2 is never even written, because the
 * hang happens before it is set.
 *
 * This bound is a defensive AVR measure, not a reproduction of anything Apple
 * did. At PRESC_DIV64 on 24 MHz the ADC clock is 375 kHz and a 12-bit
 * conversion takes ~40 us, so 2 ms is ~50x margin: it cannot fire in normal
 * operation, and it converts a dead board into a diagnosable one.
 *
 * ON TIMEOUT:
 *   - return a MID-SCALE value, not 0. Zero reads as a flat battery and would
 *     invite the very shutdown we are trying to avoid.
 *   - raise s_adc_fault, which forces pmgr_adc_scale_ok() false. That reuses
 *     the existing interlock: the battery thresholds are then not acted on at
 *     all, so a broken ADC cannot park the machine (see pmgr_tick_1s).
 *   - drive DBG1 high, readable with a DMM the moment it happens.
 */
#define PMGR_ADC_TIMEOUT_US   2000u
#define PMGR_ADC_SAFE_VALUE    128u   /* mid-scale: not low, not dead */

#ifdef PMGR_PARK_SLEEP
static bool s_discard_next;
void pmgr_adc_note_wake(void) { s_discard_next = true; }
#endif

static uint8_t convert(uint8_t muxpos)
{
#ifdef PMGR_PARK_SLEEP
    if (s_discard_next) {
        /* ⭐ One throwaway conversion on the same channel, bounded like the real
         * one; its result and any timeout are ignored (2.199 decision 2a). */
        s_discard_next = false;
        ADC0.MUXPOS  = muxpos;
        ADC0.INTFLAGS = ADC_RESRDY_bm;
        ADC0.COMMAND = ADC_STCONV_bm;
        for (uint16_t us = 0; us < PMGR_ADC_TIMEOUT_US; us++) {
            if ((ADC0.INTFLAGS & ADC_RESRDY_bm) != 0u) break;
            _delay_us(1);
        }
        ADC0.INTFLAGS = ADC_RESRDY_bm;
    }
#endif
    ADC0.MUXPOS  = muxpos;
    ADC0.INTFLAGS = ADC_RESRDY_bm;          /* discard any stale flag */
    ADC0.COMMAND = ADC_STCONV_bm;

    for (uint16_t us = 0; us < PMGR_ADC_TIMEOUT_US; us++) {
        if ((ADC0.INTFLAGS & ADC_RESRDY_bm) != 0u) {
            ADC0.INTFLAGS = ADC_RESRDY_bm;
            /* ⭐ AD_Reg ($EF) models the M50753's LAST CONVERSION RESULT,
             * whichever channel it was - not "the battery". Updated here, on
             * completion, and DELIBERATELY NOT on the timeout path below: if a
             * conversion never finishes, the real register still holds the
             * previous value. Substituting the safe value there would report a
             * conversion that did not happen. */
            /* ⭐ A completed conversion clears the RECOVERABLE fault. DBG1 and
             * s_adc_ever_faulted stay latched for diagnosis. */
            s_adc_fault   = false;
            s_last_convert_ok = true;
            s_last_result = (uint8_t)(ADC0.RES >> 4);
            return s_last_result;
        }
        _delay_us(1);
    }

    /* Conversion never completed. Fail loud and safe rather than hanging. */
    s_adc_fault        = true;
    s_adc_ever_faulted = true;          /* latched - never cleared */
    s_last_convert_ok  = false;
#ifndef PMGR_SAVE_MARKER            /* A/B builds use DBG1 as the PRAM-save marker */
    DBG1_PORT.DIRSET = DBG1_bm;
    DBG1_PORT.OUTSET = DBG1_bm;
#endif
    return PMGR_ADC_SAFE_VALUE;
}

/* False if the scale check failed OR the ADC is CURRENTLY faulted. Either way
 * the readings cannot be trusted to gate a shutdown.
 * ⚠️ "currently", not "has ever": s_adc_fault clears on the next completing
 * conversion. An earlier comment here said "has ever timed out", which was true
 * of the latched flag it used to read and is not true now. The latched history
 * lives in s_adc_ever_faulted / pmgr_adc_faulted(). */
bool pmgr_adc_scale_ok(void)
{
    return (s_scale == SCALE_OK) && !s_adc_fault && !s_batt_fault;
}

/* Re-evaluate the ADC scale against AD_Ref. Called from the 1 Hz tick.
 *
 * `ref_expected` says whether the SWITCHED (5/0) rail is up - i.e. whether
 * AD_Ref is PRESENT. The caller knows this because it drives SYS_PWR*.
 *
 * ⛔⛔ WHY THAT ARGUMENT EXISTS. A low reading is ambiguous on its own:
 *
 *     rail down, ADC fine   -> ~0, and that is CORRECT and expected
 *     rail up,   ADC broken -> ~0, and that is a GROSS FAILURE
 *
 * A first version of this fix treated EVERY low reading as "rail down, no
 * information". That silently defeated one of the three gross errors the check
 * exists to catch - a wrong shift (>> 2) truncates to 0, and an open MUX reads
 * anything including 0. The check would have reported nothing while the ADC
 * was demonstrably broken. Only the caller can disambiguate, so it must.
 *
 * ⭐ THE PASS IS LATCHED, and here that is enforced rather than merely stated.
 * The ADC's scale does not change with machine state - only the reference's
 * availability does - so a verdict reached against a live reference holds while
 * the machine is off, which is when the thresholds still gate a wake decision.
 * A hard failure after that point surfaces as a TIMEOUT via s_adc_fault, which
 * pmgr_adc_scale_ok() already folds in.
 *
 * FAILED is NOT latched: a bad reading taken once should not be permanent if a
 * later, valid one disagrees. */
void pmgr_adc_recheck_scale(bool ref_expected)
{
    if (s_scale == SCALE_OK) {
        return;                         /* latched - see above */
    }

    uint8_t ref = convert(ADC_MUXPOS_AIN7_gc);

    if (ref < PMGR_ADC_REF5V_FLOOR) {
        /* Reference absent. Only a failure if it should have been there. */
        if (ref_expected) {
            s_scale = SCALE_FAILED;
        }
        return;
    }
    s_scale = (ref >= (uint8_t)(PMGR_ADC_REF5V_EXPECT - PMGR_ADC_REF5V_TOL) &&
               ref <= (uint8_t)(PMGR_ADC_REF5V_EXPECT + PMGR_ADC_REF5V_TOL))
              ? SCALE_OK : SCALE_FAILED;
#ifndef PMGR_LOOP_MARKER            /* marker builds own DBG2 */
    if (s_scale == SCALE_OK) DBG2_PORT.OUTSET = DBG2_bm;
#endif
}

/* True if any conversion has ever timed out. DBG1 mirrors this on a pin. */
bool pmgr_adc_faulted(void) { return s_adc_ever_faulted; }

/* The M50753's AD_Reg holds the result of whichever channel converted LAST.
 * Exposed for the $E8 virtual address map so $EF carries the register's real
 * semantics rather than "battery voltage", which it only sometimes is. */
uint8_t pmgr_adc_last_result(void) { return s_last_result; }

void pmgr_adc_init(void)
{
    /* Disable the digital input buffers on both analogue pins. */
    ADC_IN_PORT.PIN0CTRL = PORT_ISC_INPUT_DISABLE_gc;   /* PD0 = AIN0. Net is ADC_IN (post-R1); AD_FILTER is the
                                                             pre-resistor net at J1-38. */
    PMGR_IN0_PORT.PIN7CTRL  = PORT_ISC_INPUT_DISABLE_gc;   /* PD7 = AIN7 */

    VREF.ADC0REF = VREF_REFSEL_VDD_gc;
    ADC0.CTRLC   = ADC_PRESC_DIV64_gc;
    ADC0.CTRLA   = ADC_ENABLE_bm | ADC_RESSEL_12BIT_gc;

    /* ⭐ SCALE SELF-CHECK - but it CANNOT be completed here.
     *
     * ⛔⛔ The "2.5 V divider" premise is RETRACTED (2.56). Apple 050-0219
     * sheet 12 shows R17 fed from the SWITCHED "(5/0)" rail, so on hardware:
     *
     *      machine OFF -> 0.0289 V      machine ON -> 2.3 V
     *
     * pmgr_adc_init() runs while the machine is OFF - that is the whole point
     * of a power manager - so the reading here is ~0 EVERY TIME. An earlier
     * revision latched that into a failure, and the guard in pmgr_tick_1s()
     * then cleared PF_BATTERY_LOW/DEAD on every tick for the life of the
     * session. The battery protection never engaged, and the symptom would
     * have been NOTHING HAPPENING: no LOW, no sleep at STAY_ASLEEP.
     *
     * ⭐ So the check is DEFERRED, not skipped: scale_recheck() runs from the
     * 1 Hz tick and latches a verdict the first time the rail is actually up.
     * A zero reading leaves the state UNKNOWN rather than FAILED.
     *
     * Discard one conversion first: the first result after enabling the ADC is
     * taken while the reference and the sampling capacitor are still settling.
     */
    (void)convert(ADC_MUXPOS_AIN7_gc);
    (void)convert(ADC_MUXPOS_AIN7_gc);
    s_scale = SCALE_UNKNOWN;
    /* The machine is off at init, so the reference is NOT expected here. */
    pmgr_adc_recheck_scale(false);

    /* Report it on DBG2 so the result is readable with a DMM the moment the
     * AVR is programmed - no host, no analyser, no ADB. HIGH = scale good.
     *
     * DBG pin allocation in the NORMAL build:
     *     DBG2  scale check passed        (set here)
     *     DBG1  ADC conversion TIMED OUT  (set by convert(), see above)
     *     DBG0  RTC/PIT sync timed out (set in main.c tick_init)
     * Between them: DBG2 high and DBG1 low is a healthy ADC; DBG1 high means
     * the converter stopped answering and every reading since is a stand-in.
     * The self-test build uses all three for its own result code instead. */
#ifndef PMGR_LOOP_MARKER            /* marker builds own DBG2 */
    DBG2_PORT.DIRSET = DBG2_bm;
    if (s_scale == SCALE_OK) DBG2_PORT.OUTSET = DBG2_bm;
    else            DBG2_PORT.OUTCLR = DBG2_bm;
#endif
}

/* AD_Batt - the line that kills real M50753s. Clamped by R1 + D1. */
/* ⭐ The ONE caller that owns s_batt_fault. Every battery sample the gate can
 * act on comes through here (pmgr_battery_now and pmgr_battery_read both call
 * it), so recording validity here covers the gate completely. */
uint8_t pmgr_adc_read_battery(void)
{
    uint8_t v = convert(ADC_MUXPOS_AIN0_gc);
    s_batt_fault = !s_last_convert_ok;
    return v;
}

/* AD_Ref - the +5V/2 divider.
 * ⚠️ ASSUMPTION: this is PMGR_IN0 (M50753 pin 54). The disassembly names the
 * channel but nothing in our pin docs maps a chip pin to A/D channel 0, so the
 * pairing is inferred, not verified. PMGR_IN0 also has NO clamp - unlike
 * AD_FILTER - which is acceptable only if it really is a divided +5V. */
uint8_t pmgr_adc_read_ref5v(void)   { return convert(ADC_MUXPOS_AIN7_gc); }
