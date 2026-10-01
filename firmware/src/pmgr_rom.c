/* The $E800-$FFFF ROM view behind $E8.
 *
 * `.LAB_F200`'s read path uses a real 16-bit pointer, so a host diagnostic can
 * read the PMGR's own program ROM out of a running part. This supplies what it
 * sees. Writes in this range are ignored by pmgr_mem_write() and complete
 * normally, exactly as they do on real silicon.
 *
 * ===== TWO BUILDS ==========================================================
 *
 *   PUBLIC / OPEN  (default)   a SYNTHETIC image. No Apple bytes anywhere in
 *                              the artifact or the repository.
 *
 *   FAITHFUL       -DPMGR_FAITHFUL_ROM   the exact pmuv1.bin bytes, so the
 *                              board is indistinguishable from real silicon
 *                              under a host firmware dump.
 *
 * ⛔ THE REPOSITORY NEVER CONTAINS APPLE'S BYTES. The faithful build sources
 * them from a LOCAL copy at build time:
 *
 *     python3 tools_rom2h.py /path/to/pmuv1.bin > include/pmgr_rom_image.h
 *     make CFLAGS="$(CFLAGS) -DPMGR_FAITHFUL_ROM"
 *
 * The generator verifies size (6144) and CRC32 (01DAE148) and refuses to emit
 * anything else, so a wrong file fails loudly instead of baking silent garbage
 * into firmware. include/pmgr_rom_image.h is gitignored - it IS the image. */
#include "pmgr_rom.h"

#ifdef PMGR_FAITHFUL_ROM
#  include "pmgr_rom_image.h"
#endif

uint8_t pmgr_rom_read(uint16_t addr)
{
    if (addr < PMGR_ROM_BASE) return 0x00u;
    const uint16_t off = (uint16_t)(addr - PMGR_ROM_BASE);
    if (off >= PMGR_ROM_SIZE) return 0x00u;

#ifdef PMGR_FAITHFUL_ROM
    return pgm_read_byte(&PMGR_ROM[off]);
#else
    /* ---- SYNTHETIC VIEW -------------------------------------------------
     * Deliberately NOT zeros: an all-zero range is indistinguishable from an
     * unmapped one, so a host reading it could not tell "open build" from
     * "broken". This is identifiable on sight in a hex dump.
     *
     * The version bytes ARE reproduced at $E802/$E803. They are the version
     * this firmware reports through $EA, so leaving them out would make the two
     * paths disagree about the same fact. */
    switch (off) {
    case 0x0002u: return 0x02u;                 /* PMVers1, matches $EA */
    case 0x0003u: return 0xB5u;                 /* PMVers2, matches $EA */
    default: break;
    }
    {
        static const char banner[] = "OPEN PMGR REPLACEMENT - NOT APPLE ROM - ";
        return (uint8_t)banner[off % (sizeof banner - 1u)];
    }
#endif
}
