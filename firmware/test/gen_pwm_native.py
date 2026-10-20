#!/usr/bin/env python3
"""Generate pwm_native.c from ../src/pmgr_pwm.c for test_pwm (MEASUREMENTS 2.431).

A plain-struct mock cannot model the AVR-DB CCL erratum: on affected silicon, writes to a LUT's configuration
registers are IGNORED while CCL.CTRLA is enabled. So every LUT2 configuration write is rewritten into a call to
ccl_lut2_write(), which drops it when the CCL is on - the one register semantic a struct cannot express, as with
adcstub_clear_resrdy(). Everything else is the real code."""
import re, sys
src = open(sys.argv[1]).read()
out = re.sub(r'CCL\.(LUT2CTRLA|LUT2CTRLB|LUT2CTRLC|TRUTH2)\s*=\s*(.*?);',
             lambda m: f'ccl_lut2_write(&CCL.{m.group(1)}, (uint8_t)({m.group(2)}));', src, flags=re.S)
n = len(re.findall(r'ccl_lut2_write\(', out))
assert n >= 5, f'expected >= 5 LUT2 writes rewritten, got {n} - the generator no longer matches pmgr_pwm.c'
sys.stdout.write('#include "pwmstub.h"\n' + out)
