/* Emits the C implementation's encoding of a record given on the command line,
 * so tools/pmgr_calib_image.py can be compared against it byte for byte.
 *
 * ⛔ WHY THIS EXISTS. The installer encodes the record in Python and the
 * firmware decodes it in C. Endianness, the Q8 rounding and the CRC are three
 * independent ways for the two to disagree, and ⛔ a disagreement would NOT be
 * caught by the CRC: the Python CRC would simply certify Python's own wrong
 * bytes, the firmware would accept them, and the board would run on a slope
 * nobody chose. */
#include "pmgr_calib.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 7) { fprintf(stderr, "usage: cross a_q8 b_q8 resid_q4 span_mv rail_mv id\n"); return 2; }
    pmgr_calib_t c = {0};
    c.magic = PMGR_CALIB_MAGIC; c.version = PMGR_CALIB_VERSION;
    c.a_q8     = (uint16_t)atoi(argv[1]);
    c.b_q8     = (int16_t) atoi(argv[2]);
    c.resid_q4 = (uint8_t) atoi(argv[3]);
    c.span_mv  = (uint16_t)atoi(argv[4]);
    c.rail_mv  = (uint16_t)atoi(argv[5]);
    c.pairing_id = (uint8_t)atoi(argv[6]);
    uint8_t b[PMGR_CALIB_SIZE];
    pmgr_calib_encode(&c, b);
    for (uint8_t i = 0; i < PMGR_CALIB_SIZE; i++) printf("%02x", b[i]);
    printf("\n");
    return 0;
}
