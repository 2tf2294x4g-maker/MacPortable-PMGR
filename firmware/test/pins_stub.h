/* Native stand-in for the generated pmgr_pins.h, which includes <avr/io.h>.
 * Only the pins pmgr_adc.c actually touches. */
#ifndef PINS_STUB_H
#define PINS_STUB_H
#include "mock_avr_io.h"
#define PMGR_IN0_PORT   PORTD
#define AD_FILTER_PORT  PORTD
#define ADC_IN_PORT     PORTD
#define DBG1_PORT       PORTF
#define DBG2_PORT       PORTF
#define DBG1_bm         0x02u
#define DBG2_bm         0x04u
#endif
