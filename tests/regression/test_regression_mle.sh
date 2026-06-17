#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

PASS=0; FAIL=0
fail() { printf '[FAIL] %s\n' "$1"; FAIL=$((FAIL+1)); }
pass() { printf '[PASS] %s\n' "$1"; PASS=$((PASS+1)); }

# ── Unique temp build dir ──────────────────────────────────
TEST_BUILD_DIR=$(mktemp -d -t cppjudge_mle_reg.XXXXXX)
export CPPJUDGE_BUILD_DIR="$TEST_BUILD_DIR"
JUDGE_LOG="$TEST_BUILD_DIR/judge_log.json"

cleanup() { rm -rf "$TEST_BUILD_DIR"; }
trap cleanup EXIT

judge_log_field() {
    python3 -c "import json; print(json.load(open('$JUDGE_LOG')).get('$1',''))" 2>/dev/null
}

run_judge() {
    rm -f "$JUDGE_LOG"
    "$CPPJUDGE_BIN" "$@" >/dev/null 2>&1 || true
    if [ ! -f "$JUDGE_LOG" ]; then
        fail "judge did not produce $JUDGE_LOG"
        return 1
    fi
    if ! python3 -c "import json; json.load(open('$JUDGE_LOG'))" 2>/dev/null; then
        fail "$JUDGE_LOG is not valid JSON"
        return 1
    fi
    return 0
}

# ── Check if cgroup delegation is available ────────────────
check_cgroup_delegated() {
    # Check if we're running inside a delegated service
    # by testing if we can create a child cgroup in our service root
    local cg_mount cg_path test_dir
    cg_mount=$(findmnt -n -o TARGET -t cgroup2 2>/dev/null || echo "/sys/fs/cgroup")
    cg_path=$(awk -F: '$2==""{print $3; exit}' /proc/self/cgroup 2>/dev/null || echo "")
    if [ -z "$cg_path" ]; then
        return 1
    fi
    test_dir="${cg_mount}${cg_path}/.cppjudge_mle_test_$$"
    if mkdir "$test_dir" 2>/dev/null; then
        rmdir "$test_dir" 2>/dev/null
        return 0
    fi
    return 1
}

# ── Copy submission to temp (never touch tracked files) ────
SUB_COPY=$(mktemp -t cppjudge_mle_sub.XXXXXX.cpp)
cleanup_sub() { rm -f "$SUB_COPY"; }

# ═══════════════════════════════════════════════════════════
# builtin MLE
# ═══════════════════════════════════════════════════════════
echo "=== builtin MLE ==="
cp submissions/tests/mle.cpp "$SUB_COPY"
run_judge "$SUB_COPY" problems/A+B 1000 128 1 floating 5000

verdict=$(judge_log_field "final_verdict")
echo "  builtin MLE verdict: $verdict"

if [ "$verdict" = "Memory Limit Exceeded" ]; then
    pass "builtin MLE -> Memory Limit Exceeded"
elif [ "$verdict" = "Runtime Error" ]; then
    fail "builtin MLE -> got Runtime Error (MLE misclassified as RE)"
else
    fail "builtin MLE -> expected Memory Limit Exceeded, got $verdict"
fi

# ═══════════════════════════════════════════════════════════
# nsjail MLE (if nsjail available)
# ═══════════════════════════════════════════════════════════
if command -v nsjail >/dev/null 2>&1; then
    echo "=== nsjail MLE ==="

    NS_PROBLEM=$(mktemp -d -t cppjudge_mle_nsprob.XXXXXX)
    cp -a problems/A+B/* "$NS_PROBLEM/"
    python3 -c "
import json
from pathlib import Path
p = Path('$NS_PROBLEM/problem.json')
with p.open('r') as f: data = json.load(f)
data['sandbox_type'] = 'nsjail'
with p.open('w') as f: json.dump(data, f, indent=4); f.write('\n')
"
    cleanup_nsprob() { rm -rf "$NS_PROBLEM"; }

    cp submissions/tests/mle.cpp "$SUB_COPY"
    run_judge "$SUB_COPY" "$NS_PROBLEM" 1000 128 1 floating 5000

    ns_verdict=$(judge_log_field "final_verdict")
    echo "  nsjail MLE verdict: $ns_verdict"

    # Stage 3B: nsjail requires cgroup delegation.
    # Without delegation, SE is the correct fail-closed behavior.
    if check_cgroup_delegated; then
        echo "  cgroup: delegated (expecting real MLE or known TLE drift)"
        if [ "$ns_verdict" = "Runtime Error" ]; then
            fail "nsjail MLE -> got Runtime Error (MLE misclassified as RE)"
        elif [ "$ns_verdict" = "Memory Limit Exceeded" ]; then
            pass "nsjail MLE -> Memory Limit Exceeded"
        elif [ "$ns_verdict" = "Time Limit Exceeded" ]; then
            pass "nsjail MLE -> Time Limit Exceeded (known nsjail MLE drift)"
        elif [ "$ns_verdict" = "System Error" ]; then
            fail "nsjail MLE -> System Error in delegated environment (unexpected)"
        else
            fail "nsjail MLE -> unexpected verdict: $ns_verdict"
        fi
    else
        echo "  cgroup: NOT delegated (expecting SE — correct fail-closed)"
        if [ "$ns_verdict" = "System Error" ]; then
            pass "nsjail MLE -> System Error (correct: no delegation, fail-closed)"
        else
            fail "nsjail MLE -> expected System Error without delegation, got $ns_verdict"
        fi
    fi

    cleanup_nsprob
else
    echo "=== nsjail MLE === SKIP (nsjail not available)"
fi

# ── Summary ────────────────────────────────────────────────
cleanup_sub

echo ""
echo "MLE Classification Tests: $((PASS + FAIL)) total, $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
echo "All MLE classification tests passed."
