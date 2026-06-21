#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT_DIR"

CPPJUDGE_BIN="$(printenv CPPJUDGE_BIN || true)"
if [ -z "$CPPJUDGE_BIN" ]; then
    CPPJUDGE_BIN="./build/cppjudge"
fi
TMP_DIR=$(mktemp -d -t cppjudge_cli.XXXXXX)
trap 'rm -rf "$TMP_DIR"' EXIT

PASS=0
FAIL=0

pass() { printf '[PASS] %s\n' "$1"; PASS=$((PASS + 1)); }
fail() { printf '[FAIL] %s\n' "$1"; FAIL=$((FAIL + 1)); }

run_expect_success() {
    local name="$1"
    shift
    if "$@" >"$TMP_DIR/$name.out" 2>&1; then
        pass "$name"
    else
        fail "$name"
        sed -n '1,80p' "$TMP_DIR/$name.out"
    fi
}

run_expect_failure_with() {
    local name="$1" expected="$2"
    shift 2
    set +e
    "$@" >"$TMP_DIR/$name.out" 2>&1
    local rc=$?
    set -e
    if [ "$rc" -eq 2 ] && grep -q -- "$expected" "$TMP_DIR/$name.out" &&
       grep -q -- "--help" "$TMP_DIR/$name.out"; then
        pass "$name"
    else
        fail "$name (rc=$rc)"
        sed -n '1,80p' "$TMP_DIR/$name.out"
    fi
}

check_log_field() {
    local name="$1" log="$2" field="$3" expected="$4"
    local actual
    actual=$(python3 - "$log" "$field" <<'PY'
import json
import sys
with open(sys.argv[1], encoding="utf-8") as handle:
    print(json.load(handle)[sys.argv[2]])
PY
)
    if [ "$actual" = "$expected" ]; then
        pass "$name"
    else
        fail "$name (expected $expected, got $actual)"
    fi
}

run_expect_success help "$CPPJUDGE_BIN" --help
grep -q "Usage" "$TMP_DIR/help.out" &&
grep -q "cppjudge judge" "$TMP_DIR/help.out" &&
grep -q -- "--problem" "$TMP_DIR/help.out" &&
grep -q -- "--submission" "$TMP_DIR/help.out" && pass "help_content" || fail "help_content"

run_expect_success judge_help "$CPPJUDGE_BIN" judge --help
grep -q "Usage: cppjudge judge --problem <problem_dir> --submission <source.cpp>" \
    "$TMP_DIR/judge_help.out" && pass "judge_help_content" || fail "judge_help_content"

run_expect_failure_with missing_problem "missing required --problem" \
    "$CPPJUDGE_BIN" judge --submission submissions/tests/ac.cpp
run_expect_failure_with missing_submission "missing required --submission" \
    "$CPPJUDGE_BIN" judge --problem problems/A+B

PROBLEM_DIR="$TMP_DIR/problem"
mkdir -p "$PROBLEM_DIR"
cp -R problems/A+B/input problems/A+B/output "$PROBLEM_DIR/"
cat >"$PROBLEM_DIR/problem.json" <<'JSON'
{
  "title": "CLI config test",
  "time_limit_ms": 777,
  "memory_limit_mb": 96,
  "output_limit_mb": 2,
  "compile_time_limit_ms": 6000,
  "compare_mode": "exact",
  "sandbox_type": "builtin"
}
JSON

export CPPJUDGE_BUILD_DIR="$TMP_DIR/new_cli_build"
run_expect_success new_cli "$CPPJUDGE_BIN" judge \
    --problem "$PROBLEM_DIR" \
    --submission submissions/tests/ac.cpp
check_log_field new_cli_verdict "$CPPJUDGE_BUILD_DIR/judge_log.json" final_verdict "Accepted"
check_log_field config_time_limit "$CPPJUDGE_BUILD_DIR/judge_log.json" time_limit_ms "777"
check_log_field config_memory_limit "$CPPJUDGE_BUILD_DIR/judge_log.json" memory_limit_mb "96"
check_log_field config_output_limit "$CPPJUDGE_BUILD_DIR/judge_log.json" output_limit_mb "2"
check_log_field config_compare_mode "$CPPJUDGE_BUILD_DIR/judge_log.json" compare_mode "exact"

export CPPJUDGE_BUILD_DIR="$TMP_DIR/shorthand_build"
run_expect_success shorthand "$CPPJUDGE_BIN" judge submissions/tests/ac.cpp --problem "$PROBLEM_DIR"
check_log_field shorthand_verdict "$CPPJUDGE_BUILD_DIR/judge_log.json" final_verdict "Accepted"

export CPPJUDGE_BUILD_DIR="$TMP_DIR/override_build"
run_expect_success overrides "$CPPJUDGE_BIN" judge \
    --problem "$PROBLEM_DIR" \
    --submission submissions/tests/ac.cpp \
    --time-limit-ms 2000 \
    --memory-limit-mb 128 \
    --output-limit-mb 4 \
    --compare-mode floating \
    --compile-time-limit-ms 10000
check_log_field override_time_limit "$CPPJUDGE_BUILD_DIR/judge_log.json" time_limit_ms "2000"
check_log_field override_memory_limit "$CPPJUDGE_BUILD_DIR/judge_log.json" memory_limit_mb "128"
check_log_field override_output_limit "$CPPJUDGE_BUILD_DIR/judge_log.json" output_limit_mb "4"
check_log_field override_compare_mode "$CPPJUDGE_BUILD_DIR/judge_log.json" compare_mode "floating"
check_log_field override_compile_limit "$CPPJUDGE_BUILD_DIR/judge_log.json" compile_time_limit_ms "10000"

export CPPJUDGE_BUILD_DIR="$TMP_DIR/legacy_build"
run_expect_success legacy_cli "$CPPJUDGE_BIN" submissions/tests/ac.cpp \
    "$PROBLEM_DIR" 1000 128 1 exact 5000
check_log_field legacy_verdict "$CPPJUDGE_BUILD_DIR/judge_log.json" final_verdict "Accepted"

grep -q "./build/cppjudge judge --problem problems/A+B --submission submissions/solution.cpp" \
    README.md && pass "readme_primary_command" || fail "readme_primary_command"

echo
echo "CLI tests: $((PASS + FAIL)) total, $PASS passed, $FAIL failed"
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
