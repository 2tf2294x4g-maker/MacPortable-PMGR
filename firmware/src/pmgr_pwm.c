#include "pmgr_pwm.h"
#include "pmgr_pins.h"
#include <avr/io.h>

/* Contrast lives in RAM below the PRAM block, so it survives a reset. Same
 * .noinit reasoning as the time in pmgr_pram.c; the host-build guard is here
 * too because Mach-O rejects the section name. */
#ifdef __AVR__
#  define PMGR_NOINIT __attribute__((section(".noinit")))
#else
#  define PMGR_NOINIT
#endif

static uint8_t s_contrast PMGR_NOINIT;      /* $2E */

/* ContrastTable, pmgr.asm 1190-1221: 2, 4, 6, ... = 2 + 2*X. Stored as an
 * expression rather than a table - it is exactly linear across all 32 entries,
 * and 64 bytes of flash for an affine function is not a faithfulness worth
 * having. If a future dump shows the real table deviating anywhere, replace
 * this with the literal bytes. */
static uint8_t contrast_to_count(uint8_t x)
{
    return (uint8_t)(2u + 2u * (x & PMGR_CONTRAST_MAX));    /* 2..64 of 66 */
}

static void pwm_set_count(uint8_t count)
{
    if (count > PMGR_PWM_STEPS) count = PMGR_PWM_STEPS;
    /* CMP0BUF, not CMP0: double-buffered, so the duty changes at the period
     * boundary and a mid-cycle write cannot emit a runt pulse. */
    TCA0.SINGLE.CMP0BUF =
        (uint16_t)(((uint32_t)count * (PMGR_PWM_PER + 1u)) / PMGR_PWM_STEPS);
}

void pmgr_pwm_init(void)
{
    /* ---- TCA0: the waveform ------------------------------------------
     * 24 MHz / 16 = 1.5 MHz; PER+1 = 1611 counts = 1074.0 us = 931.1 Hz.
     *
     * ⚠️ ROUTE IT TO PORTF. TCA0's default PORTMUX is PORTA, and PORTA is the
     * entire VIA data bus (pmgr_pins.h) - enabling CMP0EN there would drive
     * VIAD0 with a 931 Hz square wave. PF0 is unassigned on this board.
     *
     * CMP0EN is enabled deliberately even though the CCL taps WO0 internally:
     * it costs nothing on an unused pin and gives PF0 as a DIRECT PROBE POINT
     * for the timer, independent of the CCL. At bring-up that separates "the
     * timer is wrong" from "the CCL routing is wrong" with one scope move. */
    PORTMUX.TCAROUTEA = PORTMUX_TCA0_PORTF_gc;

    TCA0.SINGLE.CTRLB = TCA_SINGLE_WGMODE_SINGLESLOPE_gc | TCA_SINGLE_CMP0EN_bm;
    TCA0.SINGLE.PER   = PMGR_PWM_PER;
    TCA0.SINGLE.CMP0  = 0;
    TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV16_gc | TCA_SINGLE_ENABLE_bm;

    /* ---- CCL LUT2: the only route to PD6 ------------------------------
     * IN0 = TCA0 WO0, IN1/IN2 masked (read as 0), so the truth-table index is
     * just IN0. TRUTH = 0b10 makes the output follow IN0: a pass-through.
     *
     * OUTEN is left OFF here - pmgr_pwm_enable() owns it, mirroring the
     * original's PWM_Ctrl_Reg bit 0 (:201 on, :1353 off, :1396 on). */
    PORTMUX.CCLROUTEA = PORTMUX_LUT2_ALT1_gc;           /* LUT2 out -> PD6 */

    CCL.LUT2CTRLB = CCL_INSEL0_TCA0_gc | CCL_INSEL1_MASK_gc;
    CCL.LUT2CTRLC = CCL_INSEL2_MASK_gc;
    CCL.TRUTH2    = 0x02u;                              /* out = IN0 */
    CCL.LUT2CTRLA = CCL_ENABLE_bm;
    CCL.CTRLA     = CCL_ENABLE_bm;

    pwm_set_count(contrast_to_count(s_contrast));
}

void pmgr_pwm_enable(bool on)
{
    /* OUTEN gates the pin. With it clear, PD6 reverts to ordinary port
     * control - and pmgr_ports_init() already leaves it an output driven LOW,
     * so switching the PWM off parks the pin low rather than floating it.
     *
     * LUTnCTRLA is written with the LUT disabled, as the datasheet requires
     * for a configuration change. */
    CCL.LUT2CTRLA = 0;
    CCL.LUT2CTRLA = on ? (uint8_t)(CCL_ENABLE_bm | CCL_OUTEN_bm)
                       : (uint8_t)(CCL_ENABLE_bm);
}

void pmgr_contrast_set(uint8_t value)
{
    s_contrast = (uint8_t)(value & PMGR_CONTRAST_MAX);  /* 1179 and #%00011111 */
    pwm_set_count(contrast_to_count(s_contrast));       /* 1184-1187 */
}

uint8_t pmgr_contrast_get(void)
{
    return s_contrast;                                  /* 1167 lda $2E */
}

void pmgr_contrast_validate(void)
{
    /* pmgr.asm 190-196. Note the ORDER of the tests matters: `beq` rejects 0
     * BEFORE the `cmp #$20` range check, so 0 is invalid even though it is
     * numerically in range. A plain `>= 0x20` test would wrongly accept it. */
    if (s_contrast == 0u || s_contrast >= 0x20u)
        s_contrast = PMGR_CONTRAST_DEF;                 /* 196 ldm #4,$2E */
}
