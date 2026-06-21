#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT_DIR"

CPPJUDGE_BIN="$(printenv CPPJUDGE_BIN || true)"
if [ -z "$CPPJUDGE_BIN" ]; then
    CPPJUDGE_BIN="./build/cppjudge"
fi

TMP_DIR=$(mktemp -d -t cppjudge_schema.XXXXXX)
trap 'rm -rf "$TMP_DIR"' EXIT

PROBLEM_DIR="$TMP_DIR/problem"
mkdir -p "$PROBLEM_DIR"
cp -R problems/A+B/input problems/A+B/output "$PROBLEM_DIR/"
cat >"$PROBLEM_DIR/problem.json" <<'JSON'
{
  "title": "Schema test",
  "time_limit_ms": 1000,
  "memory_limit_mb": 128,
  "output_limit_mb": 1,
  "compile_time_limit_ms": 5000,
  "compare_mode": "floating",
  "sandbox_type": "builtin"
}
JSON

export CPPJUDGE_BUILD_DIR="$TMP_DIR/build"
"$CPPJUDGE_BIN" judge --problem "$PROBLEM_DIR" --submission submissions/tests/ac.cpp >/tmp/cppjudge_schema_judge.out 2>&1

LOG="$CPPJUDGE_BUILD_DIR/judge_log.json"
python3 -m json.tool "$LOG" >/tmp/cppjudge_log_check.json

python3 - "$LOG" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    data = json.load(handle)

assert data["schema_version"] == 1
assert data["tool"] == "cppjudge"
assert isinstance(data["cppjudge_version"], str) and data["cppjudge_version"]
assert isinstance(data["git_commit"], str) and data["git_commit"]
assert data["cli_mode"] == "judge"
assert data["problem_dir"]
assert data["submission_file"].endswith("ac.cpp")
assert data["final_verdict"] == "Accepted"
assert isinstance(data["results"], list) and data["results"]
for case in data["results"]:
    assert "case" in case
    assert "verdict" in case
print("schema ok")
PY

export CPPJUDGE_BUILD_DIR="$TMP_DIR/legacy_build"
"$CPPJUDGE_BIN" submissions/tests/ac.cpp "$PROBLEM_DIR" 1000 128 1 floating 5000 >/tmp/cppjudge_schema_legacy.out 2>&1
python3 - "$CPPJUDGE_BUILD_DIR/judge_log.json" <<'PY'
import json
import sys
with open(sys.argv[1], encoding="utf-8") as handle:
    data = json.load(handle)
assert data["schema_version"] == 1
assert data["tool"] == "cppjudge"
assert data["cli_mode"] == "legacy"
assert data["final_verdict"] == "Accepted"
print("legacy schema ok")
PY

set +e
"$CPPJUDGE_BIN" judge --submission submissions/tests/ac.cpp >/tmp/cppjudge_missing_problem.out 2>&1
missing_problem_rc=$?
"$CPPJUDGE_BIN" judge --problem "$PROBLEM_DIR" >/tmp/cppjudge_missing_submission.out 2>&1
missing_submission_rc=$?
set -e

if [ "$missing_problem_rc" -ne 2 ] || [ "$missing_submission_rc" -ne 2 ]; then
    echo "missing argument exit code failure: problem=$missing_problem_rc submission=$missing_submission_rc" >&2
    exit 1
fi

echo "judge log schema tests passed"
