#!/bin/bash
# usage: loops.sh PHASE LOOPS SECONDS -> LOOPS parallel loops of RJ1.13 for SECONDS seconds; every failing run is printed with its assertion
phase=$1; loops=$2; secs=$3
deadline=$((SECONDS + secs))
for l in $(seq 1 "$loops"); do
  (
    runs=0; fails=0
    while [ $SECONDS -lt $deadline ]; do
      out=$(ANTS_TEST_FILTER=RJ1.13 build/tests/test_server/test_rejoin 2>&1) || true
      runs=$((runs + 1))
      if ! grep -q "Failed:           0" <<< "$out"; then
        fails=$((fails + 1))
        echo "FAIL phase=$phase loop=$l run=$runs :: $(grep -o 'Assertion failed.*' <<< "$out" | head -2 | cut -c1-210 | tr '\n' ' ')"
      fi
    done
    echo "LOOPDONE phase=$phase loop=$l runs=$runs fails=$fails"
  ) &
done
wait
