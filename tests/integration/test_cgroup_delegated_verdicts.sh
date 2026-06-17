#!/usr/bin/env bash
set -euo pipefail
# test_cgroup_delegated_verdicts.sh  —  Stage 3B delegated cgroup v2 integration tests.
# MUST run inside a systemd service with Delegate=memory pids. Exits 77 (SKIP) otherwise.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"

PASS=0; FAIL=0; SKIP=0
fail() { printf '[FAIL] %s\n' "$1"; FAIL=$((FAIL+1)); }
pass() { printf '[PASS] %s\n' "$1"; PASS=$((PASS+1)); }
skip() { printf '[SKIP] %s\n' "$1"; SKIP=$((SKIP+1)); }

check_delegated() {
    local cg_mount cg_path test_dir
    cg_mount="/sys/fs/cgroup"
    cg_path=$(grep "^0::" /proc/self/cgroup 2>/dev/null | head -1 | sed 's/^0:://')
    if [ -z "$cg_path" ]; then return 1; fi
    test_dir="${cg_mount}${cg_path}/.cppjudge_del_test_$$"
    if mkdir "$test_dir" 2>/dev/null; then
        rmdir "$test_dir" 2>/dev/null
        if [ -f "${cg_mount}${cg_path}/cgroup.controllers" ]; then
            if grep -q "memory" "${cg_mount}${cg_path}/cgroup.controllers" 2>/dev/null; then
                return 0
            fi
        fi
        return 1
    fi
    return 1
}

if ! check_delegated; then
    echo "SKIP: not running in a delegated cgroup environment"
    echo "This test requires a systemd service with Delegate=memory pids"
    exit 77
fi

echo "=== Delegated cgroup v2 integration tests ==="

HELPER_DIR=$(mktemp -d -t cppjudge_del_helper.XXXXXX)
cleanup_helper() { rm -rf "$HELPER_DIR"; }
trap cleanup_helper EXIT

cat > "$HELPER_DIR/oom_test.cpp" << 'CPPEOF'
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/mman.h>
int main() {
    size_t bytes = 256ULL * 1024 * 1024;
    char* p = (char*)mmap(NULL, bytes, PROT_READ|PROT_WRITE,
                          MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) { fprintf(stderr, "mmap failed\n"); return 1; }
    for (size_t i = 0; i < bytes; i += 4096) p[i] = (char)(i % 256);
    fprintf(stderr, "SURVIVED\n");
    return 0;
}
CPPEOF
g++ -std=c++17 -o "$HELPER_DIR/oom_test" "$HELPER_DIR/oom_test.cpp"

cat > "$HELPER_DIR/ac_test.cpp" << 'CPPEOF'
#include <iostream>
int main() { int a,b; std::cin >> a >> b; std::cout << (a+b) << std::endl; return 0; }
CPPEOF
g++ -std=c++17 -o "$HELPER_DIR/ac_test" "$HELPER_DIR/ac_test.cpp"

cat > "$HELPER_DIR/exit137_test.cpp" << 'CPPEOF'
#include <unistd.h>
int main() { _exit(137); }
CPPEOF
g++ -std=c++17 -o "$HELPER_DIR/exit137_test" "$HELPER_DIR/exit137_test.cpp"

cat > "$HELPER_DIR/selfkill_test.cpp" << 'CPPEOF'
#include <csignal>
#include <unistd.h>
int main() { kill(getpid(), SIGKILL); pause(); return 0; }
CPPEOF
g++ -std=c++17 -o "$HELPER_DIR/selfkill_test" "$HELPER_DIR/selfkill_test.cpp"

cat > "$HELPER_DIR/exit1_test.cpp" << 'CPPEOF'
int main() { return 1; }
CPPEOF
g++ -std=c++17 -o "$HELPER_DIR/exit1_test" "$HELPER_DIR/exit1_test.cpp"

cat > "$HELPER_DIR/scary_test.cpp" << 'CPPEOF'
#include <iostream>
int main() {
    int a,b; std::cin >> a >> b;
    std::cerr << "memory oom bad_alloc Failed to execute nsjail" << std::endl;
    std::cerr << "time limit timed out File size limit exceeded" << std::endl;
    std::cout << (a+b) << std::endl;
    return 0;
}
CPPEOF
g++ -std=c++17 -o "$HELPER_DIR/scary_test" "$HELPER_DIR/scary_test.cpp"

echo "Helpers built"

TEST_BUILD_DIR=$(mktemp -d -t cppjudge_del_build.XXXXXX)
export CPPJUDGE_BUILD_DIR="$TEST_BUILD_DIR"
JUDGE_LOG="$TEST_BUILD_DIR/judge_log.json"
cleanup_build() { rm -rf "$TEST_BUILD_DIR"; }

judge_log_field() {
    python3 -c "import json; print(json.load(open('$JUDGE_LOG')).get('$1',''))" 2>/dev/null
}

