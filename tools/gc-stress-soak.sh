#!/bin/sh
# Runs the test suite under --gc-stress over and over, and captures
# everything about any run that stops making progress.
#
#   tools/gc-stress-soak.sh [output-directory] [hours]
#
# Defaults to ./gc-soak and 8 hours. Safe to leave unattended: it bounds
# what it writes, kills what it starts, and keeps going after a capture
# so that one night produces many of them rather than one.
#
# For each hang it records which test was running, the interpreter's own
# report of which threads the collector was waiting for, and a sample of
# every thread's stack. That is the set of things needed to tell one
# deadlock from another.

set -u

OUT=${1:-./gc-soak}
HOURS=${2:-8}
# How long one whole-suite run may take before it counts as hung. The
# suite is minutes under --gc-stress, so this is generous.
RUN_TIMEOUT=${RUN_TIMEOUT:-1200}
# How long a single test may sit with no progress before it counts as
# stuck. This is the signal that actually fires.
STALL_SECONDS=${STALL_SECONDS:-120}

HERE=$(cd "$(dirname "$0")/.." && pwd)
RED="$HERE/build/red"
TESTS="$HERE/tests"

if [ ! -x "$RED" ]; then
  echo "No interpreter at $RED. Build it first: cmake --build build" >&2
  exit 1
fi

mkdir -p "$OUT"
SUMMARY="$OUT/summary.txt"
DEADLINE=$(( $(date +%s) + HOURS * 3600 ))
run=0
hangs=0

echo "soak started $(date)" >> "$SUMMARY"
echo "interpreter: $($RED version)" >> "$SUMMARY"
echo "runs until: $(date -r $DEADLINE 2>/dev/null || echo "+${HOURS}h")" >> "$SUMMARY"
echo "" >> "$SUMMARY"

cleanup() {
  pkill -9 -f "$RED" 2>/dev/null
  exit 0
}
trap cleanup INT TERM

# The test currently being run by the suite, as a bare file name.
current_test() {
  ps -A -o args= 2>/dev/null \
    | grep -- "--gc-stress $TESTS/" \
    | sed 's|.*/||' \
    | head -1
}

while [ "$(date +%s)" -lt "$DEADLINE" ]; do
  run=$(( run + 1 ))
  started=$(date +%s)
  log="$OUT/run-$run.log"

  "$RED" test "$TESTS" --gc-stress > "$log" 2>&1 &
  suite=$!

  stalled_on=""
  previous=""
  same_for=0

  while kill -0 "$suite" 2>/dev/null; do
    now=$(date +%s)
    if [ $(( now - started )) -gt "$RUN_TIMEOUT" ]; then
      stalled_on="(whole run over ${RUN_TIMEOUT}s)"
      break
    fi

    here=$(current_test)
    if [ -n "$here" ] && [ "$here" = "$previous" ]; then
      same_for=$(( same_for + 2 ))
      if [ "$same_for" -ge "$STALL_SECONDS" ]; then
        stalled_on="$here"
        break
      fi
    else
      previous="$here"
      same_for=0
    fi
    sleep 2
  done

  if [ -z "$stalled_on" ]; then
    wait "$suite" 2>/dev/null
    status=$?
    tail -1 "$log" > "$OUT/.last" 2>/dev/null
    printf 'run %-4s %s  ok(%s)  %ss  %s\n' "$run" "$(date '+%H:%M:%S')" \
      "$status" "$(( $(date +%s) - started ))" "$(cat "$OUT/.last" 2>/dev/null)" \
      >> "$SUMMARY"
    # A run that finished tells us nothing new; its log is just noise.
    rm -f "$log"
    continue
  fi

  # Stuck. Take everything before killing anything.
  hangs=$(( hangs + 1 ))
  capture="$OUT/hang-$(date '+%Y%m%d-%H%M%S')"
  mkdir -p "$capture"
  echo "$stalled_on" > "$capture/test"

  child=$(pgrep -f -- "--gc-stress $TESTS/" | head -1)
  echo "suite=$suite child=${child:-none}" >> "$capture/test"

  if [ -n "${child:-}" ]; then
    # Every thread's stack. This is the half that says where each one is.
    sample "$child" 4 -f "$capture/sample.txt" > /dev/null 2>&1
    # And the same process again a few seconds later, so a slow run can
    # be told apart from a stopped one.
    sleep 5
    sample "$child" 4 -f "$capture/sample-2.txt" > /dev/null 2>&1
    ps -M "$child" > "$capture/threads.txt" 2>&1
  fi

  # The interpreter's own account of which threads the collector is
  # waiting for. The suite gives each test its own stderr file.
  scratch=$(ls -td /tmp/red-test-* 2>/dev/null | head -1)
  if [ -n "$scratch" ]; then
    cp "$scratch/err" "$capture/stderr.txt" 2>/dev/null
    cp "$scratch/out" "$capture/stdout.txt" 2>/dev/null
  fi
  cp "$log" "$capture/suite.log" 2>/dev/null

  pkill -9 -f "$RED" 2>/dev/null
  wait "$suite" 2>/dev/null
  rm -f "$log"

  printf 'run %-4s %s  HUNG on %s -> %s\n' "$run" "$(date '+%H:%M:%S')" \
    "$stalled_on" "$(basename "$capture")" >> "$SUMMARY"
  echo "hang $hangs captured in $capture"
done

echo "" >> "$SUMMARY"
echo "soak finished $(date): $run runs, $hangs hung" >> "$SUMMARY"
pkill -9 -f "$RED" 2>/dev/null
