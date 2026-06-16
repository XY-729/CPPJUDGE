# Shared test helpers for cppjudge shell tests
# Source this file in integration/regression test scripts:
#   source "$(dirname "${BASH_SOURCE[0]}")/../support/test_helpers.sh"

set -euo pipefail

CPPJUDGE_BIN="${CPPJUDGE_BIN:-./build/cppjudge}"

PASS=0
FAIL=0

fail() { printf '[FAIL] %s\n' "$1"; FAIL=$((FAIL + 1)); }
pass() { printf '[PASS] %s\n' "$1"; PASS=$((PASS + 1)); }

# ── Isolated build directory ──────────────────────────────
# Each test script gets its own build dir to avoid cross-test pollution.
TEST_BUILD_DIR=$(mktemp -d -t cppjudge_build.XXXXXX)
export CPPJUDGE_BUILD_DIR="$TEST_BUILD_DIR"

cleanup_build_dir() { rm -rf "$TEST_BUILD_DIR"; }

# ── Temporary submission copy ─────────────────────────────
# Never modify the tracked submissions/solution.cpp directly.
SUBMISSION_COPY=$(mktemp -t cppjudge_submission.XXXXXX.cpp)

cleanup_submission() { rm -f "$SUBMISSION_COPY"; }

# ── judge_log.json path ──────────────────────────────────
JUDGE_LOG="$TEST_BUILD_DIR/judge_log.json"

judge_log_field() {
    local field="$1"
    python3 -c "import json; print(json.load(open('$JUDGE_LOG'))['${field}'])" 2>/dev/null
}

# Delete old log, run judge, verify new log was created and is valid JSON
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

# Assert final_verdict equals expected value
assert_verdict() {
    local name="$1" expected="$2" actual
    actual=$(judge_log_field "final_verdict")
    if [ "$actual" != "$expected" ]; then
        fail "${name} -> expected ${expected}, got ${actual}"
        return 1
    fi
    pass "${name} -> ${expected}"
    return 0
}

# Create a temporary problem directory with input/output
make_temp_problem() {
    local dir
    dir=$(mktemp -d -t cppjudge_problem.XXXXXX)
    mkdir -p "$dir/input" "$dir/output"
    echo "$dir"
}

# Run a verdict test case using temp submission copy (never touches tracked file)
test_verdict() {
    local name="$1" expected="$2" submission="$3"
    shift 3
    cp "$submission" "$SUBMISSION_COPY"
    run_judge "$SUBMISSION_COPY" "$@" || return 1
    assert_verdict "$name" "$expected"
}

# Print summary and exit with appropriate code
finish_tests() {
    cleanup_build_dir
    cleanup_submission
    echo ""
    echo "========================================"
    echo "Tests: $((PASS + FAIL)) total, $PASS passed, $FAIL failed"
    echo "========================================"
    if [ "$FAIL" -gt 0 ]; then
        exit 1
    fi
    echo "All tests passed."
}
