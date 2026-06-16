#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

if ! command -v nsjail >/dev/null 2>&1; then
    echo "nsjail not found, skipping nsjail integration tests"
    exit 0
fi

source tests/support/test_helpers.sh

# Backup
SOL_BAK=$(mktemp); PJ_BAK=$(mktemp)
cp submissions/solution.cpp "$SOL_BAK"
cp problems/A+B/problem.json "$PJ_BAK"

PROBLEM_DIR="problems/A+B_nsjail_int"
rm -rf "$PROBLEM_DIR"
cp -a problems/A+B "$PROBLEM_DIR"
python3 -c "
import json
from pathlib import Path
p = Path('$PROBLEM_DIR/problem.json')
with p.open('r') as f: data = json.load(f)
data['sandbox_type'] = 'nsjail'
with p.open('w') as f: json.dump(data, f, indent=4); f.write('\n')
"

do_cleanup() {
    cp "$SOL_BAK" submissions/solution.cpp 2>/dev/null || true
    cp "$PJ_BAK" problems/A+B/problem.json 2>/dev/null || true
    rm -f "$SOL_BAK" "$PJ_BAK"
    rm -rf "$PROBLEM_DIR" /tmp/cppjudge_int_*
}

# Build once
rm -rf build
mkdir build
cd build
cmake .. >/dev/null 2>&1
make >/dev/null 2>&1
cd ..

run_all() {
    echo "=== nsjail AC ==="
    test_verdict "nsjail_ac" "Accepted" submissions/tests/ac.cpp "$PROBLEM_DIR" 1000 128 1 floating 5000

    echo "=== nsjail TLE ==="
    test_verdict "nsjail_tle" "Time Limit Exceeded" submissions/tests/tle.cpp "$PROBLEM_DIR" 100 128 1 floating 5000

    echo "=== nsjail OLE ==="
    test_verdict "nsjail_ole" "Output Limit Exceeded" submissions/tests/ole.cpp "$PROBLEM_DIR" 1000 128 1 floating 5000

    echo "=== nsjail RE ==="
    test_verdict "nsjail_re" "Runtime Error" submissions/tests/re.cpp "$PROBLEM_DIR" 1000 128 1 floating 5000

    echo "=== nsjail MLE ==="
    cp submissions/tests/mle.cpp submissions/solution.cpp
    rm -f build/judge_log.json
    "$CPPJUDGE_BIN" submissions/solution.cpp "$PROBLEM_DIR" 1000 128 1 floating 5000 >/dev/null 2>&1 || true
    if [ -f build/judge_log.json ]; then
        verdict=$(judge_log_field "final_verdict")
        if [ "$verdict" = "Memory Limit Exceeded" ] || [ "$verdict" = "Time Limit Exceeded" ]; then
            pass "nsjail_mle -> ${verdict} (MLE or TLE, see known nsjail MLE drift)"
        else
            fail "nsjail_mle -> expected MLE or TLE (nsjail RLIMIT_AS drift), got ${verdict}"
        fi
    else
        fail "nsjail_mle -> no judge_log.json"
    fi

    echo "=== nsjail CE ==="
    test_verdict "nsjail_ce" "Compile Error" submissions/tests/ce.cpp "$PROBLEM_DIR" 1000 128 1 floating 5000
}

rc=0
run_all || rc=$?
do_cleanup
if [ $rc -ne 0 ]; then exit $rc; fi
finish_tests
