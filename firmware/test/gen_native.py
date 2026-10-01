#!/usr/bin/env python3
"""Generate a natively-compilable pmgr_handlers.c.

⭐ TWO substitutions, and only two:

  1. pmgr_pins.h -> pins_native.h, which rewrites ASSERT_LOW/DEASSERT_HIGH.
  2. Direct writes to OUTSET/OUTCLR/DIRSET/DIRCLR become read-modify-write on
     OUT/DIR.

⛔ (2) is NOT cosmetic and rewriting only the macros is NOT enough: the HICHG
state machine writes `HICHG_PORT.OUTSET = HICHG_bm;` DIRECTLY (pmgr_handlers.c
:2203), bypassing the macro entirely. On hardware writing OUTSET sets bits in
OUT; in a plain-struct mock it just stores a byte in a different field, so the
firmware asserts HICHG and the test never sees it. This is the same class of
problem adcstub.h documents for write-1-to-CLEAR on INTFLAGS - the one register
semantic a struct cannot express.

⚠️ Everything else is the real code, compiled unchanged.
"""
import re, sys
src = open(sys.argv[1]).read()
src = src.replace('#include "pmgr_pins.h"', '#include "pins_native.h"')
src = re.sub(r'\.OUTSET\s*=\s*([^;]+);', r'.OUT |= (uint8_t)(\1);', src)
src = re.sub(r'\.OUTCLR\s*=\s*([^;]+);', r'.OUT &= (uint8_t)~(\1);', src)
src = re.sub(r'\.DIRSET\s*=\s*([^;]+);', r'.DIR |= (uint8_t)(\1);', src)
src = re.sub(r'\.DIRCLR\s*=\s*([^;]+);', r'.DIR &= (uint8_t)~(\1);', src)
open(sys.argv[2], 'w').write(src)
