#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if ! command -v nsjail >/dev/null 2>&1; then
    echo "nsjail not found, skipping nsjail tests"
    exit 0
fi

rm -rf build
mkdir build
cd build
cmake .. >/dev/null
make >/dev/null
cd ..

problem_dir="problems/A+B_nsjail"
cleanup() {
    rm -rf "$problem_dir"
}
trap cleanup EXIT

rm -rf "$problem_dir"
cp -a "problems/A+B" "$problem_dir"

python3 - <<'PY'
import json
from pathlib import Path

path = Path("problems/A+B_nsjail/problem.json")
with path.open("r", encoding="utf-8") as f:
    data = json.load(f)
data["sandbox_type"] = "nsjail"
with path.open("w", encoding="utf-8") as f:
    json.dump(data, f, indent=4)
    f.write("\n")
PY

fail() {
    printf '[FAIL] %s\n' "$1"
    exit 1
}

run_nsjail_case() {
    local name="$1"
    local submission="$2"
    local expected="$3"
    local stderr_marker="${4:-}"

    ./build/cppjudge "$submission" "$problem_dir" 1000 128 1 floating 5000 >/tmp/cppjudge_nsjail_${name}.log 2>&1 || true

    python3 - "$name" "$expected" "$stderr_marker" <<'PY'
import json
import os
import sys

name = sys.argv[1]
expected = sys.argv[2]
stderr_marker = sys.argv[3]

with open("build/judge_log.json", "r", encoding="utf-8") as f:
    data = json.load(f)

if data.get("final_verdict") != expected:
    print(f'[FAIL] {name} -> expected {expected}, got {data.get("final_verdict")}')
    sys.exit(1)

if data.get("sandbox_type") != "nsjail":
    print(f'[FAIL] {name} -> expected sandbox_type nsjail, got {data.get("sandbox_type")}')
    sys.exit(1)

if not data.get("run_id") or not data.get("run_dir"):
    print(f'[FAIL] {name} -> missing run_id or run_dir')
    sys.exit(1)

if not os.path.isdir(data["run_dir"]):
    print(f'[FAIL] {name} -> run_dir does not exist: {data["run_dir"]}')
    sys.exit(1)

results = data.get("results") or []
if not results:
    print(f'[FAIL] {name} -> missing results')
    sys.exit(1)

error_file = results[0].get("user_error_file", "")
if not error_file or not os.path.isfile(error_file):
    print(f'[FAIL] {name} -> stderr file does not exist: {error_file}')
    sys.exit(1)

with open(error_file, "r", encoding="utf-8", errors="replace") as f:
    stderr_content = f.read()

if "[nsjail] command:" not in stderr_content:
    print(f'[FAIL] {name} -> missing nsjail command log in {error_file}')
    sys.exit(1)

if stderr_marker and stderr_marker not in stderr_content:
    print(f'[FAIL] {name} -> missing stderr marker {stderr_marker} in {error_file}')
    sys.exit(1)

if name == "nsjail_net":
    if "clone_newnet:true" not in stderr_content:
        print(f'[FAIL] {name} -> nsjail log does not show clone_newnet:true in {error_file}')
        sys.exit(1)
    if "--disable_clone_newnet" in stderr_content:
        print(f'[FAIL] {name} -> nsjail command unexpectedly disables network namespace in {error_file}')
        sys.exit(1)
PY

    printf '[PASS] %-18s -> %s\n' "$name" "$expected"
}

run_nsjail_case "nsjail_ac" "submissions/tests/ac.cpp" "Accepted"
run_nsjail_case "nsjail_re" "submissions/tests/re.cpp" "Runtime Error"
run_nsjail_case "nsjail_tle" "submissions/tests/tle.cpp" "Time Limit Exceeded"
run_nsjail_case "nsjail_mle" "submissions/tests/mle.cpp" "Memory Limit Exceeded"
run_nsjail_case "nsjail_ole" "submissions/tests/ole.cpp" "Output Limit Exceeded"
run_nsjail_case "nsjail_stderr" "submissions/tests/security/stderr_output.cpp" "Accepted" "CPPJUDGE_SECURITY_STDERR_MARKER"
run_nsjail_case "nsjail_fs" "submissions/tests/security/fs_isolation.cpp" "Accepted" "CPPJUDGE_NSJAIL_FS_ISOLATED"
run_nsjail_case "nsjail_net" "submissions/tests/security/network_access.cpp" "Accepted" "CPPJUDGE_NSJAIL_NETWORK_ISOLATED"

run_nsjail_compile_isolation_case() {
    local name="nsjail_compile_fs"
    ./build/cppjudge "submissions/tests/security/compile_include_passwd.cpp" "$problem_dir" 1000 128 1 floating 5000 >/tmp/cppjudge_nsjail_\${name}.log 2>&1 || true

    python3 - "$name" <<'PY'
import json
import sys

name = sys.argv[1]
with open("build/judge_log.json", "r", encoding="utf-8") as f:
    data = json.load(f)

if data.get("final_verdict") != "Compile Error":
    print(f'[FAIL] {name} -> expected Compile Error, got {data.get("final_verdict")}')
    sys.exit(1)

compile_error_file = data.get("compile_error_file", "")
if not compile_error_file:
    print(f'[FAIL] {name} -> missing compile_error_file')
    sys.exit(1)

with open(compile_error_file, "r", encoding="utf-8", errors="replace") as f:
    compile_error = f.read()

if "root:x:" in compile_error:
    print(f'[FAIL] {name} -> compile error leaked /etc/passwd content')
    sys.exit(1)

if "/etc/passwd" not in compile_error or "No such file" not in compile_error:
    print(f'[FAIL] {name} -> expected isolated missing-file error')
    sys.exit(1)
PY

    printf '[PASS] %-18s -> Compile Error\n' "$name"
}

run_nsjail_compile_isolation_case

echo "All nsjail tests passed."
