#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

PASS=0; FAIL=0

fail() { printf '[FAIL] %s\n' "$1"; FAIL=$((FAIL + 1)); }
pass() { printf '[PASS] %s\n' "$1"; PASS=$((PASS + 1)); }

# ── Backup repo files (exact byte preservation) ────────────
SOLUTION_BAK=$(mktemp)
PROBLEM_JSON_BAK=$(mktemp)
cp submissions/solution.cpp "$SOLUTION_BAK"
cp problems/A+B/problem.json "$PROBLEM_JSON_BAK"

restore_repo_files() {
    cp "$SOLUTION_BAK" submissions/solution.cpp 2>/dev/null || true
    cp "$PROBLEM_JSON_BAK" problems/A+B/problem.json 2>/dev/null || true
    rm -f "$SOLUTION_BAK" "$PROBLEM_JSON_BAK"
}

cleanup() {
    restore_repo_files
    rm -rf /tmp/cppjudge_se_tests
}
trap cleanup EXIT INT TERM

rm -rf /tmp/cppjudge_se_tests
mkdir -p /tmp/cppjudge_se_tests

# ── Helpers ────────────────────────────────────────────────
set_solution() { cp "$1" submissions/solution.cpp; }

set_problem_sandbox() {
    python3 - "$1" <<'PY'
import json, sys
from pathlib import Path
path = Path("problems/A+B/problem.json")
with path.open("r") as f: data = json.load(f)
data["sandbox_type"] = sys.argv[1]
with path.open("w") as f: json.dump(data, f, indent=4); f.write("\n")
PY
}

require_verdict() {
    local name="$1" expected="$2" actual
    actual=$(python3 -c "import json; print(json.load(open('build/judge_log.json')).get('final_verdict', ''))")
    [ "$actual" = "$expected" ] && pass "${name} -> ${expected}" || fail "${name} -> expected ${expected}, got ${actual}"
}

require_error_contains() {
    local name="$1" keyword="$2" actual
    actual=$(python3 -c "import json; print(json.load(open('build/judge_log.json')).get('error', ''))")
    [[ "$actual" == *"$keyword"* ]] || fail "${name} -> error missing '${keyword}': ${actual}"
}

run_judge() {
    local name="$1"; shift
    rm -f build/judge_log.json
    ${CPPJUDGE_BIN:-./build/cppjudge} "$@" >"/tmp/cppjudge_se_${name}.log" 2>&1 || true
    if [ ! -f build/judge_log.json ]; then
        fail "${name} -> judge did not produce judge_log.json"; return 1
    fi
    python3 -c "import json; json.load(open('build/judge_log.json'))" 2>/dev/null || {
        fail "${name} -> judge_log.json invalid JSON"; return 1
    }
}

# ── Build ──────────────────────────────────────────────────
if [ -z "${CPPJUDGE_BIN:-}" ]; then
rm -rf build
mkdir build
cd build
cmake .. >/dev/null 2>&1
make >/dev/null 2>&1
cd ..
fi

# Derive build directory from CPPJUDGE_BIN for linking test harnesses.
# CTest sets CPPJUDGE_BIN to <build_dir>/cppjudge, so dirname gives us the
# build directory containing libcppjudge_lib.a.
if [ -n "${CPPJUDGE_BIN:-}" ]; then
    BUILD_OBJ_DIR="${CPPJUDGE_BIN%/*}"
else
    BUILD_OBJ_DIR="build"
fi

# ════════════════════════════════════════════════════════════
# Tests 1-3: stderr fake tests -> MUST be Accepted
# ════════════════════════════════════════════════════════════
for t in 1 2 3; do
    case $t in
        1) src="submissions/tests/security/stderr_fake_nsjail_exec.cpp"; label="fake nsjail exec" ;;
        2) src="submissions/tests/security/stderr_fake_nsjail_fs.cpp";   label="fake nsjail fs" ;;
        3) src="submissions/tests/security/stderr_fake_isolate.cpp";     label="fake isolate" ;;
    esac
    echo "=== Test ${t}: User stderr ${label} -> Accepted ==="
    set_solution "$src"
    run_judge "t${t}" submissions/solution.cpp problems/A+B 1000 128 1 floating 5000
    require_verdict "Test${t}" "Accepted"
done

# ════════════════════════════════════════════════════════════
# Test 4: g++ exec failure -> SE
# ════════════════════════════════════════════════════════════
echo "=== Test 4: g++ exec failure -> SE ==="
EMPTY_BIN=/tmp/cppjudge_se_tests/empty_bin
rm -rf "$EMPTY_BIN"; mkdir -p "$EMPTY_BIN"
set_solution submissions/tests/ac.cpp
rm -f build/judge_log.json
PATH="$EMPTY_BIN" ${CPPJUDGE_BIN:-./build/cppjudge} submissions/solution.cpp problems/A+B 1000 128 1 floating 5000 >/tmp/cppjudge_se_t4.log 2>&1 || true
if [ -f build/judge_log.json ]; then
    require_verdict "Test4" "System Error"
    require_error_contains "Test4" "g++"
