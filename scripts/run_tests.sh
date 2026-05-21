#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

mkdir -p build
cmake -S . -B build >/dev/null
cmake --build build >/dev/null

tmp_solution="$(mktemp)"
tmp_problem_config="$(mktemp)"
had_solution=0
had_problem_config=0

if [[ -f submissions/solution.cpp ]]; then
    cp submissions/solution.cpp "$tmp_solution"
    had_solution=1
fi

if [[ -f problems/A+B/problem.json ]]; then
    cp problems/A+B/problem.json "$tmp_problem_config"
    had_problem_config=1
fi

restore_problem_config() {
    if [[ "$had_problem_config" -eq 1 ]]; then
        cp "$tmp_problem_config" problems/A+B/problem.json
    else
        rm -f problems/A+B/problem.json
    fi
}

cleanup() {
    if [[ "$had_solution" -eq 1 ]]; then
        cp "$tmp_solution" submissions/solution.cpp
    else
        rm -f submissions/solution.cpp
    fi

    restore_problem_config
    rm -f "$tmp_solution" "$tmp_problem_config"
}
trap cleanup EXIT

fail() {
    printf '[FAIL] %s\n' "$1"
    exit 1
}

set_problem_json_field() {
    local field="$1"
    local json_value="$2"
    python3 - "$field" "$json_value" <<'PY'
import json
import sys
from pathlib import Path

path = Path("problems/A+B/problem.json")
field = sys.argv[1]
json_value = sys.argv[2]
with path.open("r", encoding="utf-8") as f:
    data = json.load(f)
data[field] = json.loads(json_value)
with path.open("w", encoding="utf-8") as f:
    json.dump(data, f, indent=4)
    f.write("\n")
PY
}

set_problem_sandbox_type() {
    local sandbox_type="$1"
    set_problem_json_field sandbox_type "\"${sandbox_type}\""
}

