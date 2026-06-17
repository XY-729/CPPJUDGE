#!/usr/bin/env bash
# Integration test: nsjail sandbox verdict coverage
# Uses pre-built CPPJUDGE_BIN, never rebuilds, never touches tracked files.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

if ! command -v nsjail >/dev/null 2>&1; then
    echo "nsjail not found, skipping nsjail integration tests"
    exit 77
fi

# Stage 3B: nsjail production mode requires delegated cgroup v2
source "$ROOT_DIR/tests/support/skip_unless_cgroup_delegated.sh"
skip_unless_cgroup_delegated

source tests/support/test_helpers.sh

PROBLEM_DIR=$(mktemp -d -t cppjudge_nsjail_problem.XXXXXX)
cp -a problems/A+B/* "$PROBLEM_DIR/"
python3 -c "
import json
from pathlib import Path
p = Path('$PROBLEM_DIR/problem.json')
with p.open('r') as f: data = json.load(f)
data['sandbox_type'] = 'nsjail'
with p.open('w') as f: json.dump(data, f, indent=4); f.write('\n')
"

cleanup_problem() { rm -rf "$PROBLEM_DIR"; }

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
    cp submissions/tests/mle.cpp "$SUBMISSION_COPY"
    run_judge "$SUBMISSION_COPY" "$PROBLEM_DIR" 1000 128 1 floating 5000 || true
    if [ -f "$JUDGE_LOG" ]; then
        verdict=$(judge_log_field "final_verdict")
        if [ "$verdict" = "Memory Limit Exceeded" ] || [ "$verdict" = "Time Limit Exceeded" ]; then
            pass "nsjail_mle -> ${verdict} (MLE or TLE, known nsjail MLE drift)"
        else
            fail "nsjail_mle -> expected MLE or TLE, got ${verdict}"
        fi
    else
        fail "nsjail_mle -> no judge log"
    fi

    echo "=== nsjail CE ==="
    test_verdict "nsjail_ce" "Compile Error" submissions/tests/ce.cpp "$PROBLEM_DIR" 1000 128 1 floating 5000
}

rc=0; run_all || rc=$?
cleanup_problem
finish_tests
if [ $rc -ne 0 ]; then exit $rc; fi
