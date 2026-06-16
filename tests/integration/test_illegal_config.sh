#!/usr/bin/env bash
# Integration test: illegal/problematic config -> System Error / appropriate verdict
# Uses pre-built CPPJUDGE_BIN, never rebuilds, never touches tracked files.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"
source tests/support/test_helpers.sh

# Make a valid temp problem with input/output
VALID_PROBLEM=$(make_temp_problem)
for i in 1 2 3; do
    echo "$i" > "$VALID_PROBLEM/input/${i}.in"
    echo "$((i*2))" > "$VALID_PROBLEM/output/${i}.out"
done

run_all() {
    echo "=== zero time limit -> SE ==="
    test_verdict "cfg_zero_time" "System Error" submissions/tests/ac.cpp "$VALID_PROBLEM" 0 128 1 floating 5000

    echo "=== negative time limit -> SE ==="
    test_verdict "cfg_neg_time" "System Error" submissions/tests/ac.cpp "$VALID_PROBLEM" -1 128 1 floating 5000

    echo "=== zero memory limit -> SE ==="
    test_verdict "cfg_zero_mem" "System Error" submissions/tests/ac.cpp "$VALID_PROBLEM" 1000 0 1 floating 5000

    echo "=== negative memory limit -> SE ==="
    test_verdict "cfg_neg_mem" "System Error" submissions/tests/ac.cpp "$VALID_PROBLEM" 1000 -1 1 floating 5000

    echo "=== zero output limit -> SE ==="
    test_verdict "cfg_zero_out" "System Error" submissions/tests/ac.cpp "$VALID_PROBLEM" 1000 128 0 floating 5000

    echo "=== zero compile time limit -> SE ==="
    test_verdict "cfg_zero_compile" "System Error" submissions/tests/ac.cpp "$VALID_PROBLEM" 1000 128 1 floating 0

    echo "=== negative compile time limit -> SE ==="
    test_verdict "cfg_neg_compile" "System Error" submissions/tests/ac.cpp "$VALID_PROBLEM" 1000 128 1 floating -1

    echo "=== invalid compare mode -> SE ==="
    test_verdict "cfg_bad_compare" "System Error" submissions/tests/ac.cpp "$VALID_PROBLEM" 1000 128 1 invalid_mode 5000

    echo "=== invalid sandbox type -> SE ==="
    # Use problem.json with invalid sandbox_type
    tmp_p=$(make_temp_problem)
    cp "$VALID_PROBLEM/input/"*.in "$tmp_p/input/" 2>/dev/null || true
    cp "$VALID_PROBLEM/output/"*.out "$tmp_p/output/" 2>/dev/null || true
    python3 -c "
import json
from pathlib import Path
Path('$tmp_p/problem.json').write_text('{\"sandbox_type\": \"invalid_sandbox\"}\n')
"
    test_verdict "cfg_bad_sandbox" "System Error" submissions/tests/ac.cpp "$tmp_p" 1000 128 1 floating 5000
    rm -rf "$tmp_p"

    echo "=== negative float_abs_eps -> SE ==="
    tmp_p2=$(make_temp_problem)
    cp "$VALID_PROBLEM/input/"*.in "$tmp_p2/input/" 2>/dev/null || true
    cp "$VALID_PROBLEM/output/"*.out "$tmp_p2/output/" 2>/dev/null || true
    python3 -c "
import json
from pathlib import Path
Path('$tmp_p2/problem.json').write_text('{\"float_abs_eps\": -1.0}\n')
"
    test_verdict "cfg_neg_eps" "System Error" submissions/tests/ac.cpp "$tmp_p2" 1000 128 1 floating 5000
    rm -rf "$tmp_p2"

    echo "=== empty input -> SE ==="
    tmp_empty=$(make_temp_problem)
    mkdir -p "$tmp_empty/output"
    test_verdict "cfg_empty_input" "System Error" submissions/tests/ac.cpp "$tmp_empty" 1000 128 1 floating 5000
    rm -rf "$tmp_empty"

    echo "=== missing output -> SE ==="
    tmp_missing_out=$(make_temp_problem)
    echo "1" > "$tmp_missing_out/input/1.in"
    mkdir -p "$tmp_missing_out/output"
    # input exists but corresponding output is missing
    test_verdict "cfg_missing_output" "System Error" submissions/tests/ac.cpp "$tmp_missing_out" 1000 128 1 floating 5000
    rm -rf "$tmp_missing_out"
}

rc=0; run_all || rc=$?
rm -rf "$VALID_PROBLEM"
finish_tests
if [ $rc -ne 0 ]; then exit $rc; fi
