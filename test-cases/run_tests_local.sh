#!/usr/bin/env bash
set -u

MYSH="../src/mysh"

if [[ ! -x "$MYSH" ]]; then
  echo "Error: $MYSH not found or not executable. Build first with:"
  echo "  cd ../src && make mysh"
  exit 1
fi

pass=0
fail=0

echo "Running tests from $(pwd)"
echo

normalize_sequence() {
  # lowercase + collapse whitespace to single newlines (order preserved)
  tr '[:upper:]' '[:lower:]' < "$1" | tr -s '[:space:]' '\n' | sed '/^$/d'
}

normalize_bag() {
  # lowercase + tokenize + sort (order ignored)
  tr '[:upper:]' '[:lower:]' < "$1" | tr -s '[:space:]' '\n' | sed '/^$/d' | sort
}

run_one() {
  local input="$1"
  local base="${input%.txt}"
  local expected="${base}_result.txt"
  local output="${base}_out.txt"

  if [[ ! -f "$expected" ]]; then
    echo "Test: $base"
    echo "  SKIP (missing $expected)"
    echo
    return
  fi

  echo "Test: $base"
  "$MYSH" < "$input" > "$output"

  # MT tests are nondeterministic; compare token multiset instead of sequence.
  if [[ "$base" == T_MT* ]]; then
    if diff -q <(normalize_bag "$output") <(normalize_bag "$expected") >/dev/null; then
      echo "  PASS (MT order-insensitive)"
      ((pass++))
      rm -f "$output"
    else
      echo "  FAIL (MT order-insensitive)"
      ((fail++))
    fi
  else
    if diff -q <(normalize_sequence "$output") <(normalize_sequence "$expected") >/dev/null; then
      echo "  PASS"
      ((pass++))
      rm -f "$output"
    else
      echo "  FAIL"
      ((fail++))
    fi
  fi

  echo
}

if [[ $# -gt 0 ]]; then
  # Run only the named tests (base names, with or without .txt)
  for t in "$@"; do
    t="${t%.txt}"
    if [[ -f "${t}.txt" ]]; then
      run_one "${t}.txt"
    else
      echo "Test: $t"
      echo "  SKIP (missing ${t}.txt)"
      echo
    fi
  done
else
  for input in *.txt; do
    [[ "$input" == *_result.txt || "$input" == *_out.txt ]] && continue
    run_one "$input"
  done
fi

echo "-------------------------------------------"
echo "Results:"
echo "Passed: $pass"
echo "Failed: $fail"

exit $(( fail > 0 ))
