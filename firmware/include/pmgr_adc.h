/* A/D. The M50753 has TWO channels (pmgr.asm 45-46):
 *     AD_Ref  = 0   a divider off the SWITCHED (5/0) rail. Drawn as 100k/100k
 *                   but MEASURES 0.451, not 0.5 - see the self-check below
 *     AD_Batt = 1   battery voltage
 * Both results are single bytes read from AD_Reg ($EF).
 *
 * On our board AD_FILTER is PD0 = AIN0, protected by R1 + D1.
 * PMGR_IN0 is PD7 = AIN7 - see the warning in pmgr_adc.c.
 */
#ifndef PMGR_ADC_H
#define PMGR_ADC_H

#include <stdint.h>
#include <stdbool.h>

/* Apple's own thresholds, in the M50753's A/D byte scale (pmgr.asm 148-149):
 *     lowBatteryLevel  = 590-512 = 78
 *     deadBatteryLevel = 574-512 = 62
 * These are only meaningful if our 8-bit values land on the SAME scale -
 * see pmgr_adc_scale_ok(). */
/* ⭐⭐ CALIBRATION, swept on Portable #2 2026-09-05 (MEASUREMENTS.md 2.50, 2.51):
 *
 *     raw = 100.00*V - 0.0     (raw = stored level + 512; V is PACK volts)
 *
 *     LOW 5.90 V - STAY_ASLEEP 5.82 V - DEAD 5.74 V - HICHG knee 7.20 V.
 *
 *     253 usable (volts, level) pairs from 283 acquisitions, 5.83-7.20 V, BOTH
 *     directions, bench supply at J17 with no cell in the loop.
 *
 *     ⚠️ ADOPTED FOR ADEQUACY, NOT EXACTNESS. An earlier version of this
 *     comment said "exactly 100 counts per volt, zero offset - a design
 *     constant". Both halves were overclaims and are RETRACTED. The full-range
 *     fit is 100.99 +/- 0.20 with a raw intercept of -6.69 +/- 1.27, so
 *     (100.00, 0) is rejected at about 5 sigma; the round number came from the
 *     lower 0.62 V subset alone. And "design constant" is a claim about Apple's
 *     intent that no measurement can support.
 *     ⭐ 100*V is kept because the disagreement does not matter where it is
 *     used: both models put every threshold within ~10 mV (LOW 5.90 vs 5.908,
 *     knee 7.20 vs 7.196). The two legs also differ on intercept by 2.6 levels,
 *     more than their formal errors, so extra digits would be false precision.
 *
 *     ⭐ The knee literal 208 was observed crossing at 7.20 V, ascending and
 *     descending - which under this model is 2.400 V/cell, inside the Cyclon
 *     float/cyclic gap. ⚠️ "2.400 exactly" is a CONSEQUENCE of the adopted
 *     round model, not an independent measurement: observed levels at the
 *     crossing were 208-209 at 7.193-7.203 V, and 2.43 saw 208 at 7.1713 V on
 *     a real charging pack. The agreement is good, not exact.
 *
 *     ⛔ SUPERSEDES the earlier 106.98*V - 44.8, which rested on two pairs.
 *     The 6.14 V pair AGREES with this line; the 7.00 V pair is 5 levels off,
 *     and one biased pair rotates the whole calibration - which is exactly why
 *     two points cannot test a line.
 *     ⚠️ WHY that pair is off is NOT established. An earlier version of this
 *     comment blamed charging overpotential; that was RETRACTED - overpotential
 *     raises the pack's TERMINAL voltage, which the meter and the PMGR's sense
 *     node both read, so it cannot make them disagree. A sense-point I*R
 *     difference can (5 levels = only 50 mV), but the path is unrecorded. The
 *     pair is superseded because 253 pairs disagree with it, not because its
 *     error has been explained.
 *
 *     ⛔ The knee is the LITERAL 208 (raw 720), not HICHGLevel 200.
 */
#define PMGR_ADC_LOW_LEVEL    78u
#define PMGR_ADC_DEAD_LEVEL   62u
/* HICHGLevel, $73 in the M50753 map. `ldm #712-512,HICHGLevel` at ResetEntry -
 * the source writes the difference, so the stored byte is 200. */
#define PMGR_ADC_HICHG_LEVEL 200u

