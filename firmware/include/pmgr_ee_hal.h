/* EEPROM primitives for NON-AVR builds only (native tests, hostcheck).
 * On the AVR these are inline in pmgr_pram.c; natively test/eesim.c provides a
 * simulated EEPROM with a busy period and injectable interruptions. */
#ifndef PMGR_EE_HAL_H
#define PMGR_EE_HAL_H
#include <stdint.h>
#include <stdbool.h>
bool    ee_busy(void);
uint8_t ee_get(uint16_t a);
void    ee_start_write(uint16_t a, uint8_t v);
void    ee_end_write(void);
#endif
