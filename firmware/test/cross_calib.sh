#!/bin/sh
# Compare tools/pmgr_calib_image.py against the FIRMWARE's own encoder.
#
# ⛔ A disagreement here would NOT be caught by the CRC - Python would certify
# its own wrong bytes and the firmware would accept them, running on a slope
# nobody chose. The cases below stress the three ways they can diverge:
# little-endian 16-bit fields, Q8 rounding, and NEGATIVE b in two's complement.
set -e
CROSS=./cross_calib
PY=../../tools/pmgr_calib_image.py
fails=0

# a       b        resid  span   rail    id      <- engineering units
# a_q8    b_q8     rq4    mv     mv      id      <- the same, pre-converted
check() {
  py=$(python3 $PY --a "$1" --b "$2" --resid "$3" --span "$4" --rail "$5" --id "$6" --hex)
  c=$($CROSS "$7" "$8" "$9" "${10}" "${11}" "$6")
  if [ "$py" = "$c" ]; then
    echo "  PASS  a=$1 b=$2  $py"
  else
    echo "  FAIL  a=$1 b=$2"
    echo "        py $py"
    echo "        C  $c"
    fails=$((fails+1))
  fi
}

echo "calibration encoder cross-check - Python installer vs firmware C"
# 2.347's measured record
check 99.35  -23.26 1.19 0.915 5.251 1   25434 -5955 19 915 5251
# NEGATIVE b, larger magnitude - two's complement over the wire
check 99.00  -99.50 2.00 0.900 5.200 7   25344 -25472 32 900 5200
# POSITIVE b - the other sign
check 101.00  40.00 0.50 1.200 5.300 42  25856  10240  8 1200 5300
# the nominal line, as test_charging provisions it
check 100.00   0.00 0.00 0.900 5.250 1   25600      0  0 900 5250
# boundary: exactly the accepted residual and span
check  99.35  -23.26 2.00 0.800 5.251 1  25434 -5955 32 800 5251

if [ $fails -eq 0 ]; then echo "  0 failure(s)"; else echo "  ⛔ $fails disagreement(s)"; fi
exit $fails
