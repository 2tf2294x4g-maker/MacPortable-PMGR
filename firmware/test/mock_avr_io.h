/* Host-side stand-in for <avr/io.h>, used ONLY by test/hostcheck.sh.
 * Enough of the AVR-DB register model to compile the firmware on the host
 * and catch typos, undeclared identifiers and type errors. It does not model
 * behaviour and must never be used for a real build. */
#ifndef MOCK_AVR_IO_H
#define MOCK_AVR_IO_H
#include <stdint.h>

typedef struct { volatile uint8_t DIR, DIRSET, DIRCLR, DIRTGL,
                                  OUT, OUTSET, OUTCLR, OUTTGL,
                                  IN, INTFLAGS, PORTCTRL,
                                  PIN0CTRL, PIN1CTRL, PIN2CTRL, PIN3CTRL,
                                  PIN4CTRL, PIN5CTRL, PIN6CTRL, PIN7CTRL; } PORT_t;
extern PORT_t PORTA, PORTB, PORTC, PORTD, PORTE, PORTF, PORTG;

typedef struct { volatile uint8_t CTRLA, CTRLB, CTRLC, CTRLD, CTRLE,
                                  SAMPCTRL, MUXPOS, MUXNEG, COMMAND,
                                  EVCTRL, INTCTRL, INTFLAGS, DBGCTRL;
                 volatile uint16_t RES, WINLT, WINHT; } ADC_t;
extern ADC_t ADC0;

typedef struct { volatile uint8_t ADC0REF, ADC1REF, DAC0REF; } VREF_t;
extern VREF_t VREF;

typedef struct { volatile uint8_t CTRLA, STATUS, INTCTRL, INTFLAGS, TEMP,
                                  DBGCTRL, CALIB, CLKSEL;
                 volatile uint16_t CNT, PER, CMP;
                 volatile uint8_t PITCTRLA, PITSTATUS, PITINTCTRL, PITINTFLAGS,
                                  PITDBGCTRL; } RTC_t;
extern RTC_t RTC;

/* TCB0 - used by pmgr_selftest.c for its tick capture. CNT and CCMP are
   16-bit on real silicon. */
typedef struct { volatile uint8_t CTRLA, CTRLB, reserved[2], EVCTRL, INTCTRL,
                                  INTFLAGS, STATUS, DBGCTRL, TEMP;
                 volatile uint16_t CNT, CCMP; } TCB_t;
extern TCB_t TCB0;

/* TCA0 - split into the SINGLE union member the firmware uses. */
typedef struct { struct { volatile uint8_t CTRLA, CTRLB, CTRLC, CTRLD,
                                           CTRLECLR, CTRLESET, CTRLFCLR, CTRLFSET,
                                           reserved0, EVCTRL, INTCTRL, INTFLAGS,
                                           reserved1[2], DBGCTRL, TEMP, reserved2[16];
                          volatile uint16_t CNT, reserved3[2], PER, CMP0, CMP1, CMP2,
                                    reserved4[2], PERBUF, CMP0BUF, CMP1BUF, CMP2BUF;
                        } SINGLE; } TCA_t;
extern TCA_t TCA0;

/* CCL - LUT2 drives the contrast PWM out to PD6. */
typedef struct { volatile uint8_t CTRLA, SEQCTRL0, SEQCTRL1, reserved0[2],
                                  INTCTRL0, reserved1, INTFLAGS,
                                  LUT0CTRLA, LUT0CTRLB, LUT0CTRLC, TRUTH0,
                                  LUT1CTRLA, LUT1CTRLB, LUT1CTRLC, TRUTH1,
                                  LUT2CTRLA, LUT2CTRLB, LUT2CTRLC, TRUTH2,
                                  LUT3CTRLA, LUT3CTRLB, LUT3CTRLC, TRUTH3; } CCL_t;
extern CCL_t CCL;

typedef struct { volatile uint8_t EVSYSCTRLA, CCLROUTEA, USBROUTEA, TWIROUTEA,
                                  TCAROUTEA, TCBROUTEA, TCDROUTEA, ACROUTEA,
                                  SPIROUTEA, USARTROUTEA[3]; } PORTMUX_t;
extern PORTMUX_t PORTMUX;

typedef struct { volatile uint8_t SWEVENTA, SWEVENTB, reserved0[14],
                                  CHANNEL0, CHANNEL1, CHANNEL2, CHANNEL3,
                                  CHANNEL4, CHANNEL5, CHANNEL6, CHANNEL7,
                                  reserved1[8], USERTCB0CAPT, USER[31];
               } EVSYS_t;
extern EVSYS_t EVSYS;

typedef struct { volatile uint8_t RSTFR, SWRR; } RSTCTRL_t;
extern RSTCTRL_t RSTCTRL;

extern volatile uint8_t SREG;

typedef struct { volatile uint8_t MCLKCTRLA, MCLKCTRLB, MCLKLOCK, MCLKSTATUS,
                                  OSCHFCTRLA, OSCHFTUNE, OSC32KCTRLA; } CLKCTRL_t;
extern CLKCTRL_t CLKCTRL;

#define PORT_ISC_INPUT_DISABLE_gc  0x04u
#define VREF_REFSEL_VDD_gc         0x05u
#define ADC_PRESC_DIV64_gc         0x06u
#define ADC_ENABLE_bm              0x01u
#define ADC_RESSEL_12BIT_gc        0x00u
#define ADC_MUXPOS_AIN0_gc         0x00u
#define ADC_STCONV_bm              0x01u
#define ADC_RESRDY_bm              0x01u
#define RTC_PI_bm                  0x01u
#define RTC_PITEN_bm               0x01u
#define RTC_CLKSEL_OSC32K_gc       0x00u
#define RTC_PERIOD_CYC32768_gc     0x0Eu
#define CLKCTRL_FRQSEL_24M_gc      0x09u


/* --- added so the host check covers the PWM/CCL/event-system paths --- */
#define TCA_SINGLE_CLKSEL_DIV16_gc         0x08u
#define TCA_SINGLE_ENABLE_bm               0x01u
#define TCA_SINGLE_WGMODE_SINGLESLOPE_gc   0x03u
#define TCA_SINGLE_CMP0EN_bm               0x10u
#define TCB_CAPT_bm                        0x01u
#define TCB_CAPTEI_bm                      0x01u
#define TCB_CLKSEL_DIV2_gc                 0x01u
#define TCB_CNTMODE_PW_gc                  0x07u
#define TCB_EDGE_bm                        0x10u
#define TCB_ENABLE_bm                      0x01u
#define CCL_ENABLE_bm                      0x01u
#define CCL_OUTEN_bm                       0x08u
#define CCL_INSEL0_TCA0_gc                 0x03u
#define CCL_INSEL1_MASK_gc                 0x00u
#define CCL_INSEL2_MASK_gc                 0x00u
#define PORTMUX_LUT2_ALT1_gc               0x10u
#define PORTMUX_TCA0_PORTF_gc              0x05u
#define PORT_ISC_FALLING_gc                0x03u
#define EVSYS_CHANNEL2_PORTD_PIN2_gc       0x4Au
#define EVSYS_USER_CHANNEL2_gc             0x03u
#define RSTCTRL_SWRST_bm                   0x01u
#define RTC_PERIOD_CYC512_gc               0x08u
#define ADC_MUXPOS_AIN7_gc                 0x07u

void ccp_write_io(void *addr, uint8_t value);
#endif
