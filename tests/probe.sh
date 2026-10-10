#!/bin/sh
# probe.sh FILE : run every line of FILE as its own one-statement script
# (after "v <- iota 5") under both apl.rex and the C build; show differences.
APL=${APL:-./apl}
n=0; d=0
while IFS= read -r line; do
  n=$((n+1))
  printf 'v <- iota 5\n%s\n' "$line" > /tmp/probe.apl
  a=$(rexx ref/apl.rex /tmp/probe.apl 2>&1)
  b=$($APL /tmp/probe.apl 2>&1)
  if [ "$a" != "$b" ]; then
    d=$((d+1)); echo "DIFF: $line"; echo "   rexx: $a" | head -3; echo "   c   : $b" | head -3
  fi
done < "$1"
echo "$n probes, $d differences"
