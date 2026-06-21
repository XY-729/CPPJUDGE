#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT_DIR"

CPPJUDGE_BIN="$(printenv CPPJUDGE_BIN || true)"
if [ -z "$CPPJUDGE_BIN" ]; then
    CPPJUDGE_BIN="./build/cppjudge"
fi

TMP_DIR=$(mktemp -d -t cppjudge_version_doctor.XXXXXX)
trap 'rm -rf "$TMP_DIR"' EXIT

PASS=0
FAIL=0

pass() { printf '[PASS] %s\n' "$1"; PASS=$((PASS + 1)); }
fail() { printf '[FAIL] %s\n' "$1"; FAIL=$((FAIL + 1)); }

if "$CPPJUDGE_BIN" --version >"$TMP_DIR/version.out" 2>&1; then
    pass "version_exit_0"
else
    fail "version_exit_0"
fi

grep -qi "cppjudge" "$TMP_DIR/version.out" && pass "version_mentions_cppjudge" || fail "version_mentions_cppjudge"
grep -qi "git:" "$TMP_DIR/version.out" && pass "version_mentions_git" || fail "version_mentions_git"

set +e
"$CPPJUDGE_BIN" doctor >"$TMP_DIR/doctor.out" 2>&1
doctor_rc=$?
set -e

if [ "$doctor_rc" -eq 0 ] || [ "$doctor_rc" -eq 2 ]; then
    pass "doctor_exit_allowed_$doctor_rc"
else
    fail "doctor_exit_allowed_$doctor_rc"
fi

grep -qi "cppjudge version" "$TMP_DIR/doctor.out" && pass "doctor_version" || fail "doctor_version"
grep -qi "git commit" "$TMP_DIR/doctor.out" && pass "doctor_git_commit" || fail "doctor_git_commit"
grep -qi "platform" "$TMP_DIR/doctor.out" && pass "doctor_platform" || fail "doctor_platform"
grep -qi "current working directory" "$TMP_DIR/doctor.out" && pass "doctor_cwd" || fail "doctor_cwd"
grep -qi "nsjail" "$TMP_DIR/doctor.out" && pass "doctor_nsjail" || fail "doctor_nsjail"
grep -qi "cgroup v2" "$TMP_DIR/doctor.out" && pass "doctor_cgroup_v2" || fail "doctor_cgroup_v2"
grep -qi "current cgroup path" "$TMP_DIR/doctor.out" && pass "doctor_current_cgroup" || fail "doctor_current_cgroup"
grep -qi "memory controller" "$TMP_DIR/doctor.out" && pass "doctor_memory_controller" || fail "doctor_memory_controller"
grep -qi "pids controller" "$TMP_DIR/doctor.out" && pass "doctor_pids_controller" || fail "doctor_pids_controller"
grep -qi "seccomp policy" "$TMP_DIR/doctor.out" && pass "doctor_seccomp_policy" || fail "doctor_seccomp_policy"
grep -Eq "READY|NOT_READY|NOT_VERIFIED" "$TMP_DIR/doctor.out" && pass "doctor_readiness" || fail "doctor_readiness"

echo
echo "Version/doctor tests: $((PASS + FAIL)) total, $PASS passed, $FAIL failed"
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
