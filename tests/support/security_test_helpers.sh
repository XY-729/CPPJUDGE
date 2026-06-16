#!/usr/bin/env bash
set -euo pipefail

# Security test helpers for cppjudge nsjail tests
# Each test gets its own isolated temp dir, build dir, canary, and PID tracking.

CPPJUDGE_BIN="${CPPJUDGE_BIN:-./build/cppjudge}"

# Validate prerequisites
if [ ! -x "$CPPJUDGE_BIN" ]; then
    echo "ERROR: CPPJUDGE_BIN not executable: $CPPJUDGE_BIN"
    exit 1
fi
if ! command -v nsjail >/dev/null 2>&1; then
    echo "SKIP: nsjail not found in PATH"
    exit 0
fi

# ── Unique temp directory ─────────────────────────────────
SECURITY_TEMP_DIR=$(mktemp -d -t cppjudge-security.XXXXXX)
export CPPJUDGE_BUILD_DIR="$SECURITY_TEMP_DIR/judge-build"

# ── Random canary ─────────────────────────────────────────
CANARY_VALUE="CPPJUDGE_CANARY_$(python3 -c 'import secrets; print(secrets.token_hex(16))')"
HOST_CANARY="$SECURITY_TEMP_DIR/host-secret.txt"
printf '%s\n' "$CANARY_VALUE" > "$HOST_CANARY"

# ── Background PID tracking ───────────────────────────────
BACKGROUND_PIDS=()

track_pid() { BACKGROUND_PIDS+=("$1"); }

cleanup() {
    local rc=$?
    for pid in "${BACKGROUND_PIDS[@]:-}"; do
        if kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    rm -rf -- "$SECURITY_TEMP_DIR"
    exit "$rc"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# ── Helpers ───────────────────────────────────────────────
PASS=0; FAIL=0
fail() { printf '  [FAIL] %s\n' "$1"; FAIL=$((FAIL+1)); }
pass() { printf '  [PASS] %s\n' "$1"; PASS=$((PASS+1)); }

# Run judge with nsjail sandbox, return final_verdict
run_nsjail_judge() {
    local problem_dir="$1" submission="$2"
    rm -f "$CPPJUDGE_BUILD_DIR/judge_log.json"
    "$CPPJUDGE_BIN" "$submission" "$problem_dir" 1000 128 1 floating 5000 >/dev/null 2>&1 || true
    if [ ! -f "$CPPJUDGE_BUILD_DIR/judge_log.json" ]; then
        echo "ERROR: judge did not produce judge_log.json"
        return 1
    fi
    python3 -c "import json; json.load(open('$CPPJUDGE_BUILD_DIR/judge_log.json'))" 2>/dev/null || {
        echo "ERROR: invalid judge_log.json"
        return 1
    }
}

judge_log_field() {
    python3 -c "import json; print(json.load(open('$CPPJUDGE_BUILD_DIR/judge_log.json')).get('$1',''))" 2>/dev/null
}

# Create nsjail problem from A+B template
make_nsjail_problem() {
    local dir
    dir=$(mktemp -d -t cppjudge_nsproblem.XXXXXX)
    cp -a problems/A+B/* "$dir/"
    python3 -c "
import json
from pathlib import Path
p = Path('$dir/problem.json')
with p.open('r') as f: data = json.load(f)
data['sandbox_type'] = 'nsjail'
with p.open('w') as f: json.dump(data, f, indent=4); f.write('\n')
"
    echo "$dir"
}

# Get the user_output dir from the last run
user_output_dir() {
    judge_log_field "user_output_dir"
}

# Check if a string appears in a file
file_contains() { grep -qF "$1" "$2" 2>/dev/null; }