run_judge() {
    rm -f "$JUDGE_LOG"
    "$CPPJUDGE_BIN" "$@" >/dev/null 2>&1 || true
}

# T3B-I01: normal exit 0 -> Accepted
echo ""
echo "=== T3B-I01: exit(0) -> Accepted ==="
run_judge "$HELPER_DIR/ac_test.cpp" problems/A+B 1000 128 1 exact 5000
v=$(judge_log_field "final_verdict")
echo "  verdict: $v"
[ "$v" = "Accepted" ] && pass "T3B-I01 exit(0) -> Accepted" || fail "T3B-I01 exit(0) -> expected Accepted, got $v"

# T3B-I02: real OOM -> MLE (nsjail required)
# Run cppjudge as main process to avoid wrapper blocking cgroup delegation.
echo ""
echo "=== T3B-I02: real OOM -> MLE ==="
if command -v nsjail >/dev/null 2>&1; then
    NS_PROB=$(mktemp -d -t cppjudge_del_nsprob.XXXXXX)
    cp -a problems/A+B/* "$NS_PROB/"
    python3 -c "
import json
from pathlib import Path
p = Path('$NS_PROB/problem.json')
with p.open('r') as f: data = json.load(f)
data['sandbox_type'] = 'nsjail'
with p.open('w') as f: json.dump(data, f, indent=4); f.write('\n')
"
    cat > "$HELPER_DIR/run_oom.sh" << SHEOF
#!/usr/bin/env bash
export CPPJUDGE_BUILD_DIR="$TEST_BUILD_DIR"
exec "$CPPJUDGE_BIN" "$HELPER_DIR/oom_test.cpp" "$NS_PROB" 5000 16 1 exact 5000
SHEOF
    chmod +x "$HELPER_DIR/run_oom.sh"
    OOM_OUT=$(systemd-run --user --pipe \
        --unit="cppjudge-stage3b-i02-$$" \
        -p Delegate='memory pids' \
        "$HELPER_DIR/run_oom.sh" 2>&1) || true
    v=$(echo "$OOM_OUT" | grep -oP 'Final Verdict:\s*\K.*' || echo "UNKNOWN")
    echo "  verdict: $v"
    if [ "$v" = "Memory Limit Exceeded" ]; then
        pass "T3B-I02 real OOM -> MLE"
    elif [ "$v" = "Runtime Error" ]; then
        fail "T3B-I02 real OOM -> RE (MLE misclassified)"
    else
        fail "T3B-I02 real OOM -> $v (expected MLE)"
    fi
    rm -rf "$NS_PROB"
else
    skip "T3B-I02 requires nsjail"
fi

# T3B-I03: exit(137) -> RE
echo ""
echo "=== T3B-I03: exit(137) -> RE ==="
run_judge "$HELPER_DIR/exit137_test.cpp" problems/A+B 1000 128 1 exact 5000
v=$(judge_log_field "final_verdict")
echo "  verdict: $v"
[ "$v" = "Runtime Error" ] && pass "T3B-I03 exit(137) -> RE (not MLE)" || fail "T3B-I03 exit(137) -> expected RE, got $v"

# T3B-I04: self SIGKILL -> RE
echo ""
echo "=== T3B-I04: self SIGKILL -> RE ==="
run_judge "$HELPER_DIR/selfkill_test.cpp" problems/A+B 1000 128 1 exact 5000
v=$(judge_log_field "final_verdict")
echo "  verdict: $v"
[ "$v" = "Runtime Error" ] || [ "$v" = "Time Limit Exceeded" ] && pass "T3B-I04 self SIGKILL -> RE/TLE (not MLE)" || fail "T3B-I04 self SIGKILL -> expected RE, got $v"

# T3B-I05: exit(1) -> RE
echo ""
echo "=== T3B-I05: exit(1) -> RE ==="
run_judge "$HELPER_DIR/exit1_test.cpp" problems/A+B 1000 128 1 exact 5000
v=$(judge_log_field "final_verdict")
echo "  verdict: $v"
[ "$v" = "Runtime Error" ] && pass "T3B-I05 exit(1) -> RE" || fail "T3B-I05 exit(1) -> expected RE, got $v"

# T3B-I06: scary stderr -> OK
echo ""
echo "=== T3B-I06: stderr scary -> OK ==="
run_judge "$HELPER_DIR/scary_test.cpp" problems/A+B 1000 128 1 exact 5000
v=$(judge_log_field "final_verdict")
echo "  verdict: $v"
[ "$v" = "Accepted" ] && pass "T3B-I06 scary stderr -> Accepted (not affected)" || fail "T3B-I06 scary stderr -> expected Accepted, got $v"

cleanup_build

echo ""
echo "Delegated Integration: $((PASS + FAIL + SKIP)) total, $PASS passed, $FAIL failed, $SKIP skipped"
[ "$FAIL" -gt 0 ] && exit 1
echo "All delegated integration tests passed."