/* The third, COMPUTED level - pmgr.asm 438-445: (low>>1) + (dead>>1).
 * With Apple's constants that is 39 + 31 = 70. Below it the PMGR sets
 * PowerFlags bit 6 and the machine can no longer be woken at all (:1366).
 * It is not a named constant in the original because it is derived. */
#define PMGR_ADC_STAY_ASLEEP_LEVEL \
    (uint8_t)((PMGR_ADC_LOW_LEVEL >> 1) + (PMGR_ADC_DEAD_LEVEL >> 1))

/* SCALE SELF-CHECK - one number validates the whole ADC scale.
 *
 * AD_Ref is PMGR_IN0 (J21-48, M50753 pin 54). Apple 050-0219 sheet 12 draws it
 * as R17/R153 = 100k/100k fed from the SWITCHED "(5/0)" rail, not the always-on
 * one. ⛔ The SWITCHED part is right; the 1:1 is NOT - measured ratio is 0.451.
 * Measured on Portable #2 (MEASUREMENTS.md 2.56):
 *
 *     machine OFF, rails up ->  0.0289 V      always-on rail 5.203 V
 *     machine ON            ->  2.3    V      switched rail   5.1 V (healthy)
 *
 * ⛔ WHY 2.3 V AND NOT 2.5: THE DIVIDER IS NOT 1:1. Measured at R17 with the
 * machine on - high end 5.1 V (the (5/0) rail, HEALTHY), low end 2.3 V, ratio
 * 0.451. An earlier revision of this comment claimed the rail was 4.6 V, a
 * diode below the always-on rail; that was RETRACTED - the rail was never
 * measured, only derived from an assumed 1:1 divider, and D3/D4 serve the
 * (5/3.7) backup rail, not this one. (5/0) arrives through a ferrite.
 *
 *     original PMGR:  2.3 / 5.1   * 255 = 115   reports 114-115, ratiometric
 *     OUR AVR:        2.3 / 5.203 * 255 = 113   VREF = VDD, NOT the (5/0) rail
 *
 * ⭐ MEASURED ON TWO BOARDS AT 4 DIGITS (2.62), different hybrid modules:
 *
 *     #2 original hybrid   2.315 / 5.203 * 255 = 113.46
 *     #1 MODERN remake     2.342 / 5.258 * 255 = 113.58     0.12 counts apart
 *
 * ⛔⛔ THAT IS NOT A CONFIRMATION OF THE CONSTANT BELOW, and an earlier revision
 * of this comment claiming it was is RETRACTED. Both readings were taken with an
 * M50753 INSTALLED; the constant is for a board with it REMOVED. Per 2.61 the
 * ~4.9 uA leaving the node may be the chip's own input current, in which case
 * our board reads ~126 instead - or a PCB leakage path, which would STAY on the
 * board and leave 113 correct. ⚠️ 113 is therefore NOT known to be wrong; it is
 * inherited from a configuration this board MAY or may not be in, and +/-16
 * spans both candidates either way. BRING-UP.md
 * stage 1 MEASURES it against a DMM-derived expectation; do not treat 113 as
 * validated for the replacement until that is done.
 *
 * ⭐ 2.62 does exclude one specific alternative: that the hybrid sets this node
 * at a voltage INDEPENDENT of the rail. The node tracks the (5/0) rail across a
 * 72 mV difference between boards. ⚠️ It does NOT show the hybrid has no
 * influence at all - the hybrid supplies that rail, and the 0.23 % board-to-board
 * ratio difference is ATTRIBUTED to R17's +/-1 % tolerance because that suffices,
 * not because a hybrid contribution was ruled out.
 *
 * THEREFORE THE VERDICT CANNOT BE REACHED AT INIT. An earlier revision of this
 * header is RETRACTED - both its expected value of 128 and its claim that the
 * check was measurable as soon as the AVR was programmed. AD_Ref reads ~0 V
 * whenever the machine is off, which is when init runs and the state a PMGR
 * spends most of its life in. The verdict latched false, pmgr_tick_1s() then
 * cleared PF_BATTERY_LOW/DEAD on every tick, and the battery protection never
 * engaged. The symptom is NOTHING HAPPENING: no LOW, no sleep at STAY_ASLEEP.
 *
 * So the verdict starts UNKNOWN and pmgr_adc_recheck_scale() settles it from
 * the 1 Hz tick, the first time the rail is actually up. A reading below
 * PMGR_ADC_REF5V_FLOOR is NO INFORMATION and leaves the verdict alone.
 *
 * ⚠️⚠️ 113 WAS MEASURED WITH THE ORIGINAL CHIP FITTED, AND THAT CHIP IS PART OF
 * THE CIRCUIT. MEASUREMENTS.md 2.61: the survey shows R17 and R153 are EQUAL
 * (~110k each), so a 0.450 ratio instead of 0.500 means ~5 uA leaves the node -
 * most plausibly the M50753's own IN0 input current. ⛔ An AVR ADC input does
 * not draw it, so on OUR board the same divider should rest near 2.60 V, not
 * 2.34, and read about 126:
 *
 *       original fitted    2.34 / 5.255 * 255 = 113.6
 *       AVR fitted (pred)  2.60 / 5.255 * 255 = 126.2
 *
 * 126 is inside 113 +/-16 (97-129) by THREE LEVELS. The check still passes.
 * ⚠️ The centre was calibrated against a load the replacement MAY remove - see
 * below: whether it does depends on what draws the current, which is unresolved.
 *
 * ⭐⭐ 2.64 NARROWED IT, and the two halves have DIFFERENT strengths:
 *
 *   ESTABLISHED  current really does leave the node. R17 and R153 are both
 *                marked 104 (100k nominal), and the measured ratio 0.448 falls
 *                outside even a +/-5% tolerance window (0.475-0.525) at ~490
 *                sigma. Tolerance cannot produce it.
 *   ⚠️ HYPOTHESIS what draws that current. The M50753's own IN0 input current is
 *                the LEADING explanation - the node reaches only chip pin 54 and
 *                an unpopulated J21-48 - but PCB surface leakage, contamination
 *                and an undocumented element have not been excluded, and a
 *                marking gives a NOMINAL value, not the fitted one.
 *
 * ⚠️ So ~126 is ONE PREDICTED reading - the one that follows if the M50753 is
 * the load. A PCB leakage path predicts ~113, because that load would STAY on
 * the board. ⛔ Neither is established, BOTH pass 113 +/-16, and this constant is
 * NOT known to be wrong. Do not "correct" a reading toward 113 without
 * measuring, and do not treat 126 as settled either.
 *
 * ✅ NO BOARD CHANGE IS NEEDED (2.65), and that holds whichever hypothesis is
 * right - which is why it can be said before the question is settled:
 *
 *      load was the CHIP     node relaxes to 2.578 V  ->  ~126
 *      load is PCB LEAKAGE   node stays at  2.315 V  ->  ~113
 *
 * Both are clean mid-scale references well inside the AVR's input range, and
 * BOTH pass 113 +/-16. ⚠️ So this constant is not known to be wrong - only
 * possibly off-centre. ⛔ Do NOT add a load to "restore" 2.315 V: that burns
 * current to reproduce an effect whose source is not even identified.
 *
 * ⭐ HOW TO SET THIS CONSTANT, once the board runs:
 *      1. meter J21-48 and VDD (J21-1), machine ON
 *      2. expected = V(node)/V(VDD) * 255
 *      3. read what the AVR reports; compare
 *   AGREE    -> the ADC is sound; set this constant to the DMM-DERIVED value,
 *               then TIGHTEN the tolerance below - +/-16 is loose only because
 *               the centre was unknown.
 *   DISAGREE -> a real fault (wrong VREF, wrong shift, wrong MUX). Fix THAT;
 *               moving this constant would hide it.
 * ⛔ Never set it from what the AVR itself reports - that is the quantity under
 * test, and matching the target to it makes the check unfalsifiable.
 * ⛔ NOT re-centred here: 126 is a prediction from an unverified 5 uA, and
 * swapping a measured number for a modelled one is the wrong trade. BRING-UP.md
 * stage 1 now MEASURES the reading before this verdict is trusted; re-centre on
 * that, not on this comment.
 * ⚠️ No probe at J21-48 can settle it - a current sink is Norton-equivalent to a
 * different divider ratio. Only R17/R153 out of circuit, or our own reading.
 *
 * +/-16 stays deliberately loose around the corrected centre. Its job is
 * catching GROSS errors, and it still does:
 *     wrong VREF (internal 2.048 V) -> input clips, reads 255
 *     wrong shift (>> 2)            -> 512, truncates to 0
 *     wrong MUX channel / open pin  -> anything
 */
