/* The 46 symbols pmgr_handlers.c needs from the rest of the firmware.
 * See handlers_stub.h for why these stay deliberately dumb. */
#include "handlers_stub.h"
/* ⛔ <avr/io.h> from mockinc/, NOT the local mock_avr_io.h. The firmware sees
 * mockinc/avr/io.h via pins_native.h, and defining the registers against a
 * DIFFERENT header gives two incompatible sets of types for the same objects. */
#include <avr/io.h>
#include "pmgr_proto.h"

/* ---- AVR registers the mock declares but nobody defines ---------------- */
PORT_t PORTA, PORTB, PORTC, PORTD, PORTE, PORTF, PORTG;
ADC_t  ADC0;
VREF_t VREF;
RTC_t  RTC;
TCB_t  TCB0;
TCA_t  TCA0;
CCL_t  CCL;
PORTMUX_t PORTMUX;
EVSYS_t EVSYS;
SLPCTRL_t SLPCTRL;
CLKCTRL_t CLKCTRL;
volatile uint8_t SREG;

/* ⭐ These live in main.c on the real build (PMGR_PARK_SLEEP): the park sets
 * g_parked and the two ISRs record a wake cause. The harness never parks, so
 * plain definitions are enough. */
volatile bool g_parked, g_park_edge, g_park_pit;

void _delay_ms(double ms) { (void)ms; }
void _delay_us(double us) { (void)us; }

/* ---- the ADC: the tests' one lever ------------------------------------- */
uint8_t stub_battery_level = 200u;   /* a plausible charging level by default */
bool    stub_scale_ok      = true;
bool    stub_adc_faulted   = false;
bool    stub_fail_at_recheck = false;
bool    stub_fail_at_battery_read = false;
int     stub_battery_reads = 0;

void stub_reset(void)
{
    stub_battery_level = 200u;
    stub_scale_ok      = true;
    stub_adc_faulted   = false;
    stub_fail_at_recheck = false;
    stub_fail_at_battery_read = false;
    stub_battery_reads = 0;
    memset(&PORTA, 0, sizeof PORTA); memset(&PORTB, 0, sizeof PORTB);
    memset(&PORTC, 0, sizeof PORTC); memset(&PORTD, 0, sizeof PORTD);
    memset(&PORTE, 0, sizeof PORTE); memset(&PORTF, 0, sizeof PORTF);
    memset(&PORTG, 0, sizeof PORTG);
}

uint8_t pmgr_adc_read_battery(void)
{
    stub_battery_reads++;
    /* ⭐ the BATTERY channel times out mid-tick: the real converter raises
     * s_batt_fault (which gates pmgr_adc_scale_ok()) and substitutes 128 */
    if (stub_fail_at_battery_read) {
        stub_scale_ok = false;
        return STUB_ADC_SAFE_VALUE;
    }
    return stub_battery_level;
}
uint8_t pmgr_adc_read_ref5v(void)   { return 113u; }
uint8_t pmgr_adc_last_result(void)  { return stub_battery_level; }
bool    pmgr_adc_scale_ok(void)     { return stub_scale_ok; }
bool    pmgr_adc_faulted(void)      { return stub_adc_faulted; }
void    pmgr_adc_recheck_scale(bool ref_expected)
{
    (void)ref_expected;
    /* ⭐ the conversion fails HERE, mid-tick - the case an ordering bug hides */
    if (stub_fail_at_recheck) stub_scale_ok = false;
}
void    pmgr_adc_note_wake(void)    { }

/* ---- ADB ---------------------------------------------------------------- */
void    pmgr_adb_off(void)          { }
bool    pmgr_adb_pending(void)      { return false; }
void    pmgr_adb_queue(uint8_t c, uint8_t s, const uint8_t *d, uint8_t n)
                                    { (void)c; (void)s; (void)d; (void)n; }
uint8_t pmgr_adb_reply(uint8_t *b)  { (void)b; return 0; }
bool    pmgr_adb_service(void)      { return false; }
uint8_t pmgr_adb_status(void)       { return 0; }
void    pmgr_adb_status_clear(void) { }
void    pmgr_adb_wake_reset(void)   { }

/* ---- PRAM / time -------------------------------------------------------- */
static uint8_t s_pram[128];
uint8_t pmgr_pram_read(uint8_t i)             { return i < 128u ? s_pram[i] : 0u; }
void    pmgr_pram_write(uint8_t i, uint8_t v) { if (i < 128u) s_pram[i] = v; }
void    pmgr_pram_commit(void)                { }
void    pmgr_pram_zero(void)                  { memset(s_pram, 0, sizeof s_pram); }
bool    pmgr_pram_flush(void)                 { return true; }
uint8_t pmgr_pram_checksum(bool *valid)       { if (valid) *valid = true; return 0; }
static uint32_t s_time;
uint32_t pmgr_time_get(void)        { return s_time; }
void     pmgr_time_set(uint32_t t)  { s_time = t; }
void     pmgr_time_tick(void)       { s_time++; }

/* ---- PWM / contrast ----------------------------------------------------- */
static uint8_t s_contrast = 4u;
void    pmgr_pwm_enable(bool on)        { (void)on; }
void    pmgr_contrast_set(uint8_t v)    { s_contrast = v; }
uint8_t pmgr_contrast_get(void)         { return s_contrast; }
void    pmgr_contrast_validate(void)    { }

/* ---- ROM / bus / interrupt bookkeeping ---------------------------------- */
uint8_t pmgr_rom_read(uint16_t a)       { (void)a; return 0; }
bool    pmgr_bus_has_latched(void)      { return false; }
bool    pmgr_return_data(uint8_t cmd, const uint8_t *d, uint8_t n)
                                        { (void)cmd; (void)d; (void)n; return true; }
static uint8_t s_int_flags;
uint8_t pmgr_int_flags(void)            { return s_int_flags; }
bool    pmgr_int_pending(void)          { return s_int_flags != 0u; }
void    pmgr_set_int_flag(uint8_t f)    { s_int_flags |= f; }
void    pmgr_clear_int_flags(uint8_t f) { s_int_flags &= (uint8_t)~f; }
void    pmgr_ack_int(void)              { }
void    pmgr_int_service(void)          { }