else
    fail "Test4 -> judge did not produce judge_log.json"
fi

# ════════════════════════════════════════════════════════════
# Test 5A: nsjail preflight failure -> SE
# ════════════════════════════════════════════════════════════
echo "=== Test 5A: nsjail not in PATH -> preflight SE ==="
set_problem_sandbox nsjail
set_solution submissions/tests/ac.cpp
rm -f build/judge_log.json
PATH="$EMPTY_BIN" ${CPPJUDGE_BIN:-./build/cppjudge} submissions/solution.cpp problems/A+B 1000 128 1 floating 5000 >/tmp/cppjudge_se_t5a.log 2>&1 || true
if [ -f build/judge_log.json ]; then
    actual=$(python3 -c "import json; print(json.load(open('build/judge_log.json')).get('final_verdict', ''))")
    error=$(python3 -c "import json; print(json.load(open('build/judge_log.json')).get('error', ''))")
    if [ "$actual" = "System Error" ] && [[ "$error" == *"nsjail"* ]]; then
        pass "Test5A -> SE (preflight: nsjail not found)"
    else
        fail "Test5A -> expected preflight SE, got ${actual}: ${error}"
    fi
else
    fail "Test5A -> judge did not produce judge_log.json"
fi
cp "$PROBLEM_JSON_BAK" problems/A+B/problem.json

# ════════════════════════════════════════════════════════════
# Test 5B: nsjail preflight passes, exec fails -> SE (exec pipe)
# ════════════════════════════════════════════════════════════
echo "=== Test 5B: nsjail outer exec failure -> SE (exec pipe) ==="
FAKE_BIN=/tmp/cppjudge_se_tests/fake_bin
rm -rf "$FAKE_BIN"; mkdir -p "$FAKE_BIN"
printf '#! /definitely/nonexistent/cppjudge-test-interpreter\n' > "$FAKE_BIN/nsjail"
chmod 755 "$FAKE_BIN/nsjail"

if [ -x "$FAKE_BIN/nsjail" ]; then
    MIXED_PATH="$FAKE_BIN:/usr/bin:/bin"
    set_problem_sandbox nsjail
    set_solution submissions/tests/ac.cpp
    rm -f build/judge_log.json
    PATH="$MIXED_PATH" ${CPPJUDGE_BIN:-./build/cppjudge} submissions/solution.cpp problems/A+B 1000 128 1 floating 5000 >/tmp/cppjudge_se_t5b.log 2>&1 || true
    if [ -f build/judge_log.json ]; then
        actual=$(python3 -c "import json; print(json.load(open('build/judge_log.json')).get('final_verdict', ''))")
        error=$(python3 -c "import json; print(json.load(open('build/judge_log.json')).get('error', ''))")
        if [ "$actual" = "System Error" ]; then
            pass "Test5B -> SE (exec pipe: ${error})"
        else
            fail "Test5B -> expected SE, got ${actual}: ${error}"
        fi
    else
        fail "Test5B -> judge did not produce judge_log.json"
    fi
else
    fail "Test5B -> fake nsjail is not executable"
fi
cp "$PROBLEM_JSON_BAK" problems/A+B/problem.json

# ════════════════════════════════════════════════════════════
# Test 6: isolate -> SE (compile phase, no builtin fallback)
# ════════════════════════════════════════════════════════════
echo "=== Test 6: isolate -> SE (no builtin fallback) ==="
set_problem_sandbox isolate
set_solution submissions/tests/ac.cpp
run_judge "t6" submissions/solution.cpp problems/A+B 1000 128 1 floating 5000
require_verdict "Test6" "System Error"
require_error_contains "Test6" "not implemented"
cp "$PROBLEM_JSON_BAK" problems/A+B/problem.json

# ════════════════════════════════════════════════════════════
# Test 7: SIGSEGV -> RE with signal
# ════════════════════════════════════════════════════════════
echo "=== Test 7: SIGSEGV -> RE with signal ==="
set_solution submissions/tests/re.cpp
run_judge "t7" submissions/solution.cpp problems/A+B 1000 128 1 floating 5000
python3 -c "
import json
with open('build/judge_log.json') as f: data = json.load(f)
results = data.get('results') or []
assert results, 'no results'
r = results[0]
assert r.get('run_result') == 'RE', f\"run_result={r.get('run_result')}\"
assert r.get('signal', -1) >= 0, f\"signal={r.get('signal')}\"
" && pass "Test7 -> RE with signal" || fail "Test7"

