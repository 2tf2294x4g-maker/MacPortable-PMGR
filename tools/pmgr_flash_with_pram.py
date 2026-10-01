#!/usr/bin/env python3
"""Flash a PMGR image and write known-good PRAM in one step (MEASUREMENTS 2.156, 2.165).

  pmgr_flash_with_pram.py <hex> <md5 prefix> <eeprom args file> [calib args file]

Every flash erases EEPROM (EESAVE clear), leaving PRAM blank - the ROM's own
initialisation then often fails to land. This writes a PRAM image straight back.

  - refuses a hex whose md5 does not start with the prefix (flash the file you meant)
  - chip erase + write + verify
  - writes the EEPROM image at offset 0 and reads back len + 132 bytes: the image must
    match and the bytes after it must be erased (for the two-slot layout: slot 1 blank)

The args file is whitespace-separated 0xNN bytes. For baselines from 2026-09-13-pram-save
on, make it with tools/pram_slot_image.py. ⛔ The PRAM image itself is kept LOCAL, not in
the repository. Port via PMGR_UPDI_PORT (default the Tigard's first interface).

⭐⭐ THE CALIBRATION RECORD (design/HYBRID-CALIBRATION.md §6, B2 of §7). The
optional 4th argument is a 16-byte record from tools/pmgr_calib_image.py, written
at EEPROM 464 and read back. ⛔ WITHOUT IT EVERY REFLASH SILENTLY REVERTS THE
BOARD TO UNCALIBRATED, because every flash erases EEPROM (EESAVE clear). Since
the gate landed (2.350) that is SAFE - the board refuses to fast-charge rather
than charging on a wrong scale - but it is still a loss, and a board that quietly
stops fast-charging after a routine reflash is a bug report waiting to happen.

⚠️ OMITTING IT IS A VALID AND TESTED MODE, not an oversight: B2's second half is
exactly "flash WITHOUT it, and confirm the gate engages"."""
import subprocess, os, sys, hashlib

if len(sys.argv) not in (4, 5): sys.exit(__doc__)
HEX, WANT, ARGS = sys.argv[1:4]
CALIB = sys.argv[4] if len(sys.argv) == 5 else None
CALIB_BASE, CALIB_LEN = 464, 16        # pmgr_calib.h - clear of PRAM (0-263) and the recorder (264-463)
PORT = os.environ.get('PMGR_UPDI_PORT', '/dev/tty.usbserial-TG1118a10')
env = dict(os.environ, PATH=os.path.expanduser('~/Library/Python/3.11/bin') + ':' + os.environ['PATH'])
base = ['-t', 'uart', '-u', PORT, '-d', 'avr128db64']
md5 = hashlib.md5(open(HEX, 'rb').read()).hexdigest()
if not md5.startswith(WANT): sys.exit(f'⛔ hex md5 {md5} != expected {WANT}')

# ⛔⛔ VALIDATE EVERY INPUT BEFORE TOUCHING THE CHIP (review 2026-09-30). The flash below ERASES the
# whole device, EEPROM included. This used to open ARGS and CALIB only AFTERWARDS, so a mistyped,
# missing or malformed calib.args was discovered with the board's existing record already gone.
# The record is checked with the ENCODER's own CRC and limits - one definition, not a second copy.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pmgr_calib_image as CI

PRAM_MAX = 264                         # two 132-byte slots; 264-463 is the recorder, 464 the record

def read_bytes(path, what):
    try:
        toks = open(path).read().split()
    except OSError as e:
        sys.exit(f'⛔ cannot read {what} "{path}": {e}\n   NOTHING HAS BEEN FLASHED.')
    try:
        vals = [int(t, 16) for t in toks]
    except ValueError:
        sys.exit(f'⛔ {what} "{path}" is not whitespace-separated hex bytes.\n   NOTHING HAS BEEN FLASHED.')
    if not toks or any(not 0 <= x <= 255 for x in vals):
        sys.exit(f'⛔ {what} "{path}" is empty or has a value outside 0x00-0xFF.\n   NOTHING HAS BEEN FLASHED.')
    return toks, vals

