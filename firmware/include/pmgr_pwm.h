/* Contrast PWM on PMGR_PWM (PD6).
 *
 * ⭐ MEASURED - MEASUREMENTS.md 2.15, on a real Portable:
 *
 *     period 1074.00 us  =  931 Hz, CONSTANT at every contrast setting
 *     duty   ContrastTable[X] / 66,  X = contrast & 0x1F
 *     table  2 + 2*X  ->  2..64 of 66  =  3.0 % .. 97.0 %
 *
 * From pmgr.asm 1177-1189: SetContrast writes BOTH timer halves, Timer_1 =
 * table[X] and Timer_2 = 66 - table[X], so the period is pinned at 66 counts
 * and only the duty moves.
 *
 * ⚠️ PD6 HAS NO TIMER OUTPUT. This is the whole reason this module exists.
 * TCA0's WOn reaches only bits 0-5 of whichever port it is muxed to; every TCB
 * WO option is PA2/PF4, PA3/PF5, PC0/PB4, PB5/PC1; TCD0 reaches PA4-7, PB4-7,
 * PF0-3, PG4-7. None of them is PD6. Exactly one peripheral can drive it:
 *
 *     PORTMUX_LUT2_ALT1_gc      -- In: PD0,PD1,PD2   Out: PD6
 *
 * so the chain is  TCA0 -> CCL LUT2 (pass-through) -> PD6.
 *
 * Anyone who reaches for TCA0.SINGLE.CMP0 on PD6 gets a dead pin and no error,
 * which is why this is spelled out here rather than left in the .c file.
 */
#ifndef PMGR_PWM_H
#define PMGR_PWM_H

#include <stdint.h>
#include <stdbool.h>

/* 24 MHz / 16 = 1.5 MHz; 1611 counts = 1074.0 us = 931.1 Hz.
 * PER is far larger than 66 on purpose: we need Apple's 66 duty STEPS, not a
 * 66-count period, and a bigger PER puts each step on an exact boundary. */
#define PMGR_PWM_PER     1610u      /* PER, so 1611 counts */
#define PMGR_PWM_STEPS     66u      /* Timer_1 + Timer_2, pmgr.asm 1182 */

/* Contrast is 5 bits (pmgr.asm 1179: and #%00011111). */
#define PMGR_CONTRAST_MAX  0x1Fu
#define PMGR_CONTRAST_DEF     4u    /* ldm #4,$2E - pmgr.asm 196 */

void    pmgr_pwm_init(void);            /* TCA0 + CCL routing. Output OFF. */

/* seb 0,PWM_Ctrl_Reg (:201, :1396) / clb 0,PWM_Ctrl_Reg (:353, :1353). */
void    pmgr_pwm_enable(bool on);

/* SetContrast, pmgr.asm 1177-1189. Stores the value and updates the duty.
 * Takes the raw host byte; masks to 5 bits itself. */
void    pmgr_contrast_set(uint8_t value);

/* GetContrast - lda $2E (:1167). The stored value, not the duty. */
uint8_t pmgr_contrast_get(void);

/* pmgr.asm 190-196. $2E lives in RAM BELOW the PRAM block ($2F..$AE), so
 * InitRAM never clears it and it survives a reset - but after a power loss it
 * is garbage. The original validates rather than initialises:
 *
 *     190  lda $2E
 *     191  bmi LAB_E8DE      ; >= $80  -> default
 *     192  beq LAB_E8DE      ; == 0    -> default
 *     193  cmp #$20 / bcc LAB_E8E1   ; < $20 -> keep
 *     196  ldm #4,$2E        ; otherwise 4
 *
 * Call once at startup, before SetContrast. */
void    pmgr_contrast_validate(void);

#endif /* PMGR_PWM_H */
