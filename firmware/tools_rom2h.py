#!/usr/bin/env python3
"""Generate pmgr_rom_image.h from a local pmuv1.bin. NEVER commit the output.

    python3 tools_rom2h.py /path/to/pmuv1.bin > include/pmgr_rom_image.h

Verifies size and CRC32 before emitting, so a wrong or corrupt file fails loudly
rather than baking silent garbage into the firmware.
"""
import sys, zlib
d = open(sys.argv[1], 'rb').read()
if len(d) != 6144:
    sys.exit(f'ERROR: {len(d)} bytes, expected 6144')
crc = zlib.crc32(d) & 0xFFFFFFFF
if crc != 0x01DAE148:
    sys.exit(f'ERROR: CRC32 {crc:08X}, expected 01DAE148')
print('/* GENERATED - do not commit. Apple firmware image, local build only. */')
print('#ifndef PMGR_ROM_IMAGE_H\n#define PMGR_ROM_IMAGE_H')
print('#include <avr/pgmspace.h>\n#include <stdint.h>')
print('#define PMGR_ROM_BASE 0xE800u')
print('#define PMGR_ROM_SIZE 6144u')
print('static const uint8_t PMGR_ROM[6144] PROGMEM = {')
for i in range(0, len(d), 16):
    print('  ' + ' '.join(f'0x{b:02X},' for b in d[i:i+16]))
print('};\n#endif')
