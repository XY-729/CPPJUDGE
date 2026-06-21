#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT_DIR"

CPPJUDGE_BIN="$(printenv CPPJUDGE_BIN || true)"
if [ -z "$CPPJUDGE_BIN" ]; then
    CPPJUDGE_BIN="./build/cppjudge"
fi

TMP_DIR=$(mktemp -d -t cppjudge_quickstart.XXXXXX)
trap 'rm -rf "$TMP_DIR"' EXIT

export CPPJUDGE_BUILD_DIR="$TMP_DIR/build"

"$CPPJUDGE_BIN" judge --problem problems/A+B --submission submissions/solution.cpp \
    >"$TMP_DIR/quickstart.out" 2>&1

LOG="$CPPJUDGE_BUILD_DIR/judge_log.json"
python3 -m json.tool "$LOG" >/tmp/cppjudge_quickstart_log.json

python3 - "$LOG" <<'PY'
import json
import sys

with open(sys.argv[1], encoding="utf-8") as handle:
    data = json.load(handle)

assert data["schema_version"] == 1
assert data["tool"] == "cppjudge"
assert data["cppjudge_version"]
assert data["final_verdict"] == "Accepted"
assert data["passed"] == data["total"]
assert data["submission_file"] == "submissions/solution.cpp"
print("quickstart schema ok")
PY

grep -q "Final Verdict: Accepted" "$TMP_DIR/quickstart.out"
echo "quickstart example accepted"