def record_errors(r):
    if len(r) != CALIB_LEN:
        return [f'{len(r)} bytes, expected {CALIB_LEN}']
    u16 = lambda i: r[i] | (r[i + 1] << 8)
    e = []
    if u16(0) != CI.MAGIC:                 e.append(f'magic 0x{u16(0):04X} != 0x{CI.MAGIC:04X} - not a calibration record')
    if r[2] != CI.VERSION:                 e.append(f'version {r[2]} != {CI.VERSION}')
    if CI.crc16(r[:14]) != u16(14):        e.append('CRC does not match - the file is corrupt or hand-edited')
    a = u16(4); b = u16(6) - (0x10000 if u16(6) & 0x8000 else 0)
    if not CI.A_MIN <= a <= CI.A_MAX:      e.append(f'slope {a/256:.2f} outside {CI.A_MIN/256:.0f}-{CI.A_MAX/256:.0f}')
    if abs(b) > CI.B_ABS_MAX:              e.append(f'intercept {b/256:.2f} beyond +/-{CI.B_ABS_MAX/256:.0f} counts')
    if r[8] > CI.RESID_MAX:                e.append(f'residual {r[8]/16:.2f} counts > {CI.RESID_MAX/16:.2f}')
    if u16(9) < CI.SPAN_MIN:               e.append(f'span {u16(9)/1000:.3f} V < {CI.SPAN_MIN/1000:.2f} V')
    return e

pram_toks = []
if ARGS != '-':
    pram_toks, pram_vals = read_bytes(ARGS, 'PRAM image')
    if len(pram_vals) > PRAM_MAX:
        sys.exit(f'⛔ PRAM image is {len(pram_vals)} bytes; the PRAM area is {PRAM_MAX}. It would overwrite '
                 f'the areas above it.\n   NOTHING HAS BEEN FLASHED.')
cal_toks = []
if CALIB:
    cal_toks, cal_vals = read_bytes(CALIB, 'calibration record')
    errs = record_errors(cal_vals)
    if errs:
        sys.exit(f'⛔ calibration record "{CALIB}" REFUSED - the firmware would reject it:\n   - '
                 + '\n   - '.join(errs) + '\n   NOTHING HAS BEEN FLASHED; the board still holds its old record.')
print('inputs checked: hex md5 ' + md5[:8]
      + ('' if ARGS == '-' else f', PRAM image {len(pram_toks)} B')
      + (f', calibration record valid (id {cal_vals[3]})' if CALIB else ', NO calibration record'))
def run(a, must_succeed=True):
    """⛔ CHECK THE EXIT STATUS. This returned only stdout+stderr, so a pymcuprog
    that FAILED but still printed parseable bytes was treated as success - the
    same shape as the empty-read bug fixed alongside it (review 2026-09-28,
    MEASUREMENTS 2.368). A nonzero return code is a failed operation whatever it
    printed."""
    r = subprocess.run(['pymcuprog'] + a, capture_output=True, text=True, env=env)
    out = r.stdout + r.stderr
    run.last_rc = r.returncode          # ⭐ always available to the caller
    if must_succeed and r.returncode != 0:
        sys.exit(f'⛔ pymcuprog {a[0]} FAILED, exit {r.returncode} - not a partial '
                 f'success to parse around:\n{out[-600:]}')
    return out
run.last_rc = 0
w = run(['write'] + base + ['-f', HEX, '--erase', '--verify'], must_succeed=False)
w_rc = run.last_rc
v = run(['verify'] + base + ['-f', HEX], must_succeed=False)
v_rc = run.last_rc
# ⛔ THE EXIT CODES ARE PART OF THE VERDICT, not just the text. These two are
# non-fatal only so their own diagnostics print; a nonzero exit still FAILS the
# flash, whatever the output happens to contain. Review 2026-09-28: deciding
# success from "OK" in the output is a verification satisfied by a STRING.
ok_flash = ('OK' in w and 'successful' in v and w_rc == 0 and v_rc == 0)
if w_rc or v_rc:
    print(f'⛔ pymcuprog exit codes: write {w_rc}, verify {v_rc} - FLASH FAILED')
print('flash md5', md5[:8], '| write:', 'OK' if 'OK' in w else w[-300:], '| verify:', 'matches' if 'successful' in v else v[-300:])
if not ok_flash: sys.exit(1)
# ⭐ '-' means NO PRAM IMAGE: leave PRAM erased and let the Mac initialise it.
# ⛔ Added 2026-09-28 rather than fabricating an all-zeros image. A zeros slot
# would be a VALID slot holding zeros, which the PMGR would load as real PRAM;
# erased EEPROM is no valid slot at all, which is the state this board has run
# on all session and boots from. The two are NOT the same thing.
if ARGS == '-':
    print('PRAM: none written (erased) - the Mac initialises it')
    vals, n = [], 0
