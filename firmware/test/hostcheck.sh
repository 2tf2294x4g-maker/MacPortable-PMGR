#!/bin/sh
# Syntax/type check the firmware on the host with mock AVR headers.
# Catches typos, undeclared identifiers and type errors.
# It does NOT verify register semantics, timing, or real-silicon behaviour.
set -e
cd "$(dirname "$0")/.."
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
mkdir -p "$TMP/avr" "$TMP/util"
cp test/mock_avr_io.h "$TMP/avr/io.h"
cat > "$TMP/avr/interrupt.h" <<'X'
#ifndef MOCK_INT_H
#define MOCK_INT_H
#define ISR(v) void v##_handler(void)
#define RTC_PIT_vect rtc_pit
static inline void sei(void) {}
static inline void cli(void) {}
#endif
X
cat > "$TMP/avr/cpufunc.h" <<'X'
#ifndef MOCK_CPUFUNC_H
#define MOCK_CPUFUNC_H
#include <stdint.h>
void ccp_write_io(void *addr, uint8_t value);
#define _PROTECTED_WRITE(reg, val)  ((reg) = (val))
#endif
X
cat > "$TMP/util/delay.h" <<'X'
#ifndef MOCK_DELAY_H
#define MOCK_DELAY_H
static inline void _delay_us(double us) { (void)us; }
static inline void _delay_ms(double ms) { (void)ms; }
#endif
X
# F_CPU must match the Makefile (24 MHz). Without it every translation unit
# that derives a timing constant fails to compile, and this check silently
# reported failure for reasons unrelated to the code under test.
cat > "$TMP/avr/eeprom.h" <<'X'
#ifndef MOCK_EEPROM_H
#define MOCK_EEPROM_H
#define EEMEM   /* real builds place these in .eeprom */
#include <stddef.h>
#include <stdint.h>
uint8_t eeprom_read_byte(const uint8_t *p);
void eeprom_update_byte(uint8_t *p, uint8_t v);
void eeprom_read_block(void *dst, const void *src, size_t n);
void eeprom_update_block(const void *src, void *dst, size_t n);
#endif
X
FLAGS="-std=gnu11 -Wall -Wextra -Wundef -Wshadow -DF_CPU=24000000UL -I$TMP -Iinclude"
fail=0
for f in src/*.c; do
  printf '  %-22s ' "$(basename "$f")"
  if cc $FLAGS -fsyntax-only "$f" 2>"$TMP/err"; then echo "ok"; else
    echo "FAIL"; cat "$TMP/err"; fail=1
  fi
done
[ "$fail" -eq 0 ] || exit 1
echo "OK - all translation units compile cleanly"
