#!/usr/bin/env bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC_DIR="$ROOT_DIR/src"
MYSH="$SRC_DIR/mysh"

FRAME_SIZE=18
VAR_SIZE=10
DO_BUILD=0
TEST_TIMEOUT=10
AUTO_BUILD=1

while [[ $# -gt 0 ]]; do
  case "$1" in
    --build)
      DO_BUILD=1
      shift
      ;;
    --framesize)
      FRAME_SIZE="$2"
      shift 2
      ;;
    --varmemsize)
      VAR_SIZE="$2"
      shift 2
      ;;
    --timeout)
      TEST_TIMEOUT="$2"
      shift 2
      ;;
    --no-autobuild)
      AUTO_BUILD=0
      shift
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: ./run_all_tests.sh [--build] [--framesize N] [--varmemsize N] [--timeout SEC] [--no-autobuild]"
      exit 2
      ;;
  esac
done

build_mysh() {
  local frame="$1"
  local var="$2"
  make -C "$SRC_DIR" clean >/dev/null || return 1
  make -C "$SRC_DIR" mysh framesize="$frame" varmemsize="$var" >/dev/null || return 1
}

if [[ "$DO_BUILD" -eq 1 && "$AUTO_BUILD" -eq 0 ]]; then
  echo "Building mysh with framesize=$FRAME_SIZE varmemsize=$VAR_SIZE"
  build_mysh "$FRAME_SIZE" "$VAR_SIZE" || exit 1
fi

if [[ ! -x "$MYSH" ]]; then
  echo "Error: $MYSH not found or not executable."
  echo "Run: make -C ../src mysh framesize=18 varmemsize=10"
  exit 1
fi

cd "$SCRIPT_DIR" || exit 1

pass=0
fail=0
skipped=0
cur_frame=""
cur_var=""

echo "Running test cases in $SCRIPT_DIR"
echo

for input in tc*.txt; do
  if [[ ! -f "$input" ]]; then
    continue
  fi

  if [[ "$input" == *_result.txt || "$input" == *_out.txt ]]; then
    continue
  fi

  base="${input%.txt}"
  expected="${base}_result.txt"
  output="${base}_out.txt"

  echo "Test: $base"

  if [[ ! -f "$expected" ]]; then
    echo "  SKIP (missing $expected)"
    ((skipped++))
    echo
    continue
  fi

  expected_header="$(head -n 1 "$expected")"
  test_frame="$(echo "$expected_header" | sed -n 's/^Frame Store Size = \([0-9][0-9]*\); Variable Store Size = \([0-9][0-9]*\)$/\1/p')"
  test_var="$(echo "$expected_header" | sed -n 's/^Frame Store Size = \([0-9][0-9]*\); Variable Store Size = \([0-9][0-9]*\)$/\2/p')"

  if [[ -z "$test_frame" || -z "$test_var" ]]; then
    test_frame="$FRAME_SIZE"
    test_var="$VAR_SIZE"
  fi

  if [[ "$AUTO_BUILD" -eq 1 ]]; then
    if [[ "$test_frame" != "$cur_frame" || "$test_var" != "$cur_var" || ! -x "$MYSH" ]]; then
      echo "  Building mysh (framesize=$test_frame varmemsize=$test_var)"
      if ! build_mysh "$test_frame" "$test_var"; then
        echo "  FAIL (build failed)"
        ((fail++))
        echo
        continue
      fi
      cur_frame="$test_frame"
      cur_var="$test_var"
    fi
  fi

  : > "$output"
  if ! timeout "${TEST_TIMEOUT}s" "$MYSH" < "$input" > "$output"; then
    rc=$?
    if [[ $rc -eq 124 ]]; then
      echo "  FAIL (timed out after ${TEST_TIMEOUT}s)"
    else
      echo "  FAIL (shell exited with code $rc)"
    fi
    ((fail++))
    echo
    continue
  fi

  if diff -q "$output" "$expected" >/dev/null; then
    echo "  PASS"
    ((pass++))
    rm -f "$output"
  else
    echo "  FAIL"
    ((fail++))
    echo "  Diff (first 40 lines):"
    diff -u "$expected" "$output" | sed -n '1,40p'
  fi

  echo
done

echo "-------------------------------------------"
echo "Results:"
echo "Passed : $pass"
echo "Failed : $fail"
echo "Skipped: $skipped"

exit $(( fail > 0 ))