/* ⛔⛔ MACINTOSH PORTABLE ONLY - MEASUREMENTS.md 2.69.
 *
 * This whole check assumes IN0 is AD_Ref: the R17/R153 divider off the switched
 * 5V rail, ratio 0.448 measured on BOTH Portable boards to four digits (2.62).
 *
 * On the POWERBOOK 100 that is FALSE. Sony 0-724-169-02 sheet 2 wires PMGR_IN0
 * to J12 pin 19 - the LCD connector - and the analogue measurement chain
 * reaches the chip on IN1 instead. PMGR_IN0 does not appear on the ANALOG
 * sheet at all. What drives J12-19 is inside the display module and is NOT in
 * that schematic package, so no PB100 expectation can be written here yet.
 *
 * ⚠️ Every bring-up stage runs on Portable #1, so this is correct today. The
 * day the board is fitted to a PowerBook 100, this check must be gated off or
 * given a value measured on that machine - NOT guessed.
 *
 * ⭐ The INPUT is safe either way: R1 1K in series and D1 BAT54S to both rails.
 */
#define PMGR_ADC_REF5V_EXPECT  113u
#define PMGR_ADC_REF5V_TOL     16u

/* ⛔ Below this, the switched rail is DOWN and AD_Ref is absent. Such a reading
 * carries NO information about scale and must never produce a verdict - see
 * pmgr_adc_recheck_scale(). ~0.65 V, far above the 0.029 V measured with the
 * machine off and far below the 113 expected with it on. */
