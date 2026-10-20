/* Native stubs for the ADC lifecycle test.
 *
 * ⭐ MODELLING write-1-to-CLEAR. On hardware, writing ADC_RESRDY_bm to INTFLAGS
 * CLEARS the flag. A plain-struct mock SETS it, so convert() used to find the
 * flag already raised, return on its first loop iteration, and never reach the
 * polling/timeout path at all. The generated adc_native.c therefore rewrites
 * `ADC0.INTFLAGS = ADC_RESRDY_bm;` into adcstub_clear_resrdy() - the ONE
 * register semantic a struct cannot express. Everything else is the real code.
 *
 * With that in place the timeout path is reachable, so the recoverable-vs-
 * latched fault split can actually be tested rather than asserted. */
#ifndef ADCSTUB_H
#define ADCSTUB_H
#include "mock_avr_io.h"
#include "pmgr_adc.h"

extern uint8_t adc_next_count;   /* value a completing conversion yields */
extern int     adc_hang;         /* 1 = never complete, exercise the timeout */
/* ⭐ The internal DACREF channel (2.427) answers by COMMANDED level: HI at
 * DACREF 255, LO at 128, 0 at anything else - and 0 whenever AC0 is off, the
 * conservative reading of the datasheet's silence on that point. */
extern uint8_t adc_int_hi, adc_int_lo;

static inline void adcstub_clear_resrdy(void) { ADC0.INTFLAGS &= (uint8_t)~ADC_RESRDY_bm; }

/* Stands in for <util/delay.h>. One "microsecond" later the conversion lands,
 * unless the test asked it to hang. */
static inline void _delay_us(double us)
{
    (void)us;
    if (!adc_hang) {
        uint8_t v = adc_next_count;
        if (ADC0.MUXPOS == ADC_MUXPOS_DACREF0_gc) {
            v = 0u;
            if ((AC0.CTRLA & AC_ENABLE_bm) != 0u) {
                if (AC0.DACREF == PMGR_ADC_INT_DACREF_HI) v = adc_int_hi;
                if (AC0.DACREF == PMGR_ADC_INT_DACREF_LO) v = adc_int_lo;
            }
        }
        ADC0.RES = (uint16_t)((uint16_t)v << 4);
        ADC0.INTFLAGS |= ADC_RESRDY_bm;
    }
}

/* Stage a value for the NEXT conversion. */
static inline void adcstub_set(uint8_t count)
{
    adc_next_count = count;
    ADC0.INTFLAGS &= (uint8_t)~ADC_RESRDY_bm;
}
#endif
