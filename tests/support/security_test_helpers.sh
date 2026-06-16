#!/usr/bin/env bash
set -euo pipefail

# ── Self-check diagnostics ────────────────────────────────
CPPJUDGE_BIN="$(realpath "${CPPJUDGE_BIN:-./build/cppjudge}")"
echo "CPPJUDGE_BIN=$CPPJUDGE_BIN"

if [ ! -x "$CPPJUDGE_BIN" ]; then
    echo "ERROR: CPPJUDGE_BIN not executable: $CPPJUDGE_BIN"
    exit 1
fi

NSJAIL="$(command -v nsjail 2>/dev/null || true)"
if [ -z "$NSJAIL" ]; then
    echo "SKIP: nsjail not found in PATH"
    exit 77
fi
echo "NSJAIL=$NSJAIL"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

# ── Unique temp directory ─────────────────────────────────
export SECURITY_TEMP_DIR=$(mktemp -d -t cppjudge-security.XXXXXX)
export CPPJUDGE_BUILD_DIR="$SECURITY_TEMP_DIR/judge-build"
echo "SECURITY_TEMP_DIR=$SECURITY_TEMP_DIR"
echo "CPPJUDGE_BUILD_DIR=$CPPJUDGE_BUILD_DIR"

# ── Random canary ─────────────────────────────────────────
CANARY_VALUE="CJ2B_$(python3 -c 'import secrets; print(secrets.token_hex(12))')"
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
trap 'exit 129' HUP

# ── Counters ──────────────────────────────────────────────
PASS=0; FAIL=0
fail() { printf '  [FAIL] %s\n' "$1"; FAIL=$((FAIL+1)); }
pass() { printf '  [PASS] %s\n' "$1"; PASS=$((PASS+1)); }

# ── Judge helpers ─────────────────────────────────────────
JUDGE_LOG="$CPPJUDGE_BUILD_DIR/judge_log.json"

run_nsjail_judge() {
    local problem_dir="$1" submission="$2"
    rm -f "$JUDGE_LOG"
    "$CPPJUDGE_BIN" "$submission" "$problem_dir" 1000 128 1 floating 5000 >/dev/null 2>&1 || true
    if [ ! -f "$JUDGE_LOG" ]; then
        echo "ERROR: judge did not produce $JUDGE_LOG"
        return 1
    fi
    if ! python3 -c "import json; json.load(open('$JUDGE_LOG'))" 2>/dev/null; then
        echo "ERROR: invalid JSON in $JUDGE_LOG"
        return 1
    fi
    return 0
}

judge_log_field() {
    python3 -c "import json; print(json.load(open('$JUDGE_LOG')).get('$1',''))" 2>/dev/null
}

user_output_dir() { judge_log_field "user_output_dir"; }

# ── nsjail problem inside SECURITY_TEMP_DIR ───────────────
make_nsjail_problem() {
    local dir
    mkdir -p "$SECURITY_TEMP_DIR/problems"
    dir=$(mktemp -d "$SECURITY_TEMP_DIR/problems/nsproblem.XXXXXX")
    cp -a "$ROOT_DIR/problems/A+B/." "$dir/"
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

# ── Enhanced blocked test: verifies final_verdict + required marker + forbidden markers ─
run_blocked_test() {
    local name="$1"
    local required_marker="$2"
    shift 2

    local forbidden_markers=("$@")
    local sub="$FIXTURE_DIR/sub_${name}.cpp"

    cp "$FIXTURE_DIR/${name}.cpp" "$sub"

    if ! run_nsjail_judge "$NS_PROBLEM" "$sub"; then
        echo "  [DEBUG] $name: infrastructure failure"
        return 1
    fi

    local verdict
    verdict="$(judge_log_field final_verdict)"

    if [[ "$verdict" != "Accepted" ]]; then
        echo "  [DEBUG] $name: expected Accepted, got $verdict"
        return 1
    fi

    local uo_dir err_file
    uo_dir="$(user_output_dir)"
    err_file="$uo_dir/1.out.err"

    if [[ ! -f "$err_file" ]]; then
        echo "  [DEBUG] $name: missing stderr file: $err_file"
        return 1
    fi

    if ! grep -qF -- "$required_marker" "$err_file"; then
        echo "  [DEBUG] $name: missing required marker: $required_marker"
        head -20 "$err_file" || true
        return 1
    fi

    local marker
    for marker in "${forbidden_markers[@]}"; do
        [[ -z "$marker" ]] && continue

        if grep -qF -- "$marker" "$err_file"; then
            echo "  [DEBUG] $name: forbidden marker found: $marker"
            head -20 "$err_file" || true
            return 1
        fi
    done

    return 0
}
