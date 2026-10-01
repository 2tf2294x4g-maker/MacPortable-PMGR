#!/usr/bin/env python3
"""Build the 16-byte calibration record for EEPROM 464 (design/HYBRID-CALIBRATION.md §5,
pre-registered MEASUREMENTS.md 2.349, measured 2.347).

    pmgr_calib_image.py --a 99.35 --b -23.26 --resid 1.19 --span 0.915 --rail 5.251 --id 1
    pmgr_calib_image.py --from-fit evidence/captures/calib-sweep-2026-09-28-pairs.csv ...

Prints whitespace-separated 0xNN bytes, the same form tools/pram_slot_image.py
produces, ready for pmgr_flash_with_pram.py.

⛔⛔ THIS ENCODER AND THE FIRMWARE'S MUST AGREE BYTE FOR BYTE. Endianness, the
Q8 rounding and the CRC are three separate ways to disagree silently, and a
record that decodes to a DIFFERENT slope would not be rejected by the CRC - it
would be accepted and wrong. `make -C firmware/test run_calib_cross` compares
this file's output against the C implementation's for the same inputs.

⛔ The bounds below are the firmware's, repeated deliberately: refusing here
gives a readable error at the desk instead of a silent gate engagement on the
bench. The FIRMWARE remains the authority - pmgr_calib.c checks them again."""
import argparse, sys

MAGIC, VERSION = 0xCA1B, 1
A_MIN, A_MAX   = 23040, 28160          # nominal 25600 +/- 10 %
B_ABS_MAX      = 25600                 # |b| <= 100 counts
RESID_MAX      = 32                    # <= 2.00 counts, in 1/16ths  (2.338)
SPAN_MIN       = 800                   # >= 0.80 V, in mV            (2.339)


def crc16(b):
    """CRC-16/CCITT reflected - byte-for-byte pmgr_pram.c's crc_update(), which
    pmgr_calib.c now shares. ⛔ This and the firmware carried CRC-16/ARC (0xA001)
    until 2026-09-28 under a comment claiming otherwise; corrected before any
    record was written to a board. `make -C firmware/test run_calib_cross` is
    what keeps the two honest."""
    c = 0xFFFF
    for x in b:
        d = (x ^ (c & 0xFF)) & 0xFF
        d = (d ^ ((d << 4) & 0xFF)) & 0xFF
        c = (((d << 8) | (c >> 8)) ^ (d >> 4) ^ ((d << 3) & 0xFFFF)) & 0xFFFF
    return c


def wr16(v):
    return [v & 0xFF, (v >> 8) & 0xFF]          # little-endian, as the AVR stores


def build(a, b, resid, span_v, rail_v, pairing):
    a_q8 = int(round(a * 256))
    b_q8 = int(round(b * 256))
    resid_q4 = int(round(resid * 16))
    span_mv, rail_mv = int(round(span_v * 1000)), int(round(rail_v * 1000))

    errs = []
    if not (A_MIN <= a_q8 <= A_MAX):
        errs.append(f'slope {a} -> a_q8 {a_q8} outside {A_MIN}..{A_MAX} (nominal 25600 +/-10 %)')
    if abs(b_q8) > B_ABS_MAX:
        errs.append(f'intercept {b} -> |b_q8| {abs(b_q8)} > {B_ABS_MAX} (|b| <= 100 counts)')
    if resid_q4 > RESID_MAX:
        errs.append(f'residual {resid} counts > 2.00 - THE SWEEP ITSELF DID NOT PASS (2.338)')
    if span_mv < SPAN_MIN:
        errs.append(f'span {span_v} V < 0.80 V - THE SWEEP ITSELF DID NOT PASS (2.339)')
    if not (0 <= pairing <= 255):
        errs.append(f'pairing id {pairing} out of range')
    if errs:
        sys.exit('⛔ refusing to build a record the firmware would reject:\n  ' + '\n  '.join(errs))

    o  = wr16(MAGIC) + [VERSION, pairing]
    o += wr16(a_q8) + wr16(b_q8 & 0xFFFF)
    o += [resid_q4] + wr16(span_mv) + wr16(rail_mv) + [0]
    assert len(o) == 14, len(o)
    return o + wr16(crc16(o))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--a',     type=float, required=True, help='slope, counts per volt')
    p.add_argument('--b',     type=float, required=True, help='intercept, counts')
    p.add_argument('--resid', type=float, required=True, help='max |residual|, counts')
    p.add_argument('--span',  type=float, required=True, help='sweep span, volts')
    p.add_argument('--rail',  type=float, required=True, help='⭐ rail DURING the sweep, volts')
    p.add_argument('--id',    type=int,   default=1, help='pairing id (§6: a hybrid swap invalidates '
                                                          'the record and the board CANNOT detect it)')
    p.add_argument('--hex', action='store_true', help='print as plain hex, for diffing')
    a = p.parse_args()
    rec = build(a.a, a.b, a.resid, a.span, a.rail, a.id)
    print(''.join(f'{x:02x}' for x in rec) if a.hex else ' '.join(f'0x{x:02X}' for x in rec))
