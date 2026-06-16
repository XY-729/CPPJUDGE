# Shared test helpers for cppjudge shell tests
# Source this file in integration/regression test scripts:
#   source "$(dirname "${BASH_SOURCE[0]}")/../support/test_helpers.sh"

set -euo pipefail

# Allow overriding the judge binary
CPPJUDGE_BIN="${CPPJUDGE_BIN:-./build/cppjudge}"

PASS=0
FAIL=0

fail() {
    printf '[FAIL] %s\n' "$1"
    FAIL=$((FAIL + 1))
}

pass() {
    printf '[PASS] %s\n' "$1"
    PASS=$((PASS + 1))
}

# Read a top-level field from build/judge_log.json
judge_log_field() {
    local field="$1"
    python3 -c "import json; print(json.load(open('build/judge_log.json'))['${field}'])" 2>/dev/null
}

# Delete old log, run judge, verify new log was created and is valid JSON
run_judge() {
    rm -f build/judge_log.json
    "$CPPJUDGE_BIN" "$@" >/dev/null 2>&1 || true
    if [ ! -f build/judge_log.json ]; then
        fail "judge did not produce build/judge_log.json"
        return 1
    fi
    if ! python3 -c "import json; json.load(open('build/judge_log.json'))" 2>/dev/null; then
        fail "build/judge_log.json is not valid JSON"
        return 1
    fi
    return 0
}

# Assert final_verdict equals expected value
assert_verdict() {
    local name="$1" expected="$2"
    local actual
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

# Run a verdict test case
test_verdict() {
    local name="$1" expected="$2" submission="$3"
    shift 3
    cp "$submission" submissions/solution.cpp
    run_judge submissions/solution.cpp "$@" || return 1
    assert_verdict "$name" "$expected"
}

# Print summary and exit with appropriate code
finish_tests() {
    echo ""
    echo "========================================"
    echo "Tests: $((PASS + FAIL)) total, $PASS passed, $FAIL failed"
    echo "========================================"
    if [ "$FAIL" -gt 0 ]; then
        exit 1
    fi
    echo "All tests passed."
}

