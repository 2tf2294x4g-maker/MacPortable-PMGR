/* test_pwm: the AVR-DB CCL erratum as a register model (MEASUREMENTS 2.431). */
#ifndef PWMSTUB_H
#define PWMSTUB_H
#include <avr/io.h>
extern int ccl_dropped_writes;
/* "The CCL Must be Disabled to Change the Configuration of a Single LUT" (DS80000915, silicon A4/A5):
 * with CCL.CTRLA enabled, a LUT configuration write does not take. */
/* RULE 2 (datasheet 31.3.1.1): every LUT2 configuration bit except ENABLE is protected by LUT2's OWN ENABLE.
 * It may change only while the LUT is disabled, or in the same write that turns ENABLE on (0 -> 1). Otherwise
 * only the ENABLE bit takes. Hardware 2.431: 32fab497 wrote 0x41 over 0x01 and read back 0x01. */
static inline void ccl_lut2_write(volatile uint8_t *reg, uint8_t v)
{
    if (CCL.CTRLA & CCL_ENABLE_bm) { ccl_dropped_writes++; return; }          /* rule 1: the erratum */
    int lut_was_on = (CCL.LUT2CTRLA & CCL_ENABLE_bm) != 0;
    if (reg == &CCL.LUT2CTRLA) {
        if (lut_was_on && (v & CCL_ENABLE_bm)) {                               /* rule 2: ENABLE stays 1 */
            if ((v & (uint8_t)~CCL_ENABLE_bm) != (*reg & (uint8_t)~CCL_ENABLE_bm)) ccl_dropped_writes++;
            return;                                                            /* protected bits keep */
        }
        *reg = v; return;
    }
    if (lut_was_on) { ccl_dropped_writes++; return; }                          /* B/C/TRUTH while LUT on */
    *reg = v;
}
#endif
