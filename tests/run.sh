#!/bin/sh
# Differential test: run each examples/*.apl with the C interpreter and
# compare to the reference output captured from apl.rex (ref/*.out).
# tests/known_diffs.txt lists intentional deviations.
APL=${APL:-./apl}
pass=0; fail=0
for f in examples/*.apl; do
  n=$(basename "$f" .apl)
  exp=ref/$n.out; [ -f ref/$n.c.out ] && exp=ref/$n.c.out
  [ -f "$exp" ] || continue
  got=$($APL "$f" 2>&1)
  if [ "$got" = "$(cat $exp)" ]; then pass=$((pass+1)); echo "ok   $n"
  else fail=$((fail+1)); echo "FAIL $n"; printf '%s\n' "$got" | diff - "$exp" | head -8; fi
done
for f in tests/scen/s*.apl; do
  n=$(basename "$f" .apl)
  [ -f tests/scen/$n.out ] || continue
  got=$($APL "$f" 2>&1)
  if [ "$got" = "$(cat tests/scen/$n.out)" ]; then pass=$((pass+1)); echo "ok   $n"
  else fail=$((fail+1)); echo "FAIL $n"; printf '%s\n' "$got" | diff - tests/scen/$n.out | head -8; fi
done
rm -f nest_ws.apl s11tmp.apl
echo "passed $pass, failed $fail"
[ $fail -eq 0 ]
