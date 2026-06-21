#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

# ── Defaults ───────────────────────────────────────────────
BUILD_DIR="${CPPJUDGE_TEST_BUILD_DIR:-$ROOT_DIR/build-stage2}"
JOBS="${CPPJUDGE_TEST_JOBS:-2}"
REPEAT="${CPPJUDGE_TEST_REPEAT:-1}"
PROFILE="${1:-portable}"

# ── Usage ──────────────────────────────────────────────────
usage() {
    cat << EOF
Usage: scripts/run_all_tests.sh [profile]

Profiles:
  quick      Fast development check (unit + portable integration)
  portable   All portable tests (no nsjail)
  full       All registered tests
  nsjail     nsjail-labelled tests only
  security   Security tests (must-block + known-gap)

Environment:
  CPPJUDGE_TEST_BUILD_DIR   Build directory (default: build-stage2)
  CPPJUDGE_TEST_JOBS        Parallel jobs for build and test (default: 2)
  CPPJUDGE_TEST_REPEAT      Test repeat count (default: 1)
EOF
}

# ── Validation ─────────────────────────────────────────────
VALID_PROFILES="quick portable full nsjail security"
if ! echo "$VALID_PROFILES" | grep -qw "$PROFILE"; then
    echo "ERROR: invalid profile '$PROFILE'"
    echo "Valid: $VALID_PROFILES"
    usage
    exit 1
fi

if ! [[ "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
    echo "ERROR: CPPJUDGE_TEST_JOBS must be a positive integer, got '$JOBS'"
    exit 1
fi

if ! [[ "$REPEAT" =~ ^[1-9][0-9]*$ ]]; then
    echo "ERROR: CPPJUDGE_TEST_REPEAT must be a positive integer, got '$REPEAT'"
    exit 1
fi

# ── Pre-flight: check for nsjail ──────────────────────────
NSJAIL_AVAILABLE=false
if command -v nsjail >/dev/null 2>&1; then
    NSJAIL_AVAILABLE=true
fi

if [[ "$PROFILE" == "nsjail" || "$PROFILE" == "security" ]]; then
    if ! $NSJAIL_AVAILABLE; then
        echo "ERROR: profile '$PROFILE' requires nsjail, but nsjail is not in PATH"
        exit 1
    fi
fi

# ── Build ──────────────────────────────────────────────────
echo "========================================"
echo "CPPJUDGE Unified Test Runner"
echo "========================================"
echo "ROOT_DIR:    $ROOT_DIR"
echo "BUILD_DIR:   $BUILD_DIR"
echo "PROFILE:     $PROFILE"
echo "JOBS:        $JOBS"
echo "REPEAT:      $REPEAT"
echo "nsjail:      $($NSJAIL_AVAILABLE && echo 'yes' || echo 'no')"
echo "========================================"

echo ""
echo "=== Configuring and building ==="
cmake -S "$ROOT_DIR" -B "$BUILD_DIR"
cmake --build "$BUILD_DIR" -j"$JOBS"

CPPJUDGE_BIN="$BUILD_DIR/cppjudge"
if [[ ! -x "$CPPJUDGE_BIN" ]]; then
    echo "ERROR: cppjudge binary not found at $CPPJUDGE_BIN"
    exit 1
fi
echo "cppjudge: $CPPJUDGE_BIN"
echo ""

# ── Build CTest label arguments ────────────────────────────
declare -a CTEST_LABEL_ARGS=()

case "$PROFILE" in
    quick)
        CTEST_LABEL_ARGS=(-L 'unit|integration' -LE 'regression|security|nsjail')
        ;;
    portable)
        CTEST_LABEL_ARGS=(-L portable -LE nsjail)
        ;;
    full)
        CTEST_LABEL_ARGS=()
        ;;
    nsjail)
        CTEST_LABEL_ARGS=(-L nsjail)
        ;;
    security)
        CTEST_LABEL_ARGS=(-L security)
        ;;
esac

# ── Count matching tests ───────────────────────────────────
echo "=== Counting matching tests ==="
set +e
COUNT_OUTPUT=$(ctest --test-dir "$BUILD_DIR" -N "${CTEST_LABEL_ARGS[@]}" 2>&1)
set -e
MATCH_COUNT=$(echo "$COUNT_OUTPUT" | grep -oP 'Total Tests:\s*\K\d+' || echo "0")

if [[ -z "$MATCH_COUNT" || "$MATCH_COUNT" -eq 0 ]]; then
    echo "ERROR: no tests match profile '$PROFILE'"
    echo "$COUNT_OUTPUT"
    exit 1
fi
echo "Registered matching tests: $MATCH_COUNT"
echo ""

# ── Security profile: verify required labels present ──────
if [[ "$PROFILE" == "security" ]]; then
    set +e
    MUST_OUT=$(ctest --test-dir "$BUILD_DIR" -N -L security-must-block 2>&1)
    GAP_OUT=$(ctest --test-dir "$BUILD_DIR" -N -L security-known-gap 2>&1)
    set -e
    MUST_COUNT=$(echo "$MUST_OUT" | grep -oP 'Total Tests:\s*\K\d+' || echo "0")
    GAP_COUNT=$(echo "$GAP_OUT" | grep -oP 'Total Tests:\s*\K\d+' || echo "0")
    if [[ "$MUST_COUNT" -eq 0 || "$GAP_COUNT" -eq 0 ]]; then
        echo "ERROR: security profile requires both security-must-block and security-known-gap tests"
        echo "  security-must-block: $MUST_COUNT"
        echo "  security-known-gap:  $GAP_COUNT"
        exit 1
    fi
    echo "  security-must-block: $MUST_COUNT"
    echo "  security-known-gap:  $GAP_COUNT"
    echo ""
