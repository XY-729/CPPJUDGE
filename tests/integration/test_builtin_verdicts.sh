#!/usr/bin/env bash
# Integration test: builtin sandbox verdict coverage
# Uses pre-built CPPJUDGE_BIN, never rebuilds, never touches tracked files.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"
source tests/support/test_helpers.sh

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
    cp submissions/tests/re.cpp "$SUBMISSION_COPY"
    run_judge "$SUBMISSION_COPY" problems/A+B 1000 128 1 floating 5000 || true
    if [ -f "$JUDGE_LOG" ]; then
        verdict=$(judge_log_field "final_verdict")
        sig=$(python3 -c "
import json
with open('$JUDGE_LOG') as f: data = json.load(f)
results = data.get('results') or []
print(results[0].get('signal', -1) if results else -1)
" 2>/dev/null || echo -1)
        if [ "$verdict" = "Runtime Error" ]; then
            pass "builtin_re -> Runtime Error (signal=${sig})"
        else
            fail "builtin_re -> expected Runtime Error, got ${verdict}"
        fi
    else
        fail "builtin_re -> no judge log"
    fi

    echo "=== builtin CE ==="
    test_verdict "builtin_ce" "Compile Error" submissions/tests/ce.cpp problems/A+B 1000 128 1 floating 5000

    echo "=== builtin SE (missing input) ==="
    tmp_prob=$(make_temp_problem)
    mkdir -p "$tmp_prob/output"
    test_verdict "builtin_se_missing_input" "System Error" submissions/tests/ac.cpp "$tmp_prob" 1000 128 1 floating 5000
    rm -rf "$tmp_prob"
}

rc=0; run_all || rc=$?
finish_tests
if [ $rc -ne 0 ]; then exit $rc; fi
