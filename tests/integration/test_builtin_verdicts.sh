#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

source tests/support/test_helpers.sh

# Backup
SOL_BAK=$(mktemp); PJ_BAK=$(mktemp)
cp submissions/solution.cpp "$SOL_BAK"
cp problems/A+B/problem.json "$PJ_BAK"

do_cleanup() {
    cp "$SOL_BAK" submissions/solution.cpp 2>/dev/null || true
    cp "$PJ_BAK" problems/A+B/problem.json 2>/dev/null || true
    rm -f "$SOL_BAK" "$PJ_BAK"
    rm -rf /tmp/cppjudge_int_*
}

# Build once
rm -rf build
mkdir build
cd build
cmake .. >/dev/null 2>&1
make >/dev/null 2>&1
cd ..

run_all() {
    echo "=== builtin AC ==="
    test_verdict "builtin_ac" "Accepted" submissions/tests/ac.cpp problems/A+B 1000 128 1 floating 5000

    echo "=== builtin WA ==="
    test_verdict "builtin_wa" "Wrong Answer" submissions/tests/wa.cpp problems/A+B 1000 128 1 floating 5000

    echo "=== builtin TLE ==="
    test_verdict "builtin_tle" "Time Limit Exceeded" submissions/tests/tle.cpp problems/A+B 100 128 1 floating 5000

    echo "=== builtin MLE ==="
    test_verdict "builtin_mle" "Memory Limit Exceeded" submissions/tests/mle.cpp problems/A+B 1000 128 1 floating 5000

    echo "=== builtin OLE ==="
    test_verdict "builtin_ole" "Output Limit Exceeded" submissions/tests/ole.cpp problems/A+B 1000 128 1 floating 5000

    echo "=== builtin RE ==="
    cp submissions/tests/re.cpp submissions/solution.cpp
    rm -f build/judge_log.json
    "$CPPJUDGE_BIN" submissions/solution.cpp problems/A+B 1000 128 1 floating 5000 >/dev/null 2>&1 || true
    if [ -f build/judge_log.json ]; then
        verdict=$(judge_log_field "final_verdict")
        sig=$(python3 -c "
import json
with open('build/judge_log.json') as f:
    data = json.load(f)
results = data.get('results') or []
print(results[0].get('signal', -1) if results else -1)
" 2>/dev/null || echo -1)
        if [ "$verdict" = "Runtime Error" ]; then
            pass "builtin_re -> Runtime Error (signal=${sig})"
        else
            fail "builtin_re -> expected Runtime Error, got ${verdict}"
        fi
    else
        fail "builtin_re -> no judge_log.json"
    fi

    echo "=== builtin CE ==="
    test_verdict "builtin_ce" "Compile Error" submissions/tests/ce.cpp problems/A+B 1000 128 1 floating 5000

    echo "=== builtin SE (missing input) ==="
    tmp_prob=$(mktemp -d -t cppjudge_problem.XXXXXX)
    mkdir -p "$tmp_prob/output"
    test_verdict "builtin_se_missing_input" "System Error" submissions/tests/ac.cpp "$tmp_prob" 1000 128 1 floating 5000
    rm -rf "$tmp_prob"
}

# Run and cleanup regardless of outcome
rc=0
run_all || rc=$?
do_cleanup
if [ $rc -ne 0 ]; then exit $rc; fi
finish_tests
