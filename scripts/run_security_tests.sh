#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

rm -rf build
mkdir build
cd build
cmake ..
make
cd ..

fail() {
    printf '[FAIL] %s\n' "$1"
    exit 1
}

latest_field() {
    local field="$1"
    python3 - "$field" <<'PY'
import json
import sys

with open("build/judge_log.json", "r", encoding="utf-8") as f:
    data = json.load(f)

field = sys.argv[1]
print(data.get(field, ""))
PY
}

latest_user_error_file() {
    python3 - <<'PY'
import json

with open("build/judge_log.json", "r", encoding="utf-8") as f:
    data = json.load(f)

results = data.get("results") or []
if results:
    print(results[0].get("user_error_file", ""))
PY
}

check_common_log() {
    local name="$1"

    [[ -f build/judge_log.json ]] || fail "${name} -> build/judge_log.json not found"

    local run_id
    local run_dir
    run_id="$(latest_field run_id)"
    run_dir="$(latest_field run_dir)"

    [[ -n "$run_id" ]] || fail "${name} -> missing run_id"
    [[ -n "$run_dir" ]] || fail "${name} -> missing run_dir"
    [[ -d "$run_dir" ]] || fail "${name} -> run_dir does not exist: ${run_dir}"
}

run_security_case() {
    local name="$1"
    local source_file="$2"
    shift 2
    local allowed_verdicts=("$@")

    local output_file="/tmp/cppjudge_security_${name}.log"
    set +e
    timeout 15s ./build/cppjudge "$source_file" problems/A+B 1000 128 1 floating 5000 >"$output_file" 2>&1
    local status=$?
    set -e

    if [[ "$status" -eq 124 ]]; then
        fail "${name} -> cppjudge timed out"
    fi

    if [[ "$status" -ne 0 ]]; then
        fail "${name} -> cppjudge exited with status ${status}"
    fi

    check_common_log "$name"

    local verdict
    verdict="$(latest_field final_verdict)"
    for allowed in "${allowed_verdicts[@]}"; do
        if [[ "$verdict" == "$allowed" ]]; then
            printf '[PASS] %-18s -> %s\n' "$name" "$verdict"
            return
        fi
    done

    fail "${name} -> unexpected verdict: ${verdict}"
}

run_open_many_files_test() {
    run_security_case \
        open_many_files \
        submissions/tests/security/open_many_files.cpp \
        "Runtime Error" \
        "System Error"
}

run_fork_many_test() {
    run_security_case \
        fork_many \
        submissions/tests/security/fork_many.cpp \
        "Time Limit Exceeded" \
        "Runtime Error" \
        "System Error"

    local run_dir
    run_dir="$(latest_field run_dir)"
    sleep 1

    if command -v pgrep >/dev/null 2>&1; then
        if pgrep -u "$(id -u)" -f "${run_dir}/solution" >/dev/null 2>&1; then
            fail "fork_many -> leftover child process detected for ${run_dir}/solution"
        fi
    fi
}

run_core_dump_test() {
    local before_core_files
    before_core_files="$(find . -maxdepth 1 -type f -name 'core*' -print | sort)"

    run_security_case \
        core_dump \
        submissions/tests/security/core_dump.cpp \
        "Runtime Error"

    local after_core_files
    after_core_files="$(find . -maxdepth 1 -type f -name 'core*' -print | sort)"
    if [[ "$before_core_files" != "$after_core_files" ]]; then
        fail "core_dump -> new core file appeared in project root"
    fi

    local run_dir
    run_dir="$(latest_field run_dir)"
    if find "$run_dir" -type f -name 'core*' -print -quit | grep -q .; then
        fail "core_dump -> core file appeared under ${run_dir}"
    fi
}

run_stderr_output_test() {
    run_security_case \
        stderr_output \
        submissions/tests/security/stderr_output.cpp \
        "Accepted"

    local error_file
    error_file="$(latest_user_error_file)"
    [[ -n "$error_file" ]] || fail "stderr_output -> missing user_error_file"
    [[ -f "$error_file" ]] || fail "stderr_output -> stderr file does not exist: ${error_file}"

    if ! grep -Fq "CPPJUDGE_SECURITY_STDERR_MARKER" "$error_file"; then
        fail "stderr_output -> stderr marker not found in ${error_file}"
    fi
}

run_open_many_files_test
run_fork_many_test
run_core_dump_test
run_stderr_output_test

echo "All security tests passed."
