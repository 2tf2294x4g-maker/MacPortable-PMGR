/* ⭐ The PWM output on PD6 - pin 57, contrast on the Portable, BACKLIGHT on the PowerBook 100.
 * MEASUREMENTS 2.431: it was dead on every board because pmgr_pwm_enable() changed LUT2 with the CCL enabled,
 * which the AVR-DB erratum ignores. pwm_native.c routes every LUT2 write through that erratum model. */
#include <stdio.h>
#include <string.h>
#include <avr/io.h>
#include "pmgr_pwm.h"

PORT_t PORTA, PORTB, PORTC, PORTD, PORTE, PORTF, PORTG;
CCL_t CCL; TCA_t TCA0; PORTMUX_t PORTMUX;
int ccl_dropped_writes = 0;
int fails = 0;
#define CHECK(c,msg) do{ if(c) printf("  PASS  %s\n",msg); else { printf("  FAIL  %s\n",msg); fails++; } }while(0)

int main(void)
{
    memset(&CCL, 0, sizeof CCL);
    pmgr_pwm_init();
    CHECK((CCL.LUT2CTRLA & CCL_ENABLE_bm) && (CCL.CTRLA & CCL_ENABLE_bm), "init: LUT2 and the CCL enabled");
    CHECK(CCL.TRUTH2 == 0x02u, "init: TRUTH2 took (written with the CCL off)");
    CHECK((CCL.LUT2CTRLA & CCL_OUTEN_bm) == 0, "init: output still OFF (pmgr_pwm_enable owns OUTEN)");

    CHECK(!pmgr_pwm_is_enabled(), "output status: disabled");
    pmgr_pwm_enable(true);
    CHECK((CCL.LUT2CTRLA & CCL_OUTEN_bm) != 0, "⭐ enable(true): OUTEN SET despite the erratum  [2.431]");
    CHECK((CCL.CTRLA & CCL_ENABLE_bm) != 0, "enable(true): the CCL is back ON afterwards");

    CHECK(pmgr_pwm_is_enabled(), "output status: enabled");
    pmgr_pwm_enable(false);
    CHECK((CCL.LUT2CTRLA & CCL_OUTEN_bm) == 0 && (CCL.LUT2CTRLA & CCL_ENABLE_bm), "enable(false): OUTEN clear, LUT still enabled");
    pmgr_pwm_enable(true);
    CHECK((CCL.LUT2CTRLA & CCL_OUTEN_bm) != 0, "sleep/wake: OUTEN set again on the second enable");

    CHECK(ccl_dropped_writes == 0, "no LUT2 write was ever made with the CCL enabled");
    printf("\n  %d failure(s)\n", fails);
    return fails ? 1 : 0;
}
