/* $E800-$FFFF ROM view for the $E8 read path. See src/pmgr_rom.c. */
#ifndef PMGR_ROM_H
#define PMGR_ROM_H

#include <stdint.h>
#ifdef PMGR_FAITHFUL_ROM
#  include <avr/pgmspace.h>
#endif

#ifndef PMGR_ROM_BASE
#  define PMGR_ROM_BASE 0xE800u
#endif
#ifndef PMGR_ROM_SIZE
#  define PMGR_ROM_SIZE 6144u
#endif

/* Returns the byte the host would read at `addr`. Outside the ROM range,
 * returns 0. Writes here are ignored by pmgr_mem_write() and still complete
 * normally - do not turn that into an error. */
uint8_t pmgr_rom_read(uint16_t addr);

#endif