fi

# ── Warn if full profile but nsjail unavailable ────────────
if [[ "$PROFILE" == "full" ]] && ! $NSJAIL_AVAILABLE; then
    echo "WARNING: nsjail not available — nsjail-labelled tests will be skipped"
    echo ""
fi

# ── Guard: track repo test data ────────────────────────────
echo "=== Checking repo test data integrity ==="
GUARD_PATHS=(problems submissions/tests)
SOLUTION_HASH_BEFORE=""
if [[ -f submissions/solution.cpp ]]; then
    SOLUTION_HASH_BEFORE="$(sha256sum submissions/solution.cpp | awk '{print $1}')"
fi

if git diff --quiet -- "${GUARD_PATHS[@]}"; then
    echo "  problem data and submission fixtures unchanged before run"
else
    echo "ERROR: tracked problem data or submission fixtures already differ before test run" >&2
    git diff -- "${GUARD_PATHS[@]}"
    exit 1
fi

if git diff --cached --quiet -- "${GUARD_PATHS[@]}"; then
    :
else
    echo "ERROR: staged problem data or submission fixtures already differ before test run" >&2
    git diff --cached -- "${GUARD_PATHS[@]}"
    exit 1
fi
echo ""

# ── Run tests ──────────────────────────────────────────────
OVERALL_RC=0
ROUND_COMPLETED=0
TOTAL_PASSED=0
TOTAL_SKIPPED=0
CTEST_LOG=$(mktemp /tmp/cppjudge_ctest.XXXXXX)
trap 'rm -f "$CTEST_LOG"' EXIT

for round in $(seq 1 "$REPEAT"); do
    if [[ "$REPEAT" -gt 1 ]]; then
        echo "=== Round $round / $REPEAT ==="
    fi

    set +e
    ctest \
        --test-dir "$BUILD_DIR" \
        -j"$JOBS" \
        --output-on-failure \
        "${CTEST_LABEL_ARGS[@]}" 2>&1 | tee "$CTEST_LOG"
    RC=$?
    set -e

    set +e
    ROUND_PASSED=$(grep -cE "^[0-9]+/[0-9]+ Test.*Passed" "$CTEST_LOG" 2>/dev/null || true)
    ROUND_SKIPPED=$(grep -cE "^[0-9]+/[0-9]+ Test.*Skipped" "$CTEST_LOG" 2>/dev/null || true)
    set -e
    ROUND_PASSED=${ROUND_PASSED:-0}
    ROUND_SKIPPED=${ROUND_SKIPPED:-0}
    TOTAL_PASSED=$((TOTAL_PASSED + ROUND_PASSED))
    TOTAL_SKIPPED=$((TOTAL_SKIPPED + ROUND_SKIPPED))

    ROUND_COMPLETED=$round

    if [[ $RC -ne 0 ]]; then
        echo ""
        echo "=== Round $round FAILED ==="
        OVERALL_RC=1
        break
    fi

    if [[ "$REPEAT" -gt 1 ]]; then
        echo "  Round $round: PASS"
    fi
done

# ── Post-run: verify test data unchanged ───────────────────
echo ""
echo "=== Post-run repo check ==="
if git diff --exit-code -- "${GUARD_PATHS[@]}"; then
    echo "  problem data and submission fixtures unchanged after run"
else
    echo "ERROR: tracked problem data or submission fixtures were modified during test run"
    OVERALL_RC=1
fi

if [[ -n "$SOLUTION_HASH_BEFORE" && -f submissions/solution.cpp ]]; then
    SOLUTION_HASH_AFTER="$(sha256sum submissions/solution.cpp | awk '{print $1}')"
    if [[ "$SOLUTION_HASH_AFTER" == "$SOLUTION_HASH_BEFORE" ]]; then
        echo "  submissions/solution.cpp unchanged during run"
    else
        echo "ERROR: submissions/solution.cpp was modified during test run"
        OVERALL_RC=1
    fi
fi

# ── Summary ────────────────────────────────────────────────
echo ""
echo "========================================"
echo "CPPJUDGE Test Summary"
echo "Profile:                $PROFILE"
echo "Build directory:        $BUILD_DIR"
echo "Registered matching tests: $MATCH_COUNT"
echo "Rounds completed:       $ROUND_COMPLETED / $REPEAT"

	echo "Tests executed:         $TOTAL_PASSED"
	echo "Tests skipped:          $TOTAL_SKIPPED"

	if [[ $OVERALL_RC -ne 0 ]]; then
	    echo "Result: FAIL"
	    LAST_LOG="$BUILD_DIR/Testing/Temporary/LastTest.log"
	    if [[ -f "$LAST_LOG" ]]; then
	        echo "LastTest.log: $LAST_LOG"
	    fi
	elif [[ $TOTAL_PASSED -eq 0 && $TOTAL_SKIPPED -gt 0 ]]; then
	    echo "Result: NOT_VERIFIED"
	    echo "NOT_VERIFIED: all tests were skipped — environment may not satisfy prerequisites"
	    OVERALL_RC=2
	else
	    echo "Result: PASS"
	fi
	echo "========================================"

	exit $OVERALL_RC
