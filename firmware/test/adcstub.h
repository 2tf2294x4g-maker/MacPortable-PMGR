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

extern uint8_t adc_next_count;   /* value a completing conversion yields */
extern int     adc_hang;         /* 1 = never complete, exercise the timeout */

static inline void adcstub_clear_resrdy(void) { ADC0.INTFLAGS &= (uint8_t)~ADC_RESRDY_bm; }

/* Stands in for <util/delay.h>. One "microsecond" later the conversion lands,
 * unless the test asked it to hang. */
static inline void _delay_us(double us)
{
    (void)us;
    if (!adc_hang) {
        ADC0.RES = (uint16_t)((uint16_t)adc_next_count << 4);
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