else:
    vals = pram_toks; n = len(vals)          # validated above, before the erase
if n:
    run(['write'] + base + ['-m', 'eeprom', '-o', '0', '-l'] + vals)   # one list argument per byte
r = run(['read'] + base + ['-m', 'eeprom', '-o', '0', '-b', str(n + 132)])
b = []
for line in r.splitlines():
    if line.startswith('0x'): b += [int(t, 16) for t in line.split(':', 1)[1].split() if t != 'xx']
same = b[:n] == [int(x, 16) for x in vals]
if n:
    erased = all(x == 0xFF for x in b[n:n + 132])
    print(f'EEPROM image ({n} B) read back identical: {same} | next 132 bytes erased: {erased} ({len(b)} bytes read)')
    ok = same and erased
else:
    # ⛔ THE ERASED CHECK IS MEANINGLESS IN '-' MODE AND MUST NOT BE REPORTED AS
    # A FAILURE. The AVR starts running the instant the flash completes, and
    # pmgr_pram_service() saves a PRAM slot in the background (2.155-2.157), so
    # bytes 0.. are legitimately non-erased within milliseconds. Observed
    # 2026-09-28: slot 0 read back `04 00 00 ...` seconds after a chip erase.
    # ⭐ What matters here is that the firmware's own writes stay OFF the record:
    # PRAM occupies 0-263 and the record sits at 464.
    print(f'PRAM slots not checked (no image written); firmware may already have saved one - '
          f'first bytes {" ".join(f"{x:02X}" for x in b[:4])}')
    ok = True

# ---- the calibration record ------------------------------------------------
def ee_read(off, ln):
    """Read ln bytes, or DIE. ⛔ Returning a short list silently is how a
    verification passes on no data: `all(x == 0xFF for x in [])` is True, so an
    EMPTY read reported "erased" and the tool exited 0. Found by review
    2026-09-28 (MEASUREMENTS 2.362). A read that does not produce exactly ln
    bytes is a FAILED read, not an erased region."""
    out, txt = [], run(['read'] + base + ['-m', 'eeprom', '-o', str(off), '-b', str(ln)])
    for line in txt.splitlines():
        if line.startswith('0x'):
            out += [int(t, 16) for t in line.split(':', 1)[1].split() if t != 'xx']
    if len(out) < ln:
        sys.exit(f'⛔ EEPROM read at {off} returned {len(out)} of {ln} bytes - '
                 f'VERIFICATION IMPOSSIBLE, not "erased". pymcuprog said:\n{txt[-400:]}')
    return out[:ln]

if CALIB:
    cv = cal_toks                             # validated above, before the erase
    if len(cv) != CALIB_LEN:
        sys.exit(f'⛔ calibration image is {len(cv)} bytes, expected {CALIB_LEN}')
    run(['write'] + base + ['-m', 'eeprom', '-o', str(CALIB_BASE), '-l'] + cv)
    got = ee_read(CALIB_BASE, CALIB_LEN)
    csame = len(got) == CALIB_LEN and got == [int(x, 16) for x in cv]
    print(f'calibration record at {CALIB_BASE} read back identical: {csame}'
          + ('' if csame else f'\n  wrote {cv}\n  read  {[f"0x{x:02X}" for x in got]}'))
    ok = ok and csame
else:
    # ⛔ SAY SO LOUDLY. A silent omission is how a board ends up uncalibrated
    # without anyone noticing - and after 2.350 that means it will not fast-charge.
    got = ee_read(CALIB_BASE, CALIB_LEN)          # dies unless it got all 16
    blank = len(got) == CALIB_LEN and all(x == 0xFF for x in got)
    print(f'⚠️  NO calibration record written - EEPROM {CALIB_BASE} is '
          + ('erased, so THE BOARD IS UNCALIBRATED and the gate will refuse fast charge'
             if blank else f'NOT erased ({got[:4]}...) - unexpected after a chip erase'))
    ok = ok and blank

sys.exit(0 if ok else 1)