# ════════════════════════════════════════════════════════════
# Test 8: Syntax error -> CE
# ════════════════════════════════════════════════════════════
echo "=== Test 8: Syntax error -> CE ==="
set_solution submissions/tests/ce.cpp
run_judge "t8" submissions/solution.cpp problems/A+B 1000 128 1 floating 5000
require_verdict "Test8" "Compile Error"

# ════════════════════════════════════════════════════════════
# Test 9: Missing input data -> SE
# ════════════════════════════════════════════════════════════
echo "=== Test 9: Missing input data -> SE ==="
TEST_PROBLEM=/tmp/cppjudge_se_tests/no_input
rm -rf "$TEST_PROBLEM"; mkdir -p "$TEST_PROBLEM/input" "$TEST_PROBLEM/output"
set_solution submissions/tests/ac.cpp
run_judge "t9" submissions/solution.cpp "$TEST_PROBLEM" 1000 128 1 floating 5000
require_verdict "Test9" "System Error"

# ════════════════════════════════════════════════════════════
# Test 10: builtin exec failure + compile error file -> SE (harness)
# ════════════════════════════════════════════════════════════
echo "=== Test 10: builtin exec failure + compile err file -> SE ==="
cat > /tmp/cppjudge_se_tests/harness.cpp << 'CPPEOF'
#include "runner.h"
#include "compiler.h"
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } else { printf("  ok: %s\n", msg); } } while(0)
int main() {
    RunInfo info = run_program("/tmp/nonexistent_xyz", "/dev/null", "/tmp/h_out.txt", 1000, 128, 1, SandboxType::BUILTIN);
    CHECK(info.result == RunResult::SE, "non-existent exec -> SE");
    CHECK(info.system_error, "system_error flag");
    const char* d = "/tmp/cppjudge_se_tests/err_is_dir";
    mkdir(d, 0755);
    FILE* f = fopen("/tmp/cppjudge_se_tests/src.cpp", "w");
    fprintf(f, "int main(){return 0;}\n"); fclose(f);
    CompileInfo ci = compile_cpp_structured("/tmp/cppjudge_se_tests/src.cpp", "/tmp/cppjudge_se_tests/out_exe", d, 5000, SandboxType::BUILTIN);
    CHECK(ci.result == CompileResult::SE, "err dir -> SE");
    CHECK(ci.system_error, "err dir system_error");
    rmdir(d); unlink("/tmp/cppjudge_se_tests/src.cpp");
    if (failures) { fprintf(stderr, "%d failure(s)\n", failures); return 1; }
    return 0;
}
CPPEOF

harness_log=/tmp/cppjudge_se_tests/harness_build.log
set +e
g++ -std=c++17 -I"$ROOT_DIR/src" \
    /tmp/cppjudge_se_tests/harness.cpp \
    "$BUILD_OBJ_DIR/libcppjudge_lib.a" \
    -o /tmp/cppjudge_se_tests/harness >"$harness_log" 2>&1
harness_build_rc=$?
set -e

if [ $harness_build_rc -ne 0 ] || [ ! -x /tmp/cppjudge_se_tests/harness ]; then
    fail "Test10 -> harness build failed"
    cat "$harness_log" 2>/dev/null || true
else
    set +e
    /tmp/cppjudge_se_tests/harness >/tmp/cppjudge_se_tests/harness_out.txt 2>&1
    harness_rc=$?
    set -e
    if [ $harness_rc -eq 0 ]; then
        pass "Test10 -> SE (harness: exec failure + compile error file)"
    else
        fail "Test10 -> harness failed: $(cat /tmp/cppjudge_se_tests/harness_out.txt 2>/dev/null)"
    fi
fi

# ════════════════════════════════════════════════════════════
# Test 11: Repo files unchanged
# ════════════════════════════════════════════════════════════
echo "=== Test 11: repo files unchanged ==="
restore_repo_files
if git diff --exit-code -- submissions/solution.cpp problems/A+B/problem.json >/dev/null 2>&1; then
    pass "Test11 -> repo files unchanged"
else
    fail "Test11 -> repo files modified!"
    git diff -- submissions/solution.cpp problems/A+B/problem.json
fi

# ── Summary ────────────────────────────────────────────────
echo ""
echo "========================================"
echo "Structured Error Tests: $((PASS + FAIL)) total, $PASS passed, $FAIL failed"
echo "========================================"

[ "$FAIL" -eq 0 ] || exit 1
echo "All structured error tests passed."