latest_final_verdict() {
    python3 - <<'PY'
import json
with open("build/judge_log.json", "r", encoding="utf-8") as f:
    print(json.load(f).get("final_verdict", ""))
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

check_latest_run_metadata() {
    local name="$1"
    python3 - "$name" <<'PY'
import json
import os
import sys

name = sys.argv[1]
try:
    with open("build/judge_log.json", "r", encoding="utf-8") as f:
        data = json.load(f)
except Exception as exc:
    print(f'[FAIL] {name} -> failed to read build/judge_log.json: {exc}')
    sys.exit(1)

required_top_fields = [
    "run_id",
    "run_dir",
    "sandbox_type",
    "executable_file",
    "compile_error_file",
    "user_output_dir",
]

for field in required_top_fields:
    if not data.get(field):
        print(f'[FAIL] {name} -> missing {field} in build/judge_log.json')
        sys.exit(1)

if not os.path.isdir(data["run_dir"]):
    print(f'[FAIL] {name} -> run_dir does not exist: {data["run_dir"]}')
    sys.exit(1)

if not os.path.isdir(data["user_output_dir"]):
    print(f'[FAIL] {name} -> user_output_dir does not exist: {data["user_output_dir"]}')
    sys.exit(1)

for index, result in enumerate(data.get("results") or []):
    for field in ("user_error_file", "run_result", "time_ms", "memory_mb"):
        if field not in result:
            print(f'[FAIL] {name} -> results[{index}] missing {field}')
            sys.exit(1)
PY
}

expect_latest_verdict() {
    local name="$1"
    local expected="$2"
    local actual
    actual="$(latest_final_verdict)"

    if [[ "$actual" != "$expected" ]]; then
        fail "${name} -> expected \"${expected}\", got \"${actual}\""
    fi

    check_latest_run_metadata "$name"
}

run_verdict_case() {
    local name="$1"
    local expected="$2"

    restore_problem_config
    cp "submissions/tests/${name}.cpp" submissions/solution.cpp
    ./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 floating 5000 >/tmp/cppjudge_${name}.log 2>&1 || true

    expect_latest_verdict "$name" "$expected"
    printf '[PASS] %-26s -> %s\n' "$name" "$expected"
}

run_invalid_sandbox_type_case() {
    local name="invalid_sandbox_type"

    restore_problem_config
    set_problem_sandbox_type invalid_sandbox
    cp submissions/tests/ac.cpp submissions/solution.cpp
    ./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 floating 5000 >/tmp/cppjudge_${name}.log 2>&1 || true

    expect_latest_verdict "$name" "System Error"
    printf '[PASS] %-26s -> System Error\n' "$name"
    restore_problem_config
}

run_invalid_time_limit_type_case() {
    local name="invalid_time_limit_type"

    restore_problem_config
    set_problem_json_field time_limit_ms "\"abc\""
    cp submissions/tests/ac.cpp submissions/solution.cpp
    ./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 floating 5000 >/tmp/cppjudge_${name}.log 2>&1 || true

    expect_latest_verdict "$name" "System Error"
    printf '[PASS] %-26s -> System Error\n' "$name"
    restore_problem_config
}

run_negative_float_abs_eps_case() {
    local name="negative_float_abs_eps"

    restore_problem_config
    set_problem_json_field float_abs_eps -1
    cp submissions/tests/ac.cpp submissions/solution.cpp
    ./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 floating 5000 >/tmp/cppjudge_${name}.log 2>&1 || true

    expect_latest_verdict "$name" "System Error"
    printf '[PASS] %-26s -> System Error\n' "$name"
    restore_problem_config
}

run_invalid_compare_mode_case() {
    local name="invalid_compare_mode"

    restore_problem_config
    set_problem_json_field compare_mode "\"wrong_mode\""
    cp submissions/tests/ac.cpp submissions/solution.cpp
    ./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 floating 5000 >/tmp/cppjudge_${name}.log 2>&1 || true

    expect_latest_verdict "$name" "System Error"
    printf '[PASS] %-26s -> System Error\n' "$name"
    restore_problem_config
}

run_missing_input_dir_case() {
    local name="missing_input_dir"
    local problem_dir="build/test_problem_errors/${name}"

    restore_problem_config
    rm -rf "$problem_dir"
    mkdir -p "${problem_dir}/output"
    cp submissions/tests/ac.cpp submissions/solution.cpp
    ./build/cppjudge submissions/solution.cpp "$problem_dir" 1000 128 1 floating 5000 >/tmp/cppjudge_${name}.log 2>&1 || true

    expect_latest_verdict "$name" "System Error"
    printf '[PASS] %-26s -> System Error\n' "$name"
}

run_missing_output_dir_case() {
    local name="missing_output_dir"
    local problem_dir="build/test_problem_errors/${name}"

    restore_problem_config
    rm -rf "$problem_dir"
    mkdir -p "${problem_dir}/input"
    cp problems/A+B/input/1.in "${problem_dir}/input/1.in"
    cp submissions/tests/ac.cpp submissions/solution.cpp
    ./build/cppjudge submissions/solution.cpp "$problem_dir" 1000 128 1 floating 5000 >/tmp/cppjudge_${name}.log 2>&1 || true

    expect_latest_verdict "$name" "System Error"
    printf '[PASS] %-26s -> System Error\n' "$name"
}

run_empty_input_dir_case() {
    local name="empty_input_dir"
    local problem_dir="build/test_problem_errors/${name}"

    restore_problem_config
    rm -rf "$problem_dir"
    mkdir -p "${problem_dir}/input" "${problem_dir}/output"
    cp submissions/tests/ac.cpp submissions/solution.cpp
    ./build/cppjudge submissions/solution.cpp "$problem_dir" 1000 128 1 floating 5000 >/tmp/cppjudge_${name}.log 2>&1 || true

    expect_latest_verdict "$name" "System Error"
    printf '[PASS] %-26s -> System Error\n' "$name"
}

run_isolate_placeholder_case() {
    local name="isolate_placeholder"

    restore_problem_config
    set_problem_sandbox_type isolate
    cp submissions/tests/ac.cpp submissions/solution.cpp
    ./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 floating 5000 >/tmp/cppjudge_${name}.log 2>&1 || true

    expect_latest_verdict "$name" "System Error"

    local error_file
    error_file="$(latest_user_error_file)"
    if [[ -z "$error_file" ]]; then
        fail "${name} -> missing results[0].user_error_file in build/judge_log.json"
    fi

    if [[ ! -f "$error_file" ]]; then
        fail "${name} -> stderr file does not exist: ${error_file}"
    fi

    if ! grep -Fq "Sandbox type not implemented: isolate" "$error_file"; then
        fail "${name} -> stderr file does not contain isolate placeholder message: ${error_file}"
    fi

    printf '[PASS] %-26s -> System Error\n' "$name"
    restore_problem_config
}

restore_problem_config

cases=(
    "ac:Accepted"
    "wa:Wrong Answer"
    "tle:Time Limit Exceeded"
    "mle:Memory Limit Exceeded"
    "ole:Output Limit Exceeded"
    "re:Runtime Error"
    "ce:Compile Error"
)

for item in "${cases[@]}"; do
    name="${item%%:*}"
    expected="${item#*:}"
    run_verdict_case "$name" "$expected"
done

run_invalid_sandbox_type_case
run_invalid_time_limit_type_case
run_negative_float_abs_eps_case
run_invalid_compare_mode_case
run_missing_input_dir_case
run_missing_output_dir_case
run_empty_input_dir_case
run_isolate_placeholder_case

echo "All tests passed."