#define PMGR_ADC_REF5V_FLOOR    32u

void    pmgr_adc_init(void);

/* True only once the ADC scale has been VERIFIED against a live AD_Ref. While
 * false the battery thresholds are NOT trustworthy and must not be acted on -
 * see the guard in pmgr_tick_1s().
 *
 * ⛔ NOT decided at init. AD_Ref sits on the SWITCHED rail and reads ~0 V while
 * the machine is off, which is when init runs, so the verdict starts UNKNOWN
 * and is settled later by pmgr_adc_recheck_scale(). MEASUREMENTS.md 2.56. */
bool    pmgr_adc_scale_ok(void);

/* Re-evaluate the scale against AD_Ref. Called from the 1 Hz tick.
 *
 * `ref_expected` = is the SWITCHED (5/0) rail up, so that AD_Ref is PRESENT?
 * The caller knows, because it drives SYS_PWR*.
 *
 * ⛔ A low reading means "rail down, no information" ONLY when the reference was
 * not expected. With the machine on, a low reading is a GROSS FAILURE - a wrong
 * shift truncates to 0 and an open MUX can read 0 - and must fail the check.
 * ⭐ The PASS is latched; FAILED is not. */
void    pmgr_adc_recheck_scale(bool ref_expected);

/* True once any conversion has EVER timed out - latched, diagnostic only, and
 * mirrored on DBG1 so a transient stays visible at a pin.
 * ⚠️ This is NOT what gates the battery thresholds. A timeout makes
 * pmgr_adc_scale_ok() false only until a conversion completes again; latching
 * that would silently disable low-battery protection for the session, which is
 * the 2.56 P0's failure shape. See the note in pmgr_adc.c. */
bool    pmgr_adc_faulted(void);

#ifdef PMGR_PARK_SLEEP
/* After waking from power-down the ADC reference may not have settled: the
 * next conversion is taken and thrown away (design §6.2 option a). */
void    pmgr_adc_note_wake(void);
#endif

/* AD_Reg ($EF) for the $E8 virtual address map: the most recent COMPLETED
 * conversion, whichever channel it was. NOT updated on a timeout - the real
 * register would still hold the previous value, so reporting the safe value
 * there would invent a conversion that never happened. */
uint8_t pmgr_adc_last_result(void);

/* Both return a byte on the M50753's scale, not the raw 12-bit result. */
uint8_t pmgr_adc_read_battery(void);
uint8_t pmgr_adc_read_ref5v(void);

#endif /* PMGR_ADC_H */
