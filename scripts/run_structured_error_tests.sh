#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

PASS=0
FAIL=0

fail() {
    printf '[FAIL] %s\n' "$1"
    FAIL=$((FAIL + 1))
}

pass() {
    printf '[PASS] %s\n' "$1"
    PASS=$((PASS + 1))
}

cleanup() {
    rm -rf /tmp/cppjudge_se_tests
}
trap cleanup EXIT
cleanup
mkdir -p /tmp/cppjudge_se_tests

rm -rf build
mkdir build
cd build
cmake .. >/dev/null 2>&1
make >/dev/null 2>&1
cd ..

latest_final_verdict() {
    python3 -c "import json; print(json.load(open('build/judge_log.json')).get('final_verdict', ''))"
}

latest_error_field() {
    python3 -c "import json; print(json.load(open('build/judge_log.json')).get('error', ''))"
}

run_judge_builtin() {
    cp "$1" submissions/solution.cpp
    ./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 floating 5000 >/tmp/cppjudge_se_test.log 2>&1 || true
}

echo "=== Test 1: User stderr fakes nsjail exec error (builtin) ==="
run_judge_builtin submissions/tests/security/stderr_fake_nsjail_exec.cpp
verdict=$(latest_final_verdict)
if [ "$verdict" = "System Error" ]; then
    fail "Test1: fake nsjail exec in stderr -> SE"
else
    pass "Test1: fake nsjail exec in stderr -> $verdict (not SE)"
fi

echo "=== Test 2: User stderr fakes nsjail fs error (builtin) ==="
run_judge_builtin submissions/tests/security/stderr_fake_nsjail_fs.cpp
verdict=$(latest_final_verdict)
if [ "$verdict" = "System Error" ]; then
    fail "Test2: fake nsjail fs in stderr -> SE"
else
    pass "Test2: fake nsjail fs in stderr -> $verdict (not SE)"
fi

echo "=== Test 3: User stderr fakes isolate message (builtin) ==="
run_judge_builtin submissions/tests/security/stderr_fake_isolate.cpp
verdict=$(latest_final_verdict)
if [ "$verdict" = "System Error" ]; then
    fail "Test3: fake isolate in stderr -> SE"
else
    pass "Test3: fake isolate in stderr -> $verdict (not SE)"
fi

echo "=== Test 4: g++ exec failure -> SE ==="
EMPTY_BIN=/tmp/cppjudge_se_tests/empty_bin
rm -rf "$EMPTY_BIN"
mkdir -p "$EMPTY_BIN"  # empty dir, no g++ -> execvp fails with ENOENT
cp submissions/tests/ac.cpp submissions/solution.cpp
PATH="$EMPTY_BIN" ./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 floating 5000 >/tmp/cppjudge_se_test.log 2>&1 || true
verdict=$(latest_final_verdict)
if [ "$verdict" = "System Error" ]; then
    pass "Test4: g++ exec failure -> SE"
else
    fail "Test4: g++ exec failure -> $verdict (expected SE)"
fi

echo "=== Test 5: nsjail outer exec failure -> SE ==="
mkdir -p "$EMPTY_BIN"  # empty dir, no nsjail -> execvp fails with ENOENT
python3 -c "
import json
from pathlib import Path
path = Path('problems/A+B/problem.json')
with path.open('r') as f:
    data = json.load(f)
data['sandbox_type'] = 'nsjail'
with path.open('w') as f:
    json.dump(data, f, indent=4)
    f.write('\n')
"
cp submissions/tests/ac.cpp submissions/solution.cpp
PATH="$EMPTY_BIN" ./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 floating 5000 >/tmp/cppjudge_se_test.log 2>&1 || true
verdict=$(latest_final_verdict)
if [ "$verdict" = "System Error" ]; then
    pass "Test5: nsjail exec failure -> SE"
else
    fail "Test5: nsjail exec failure -> $verdict (expected SE)"
fi
python3 -c "
import json
from pathlib import Path
path = Path('problems/A+B/problem.json')
with path.open('r') as f:
    data = json.load(f)
data.pop('sandbox_type', None)
with path.open('w') as f:
    json.dump(data, f, indent=4)
    f.write('\n')
"

echo "=== Test 6: isolate sandbox -> SE ==="
python3 -c "
import json
from pathlib import Path
path = Path('problems/A+B/problem.json')
with path.open('r') as f:
    data = json.load(f)
data['sandbox_type'] = 'isolate'
with path.open('w') as f:
    json.dump(data, f, indent=4)
    f.write('\n')
"
cp submissions/tests/ac.cpp submissions/solution.cpp
./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 floating 5000 >/tmp/cppjudge_se_test.log 2>&1 || true
verdict=$(latest_final_verdict)
if [ "$verdict" = "System Error" ]; then
    pass "Test6: isolate sandbox -> SE"
else
    fail "Test6: isolate sandbox -> $verdict (expected SE)"
fi
python3 -c "
import json
from pathlib import Path
path = Path('problems/A+B/problem.json')
with path.open('r') as f:
    data = json.load(f)
data.pop('sandbox_type', None)
with path.open('w') as f:
    json.dump(data, f, indent=4)
    f.write('\n')
"

echo "=== Test 7: SIGSEGV -> RE with signal ==="
run_judge_builtin submissions/tests/re.cpp
python3 -c "
import json
with open('build/judge_log.json') as f:
    data = json.load(f)
results = data.get('results') or []
if not results:
    print('FAIL: no results')
    exit(1)
r = results[0]
run_result = r.get('run_result', '')
signal = r.get('signal', -1)
if run_result == 'RE' and signal >= 0:
    print(f'OK: RE with signal={signal}')
else:
    print(f'FAIL: run_result={run_result}, signal={signal}')
    exit(1)
"
if [ $? -eq 0 ]; then
    pass "Test7: SIGSEGV -> RE with signal recorded"
else
    fail "Test7: SIGSEGV -> RE with signal recorded"
fi

echo "=== Test 8: Syntax error -> CE ==="
run_judge_builtin submissions/tests/ce.cpp
verdict=$(latest_final_verdict)
if [ "$verdict" = "Compile Error" ]; then
    pass "Test8: syntax error -> CE"
else
    fail "Test8: syntax error -> $verdict (expected CE)"
fi

echo "=== Test 9: Missing input data -> SE ==="
TEST_PROBLEM=/tmp/cppjudge_se_tests/no_input
rm -rf "$TEST_PROBLEM"
mkdir -p "$TEST_PROBLEM/input" "$TEST_PROBLEM/output"
cp submissions/tests/ac.cpp submissions/solution.cpp
./build/cppjudge submissions/solution.cpp "$TEST_PROBLEM" 1000 128 1 floating 5000 >/tmp/cppjudge_se_test.log 2>&1 || true
verdict=$(latest_final_verdict)
if [ "$verdict" = "System Error" ]; then
    pass "Test9: empty input dir -> SE"
else
    fail "Test9: empty input dir -> $verdict (expected SE)"
fi

echo ""
echo "========================================"
echo "Structured Error Tests: $((PASS + FAIL)) total, $PASS passed, $FAIL failed"
echo "========================================"

if [ "$FAIL" -gt 0 ]; then
    exit 1
fi
echo "All structured error tests passed."
