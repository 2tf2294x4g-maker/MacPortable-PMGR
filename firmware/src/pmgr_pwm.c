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

    /* ⛔ CCL OFF while LUT2 is configured - the AVR-DB erratum, see
     * pmgr_pwm_enable(). Reset leaves it off; this keeps init right if it is
     * ever called again. */
    CCL.CTRLA     = 0;
    CCL.LUT2CTRLA = 0;                                  /* and the LUT off: 31.3.1.1 */
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
     * ⛔⛔ THE WHOLE CCL MUST BE OFF FOR THIS WRITE (MEASUREMENTS.md 2.431).
     * Microchip's AVR128DB errata (DS80000915), "The CCL Must be Disabled to
     * Change the Configuration of a Single LUT" (silicon A4/A5): disabling
     * just the LUT is NOT enough. This used to write LUT2CTRLA twice with
     * CCL.CTRLA still enabled - which the datasheet allows - and on affected
     * parts the OUTEN write was IGNORED. PD6 stayed a port output driven
     * low, so pin 57 never carried the PWM on any board, unnoticed because
     * the Portable's LCD on that line was disconnected (2.189: "effect on
     * the LCD contrast line is not observed"). On a PowerBook 100 that line
     * is the BACKLIGHT BRIGHTNESS.
     *
     * ⛔⛔ AND THE LUT ITSELF MUST BE OFF TOO - TWO RULES, BOTH REQUIRED.
     * Datasheet 31.3.1.1: OUTEN is enable-protected by the LUT's OWN ENABLE;
     * it may only change while the LUT is disabled, or in the same write that
     * turns ENABLE on. The original code obeyed that (LUT2CTRLA = 0 first) and
     * broke the erratum; the first fix (32fab497) obeyed the erratum and
     * dropped the LUT-off step. Both left OUTEN clear - read back 0x01 from
     * the running chip, silicon A5 (REVID 0x15), MEASUREMENTS.md 2.431.
     *
     *     CCL off -> LUT2 off -> LUT2 = ENABLE (+OUTEN) -> CCL on
     *
     * LUT2 is the only LUT in use, so switching the CCL off for these writes
     * disturbs nothing else; the pin sits at its port value (low) for that
     * instant. */
    CCL.CTRLA     = 0;
    CCL.LUT2CTRLA = 0;
    CCL.LUT2CTRLA = on ? (uint8_t)(CCL_ENABLE_bm | CCL_OUTEN_bm)
                       : (uint8_t)(CCL_ENABLE_bm);
    CCL.CTRLA     = CCL_ENABLE_bm;
}

bool pmgr_pwm_is_enabled(void)
{
    return (CCL.CTRLA & CCL_ENABLE_bm) != 0u &&
           (CCL.LUT2CTRLA & (CCL_ENABLE_bm | CCL_OUTEN_bm)) ==
               (CCL_ENABLE_bm | CCL_OUTEN_bm);
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

#ifdef PMGR_PWM_DIAG
/* ⛔ DIAGNOSTIC BUILD ONLY - see pmgr_pwm.h. Layout is fixed so a UPDI read can be decoded by hand:
 *   [0]  0xD1 magic             [1]  snapshots taken (wraps)   [2]  SYSCFG.REVID (silicon revision)
 *   [3]  CCL.CTRLA              [4]  CCL.LUT2CTRLA             [5]  LUT2CTRLB    [6] LUT2CTRLC   [7] TRUTH2
 *   [8]  PORTMUX.CCLROUTEA      [9]  PORTMUX.TCAROUTEA         [10] TCA0.CTRLA   [11] TCA0.CTRLB [12] TCA0.CTRLD
 *   [13] CNT lo  [14] CNT hi    [15] CNT lo  [16] CNT hi  (two reads ~50 us apart: does the timer count?)
 *   [17] PER lo  [18] PER hi    [19] CMP0 lo [20] CMP0 hi
 *   [21] PORTD.DIR  [22] PORTD.OUT  [23] PORTD.PIN6CTRL
 *   [24] PD6 HIGH count of 125 samples   [25] PD6 LOW->HIGH edges seen in those samples
 *   [26] s_contrast */
#include <util/delay.h>
volatile uint8_t g_pwmdiag[27];

void pmgr_pwm_diag_snapshot(void)
{
    static uint8_t div;
    if (++div < 4u) return;                 /* every 4 s: keep the main loop's busy time small */
    div = 0;
    volatile uint8_t *d = g_pwmdiag;
    d[0] = 0xD1u; d[1]++; d[2] = SYSCFG.REVID;
    d[3] = CCL.CTRLA; d[4] = CCL.LUT2CTRLA; d[5] = CCL.LUT2CTRLB; d[6] = CCL.LUT2CTRLC; d[7] = CCL.TRUTH2;
    d[8] = PORTMUX.CCLROUTEA; d[9] = PORTMUX.TCAROUTEA;
    d[10] = TCA0.SINGLE.CTRLA; d[11] = TCA0.SINGLE.CTRLB; d[12] = TCA0.SINGLE.CTRLD;
    uint16_t c1 = TCA0.SINGLE.CNT; _delay_us(50); uint16_t c2 = TCA0.SINGLE.CNT;
    d[13] = (uint8_t)c1; d[14] = (uint8_t)(c1 >> 8); d[15] = (uint8_t)c2; d[16] = (uint8_t)(c2 >> 8);
    uint16_t per = TCA0.SINGLE.PER, cmp = TCA0.SINGLE.CMP0;
    d[17] = (uint8_t)per; d[18] = (uint8_t)(per >> 8); d[19] = (uint8_t)cmp; d[20] = (uint8_t)(cmp >> 8);
    d[21] = PORTD.DIR; d[22] = PORTD.OUT; d[23] = PORTD.PIN6CTRL;
    /* 125 samples x ~8 us = ~1 ms, one PWM period at 931 Hz (interrupts still run) */
    uint8_t hi = 0, edges = 0, prev = 0;
    for (uint8_t i = 0; i < 125u; i++) {
        uint8_t now = (PORTD.IN & PMGR_PWM_bm) ? 1u : 0u;
        hi += now; if (now && !prev && i) edges++; prev = now;
        _delay_us(8);
    }
    d[24] = hi; d[25] = edges; d[26] = s_contrast;
}
#endif
