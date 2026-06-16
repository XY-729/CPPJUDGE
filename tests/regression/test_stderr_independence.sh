#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

PASS=0; FAIL=0
fail() { printf '[FAIL] %s\n' "$1"; FAIL=$((FAIL+1)); }
pass() { printf '[PASS] %s\n' "$1"; PASS=$((PASS+1)); }

# ── Unique temp build dir ──────────────────────────────────
TEST_BUILD_DIR=$(mktemp -d -t cppjudge_stderr_indep.XXXXXX)
export CPPJUDGE_BUILD_DIR="$TEST_BUILD_DIR"
JUDGE_LOG="$TEST_BUILD_DIR/judge_log.json"

cleanup() { rm -rf "$TEST_BUILD_DIR"; }
trap cleanup EXIT

judge_log_field() {
    python3 -c "import json; print(json.load(open('$JUDGE_LOG')).get('$1',''))" 2>/dev/null
}

run_judge() {
    # Always start with a fresh log
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

# ── Create problem ─────────────────────────────────────────
PROBLEM_DIR=$(mktemp -d -t cppjudge_stderr_prob.XXXXXX)
mkdir -p "$PROBLEM_DIR/input" "$PROBLEM_DIR/output"
cp problems/A+B/input/1.in "$PROBLEM_DIR/input/1.in"
cp problems/A+B/output/1.out "$PROBLEM_DIR/output/1.out"
cp problems/A+B/problem.json "$PROBLEM_DIR/problem.json"

cleanup_problem() { rm -rf "$PROBLEM_DIR"; }

# ── Create fixtures ────────────────────────────────────────
FIXTURE_DIR=$(mktemp -d -t cppjudge_stderr_fix.XXXXXX)
cleanup_fixtures() { rm -rf "$FIXTURE_DIR"; }

# Case A: correct answer (exit 0) + scary stderr
cat > "$FIXTURE_DIR/case_a.cpp" << 'CPPEOF'
#include <iostream>
int main() {
    int a, b;
    if (!(std::cin >> a >> b)) return 1;
    std::cerr << "No such file or directory" << std::endl;
    std::cerr << "Permission denied" << std::endl;
    std::cerr << "nsjail failed" << std::endl;
    std::cerr << "System Error" << std::endl;
    std::cerr << "Runtime Error" << std::endl;
    std::cout << (a + b) << std::endl;
    return 0;
}
CPPEOF

# Case B: same stderr + exit 7
cat > "$FIXTURE_DIR/case_b.cpp" << 'CPPEOF'
#include <iostream>
int main() {
    int a, b;
    if (!(std::cin >> a >> b)) return 1;
    std::cerr << "No such file or directory" << std::endl;
    std::cerr << "Permission denied" << std::endl;
    std::cerr << "nsjail failed" << std::endl;
    std::cerr << "System Error" << std::endl;
    std::cerr << "Runtime Error" << std::endl;
    std::cout << (a + b) << std::endl;
    return 7;
}
CPPEOF

# ── Test Case A ────────────────────────────────────────────
echo "=== Case A: correct answer + scary stderr === "

# Run with fresh build dir
run_judge "$FIXTURE_DIR/case_a.cpp" "$PROBLEM_DIR" 1000 128 1 floating 5000

verdict_a=$(judge_log_field "final_verdict")
system_error_a=$(python3 -c "
import json
with open('$JUDGE_LOG') as f: data = json.load(f)
results = data.get('results') or []
r = results[0] if results else {}
se = r.get('system_error', False)
print('true' if se else 'false')
" 2>/dev/null || echo "parse_error")

echo "  Case A: verdict=$verdict_a system_error=$system_error_a"

if [ "$verdict_a" = "Accepted" ]; then
    pass "Case A: Accepted (scary stderr did NOT cause false SE)"
else
    fail "Case A: expected Accepted, got $verdict_a (stderr poisoning)"
fi

# ── Test Case B ────────────────────────────────────────────
echo "=== Case B: same stderr + exit 7 === "

# Delete judge log to prove no cross-run contamination
rm -f "$JUDGE_LOG"

run_judge "$FIXTURE_DIR/case_b.cpp" "$PROBLEM_DIR" 1000 128 1 floating 5000

verdict_b=$(judge_log_field "final_verdict")
system_error_b=$(python3 -c "
import json
with open('$JUDGE_LOG') as f: data = json.load(f)
results = data.get('results') or []
r = results[0] if results else {}
se = r.get('system_error', False)
print('true' if se else 'false')
" 2>/dev/null || echo "parse_error")

exit_code_b=$(python3 -c "
import json
with open('$JUDGE_LOG') as f: data = json.load(f)
results = data.get('results') or []
r = results[0] if results else {}
print(r.get('exit_code', -1))
" 2>/dev/null || echo -1)

echo "  Case B: verdict=$verdict_b system_error=$system_error_b exit_code=$exit_code_b"

if [ "$verdict_b" = "Runtime Error" ]; then
    pass "Case B: Runtime Error (not SE despite scary stderr)"
else
    fail "Case B: expected Runtime Error, got $verdict_b"
fi

# ── Summary ────────────────────────────────────────────────
cleanup_fixtures
cleanup_problem

echo ""
echo "Stderr Independence Tests: $((PASS + FAIL)) total, $PASS passed, $FAIL failed"
if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
echo "All stderr independence tests passed."
