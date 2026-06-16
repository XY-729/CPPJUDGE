#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"
source tests/support/security_test_helpers.sh

FIXTURE_DIR="$SECURITY_TEMP_DIR/fixtures"
mkdir -p "$FIXTURE_DIR"
NS_PROBLEM=$(make_nsjail_problem)

echo "Security KNOWN_GAP Report"
echo "========================="

INFRA_FAIL=0

# Helper: run fixture and require valid infrastructure; gap behavior is observed, not judged
run_gap_fixture() {
    local name="$1"
    local sub="$FIXTURE_DIR/sub_${name}.cpp"
    cp "$FIXTURE_DIR/${name}.cpp" "$sub"

    if ! run_nsjail_judge "$NS_PROBLEM" "$sub"; then
        echo "  INFRASTRUCTURE FAILURE: $name — no valid judge log"
        INFRA_FAIL=$((INFRA_FAIL + 1))
        return 1
    fi

    local verdict
    verdict=$(judge_log_field "final_verdict")
    if [ -z "$verdict" ]; then
        echo "  INFRASTRUCTURE FAILURE: $name — missing final_verdict"
        INFRA_FAIL=$((INFRA_FAIL + 1))
        return 1
    fi

    echo "  $name verdict: $verdict"

    local uo_dir err_file
    uo_dir=$(user_output_dir)
    err_file="$uo_dir/1.out.err"
    if [ -f "$err_file" ]; then
        echo "  $name stderr:"
        cat "$err_file" | while IFS= read -r line; do echo "    $line"; done
    else
        echo "  $name stderr: (not found)"
    fi
    return 0
}

# ═══════════════════════════════════════════════════════════
# GAP 1: Socket creation
# ═══════════════════════════════════════════════════════════
cat > "$FIXTURE_DIR/gap_socket.cpp" << CPPEOF
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
int main() {
    int a,b; if(!(std::cin>>a>>b)) return 1;
    int fd=socket(AF_INET,SOCK_STREAM,0);
    std::cerr<<"AF_INET socket: ";
    if(fd>=0){ std::cerr<<"created(fd="<<fd<<")"; close(fd); }
    else std::cerr<<"failed(errno="<<errno<<")";
    fd=socket(AF_UNIX,SOCK_STREAM,0);
    std::cerr<<" AF_UNIX socket: ";
    if(fd>=0){ std::cerr<<"created(fd="<<fd<<")"; close(fd); }
    else std::cerr<<"failed(errno="<<errno<<")";
    std::cerr<<std::endl;
    std::cout<<a+b<<std::endl;
    return 0;
}
CPPEOF

echo "--- Socket creation ---"
run_gap_fixture gap_socket || true

# ═══════════════════════════════════════════════════════════
# GAP 2: Process creation limit
# ═══════════════════════════════════════════════════════════
cat > "$FIXTURE_DIR/gap_processes.cpp" << CPPEOF
#include <cstdio>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
int main() {
    int a,b; if(!(std::cin>>a>>b)) return 1;
    int count=0, max_attempt=64;
    for(int i=0;i<max_attempt;i++){
        pid_t p=fork();
        if(p==0){ usleep(50000); _exit(0); }
        if(p>0) count++; else break;
    }
    for(int i=0;i<count;i++) wait(nullptr);
    std::cerr<<"Processes created: "<<count<<"/"<<max_attempt<<std::endl;
    std::cout<<a+b<<std::endl;
    return 0;
}
CPPEOF

echo "--- Process creation ---"
run_gap_fixture gap_processes || true

# ═══════════════════════════════════════════════════════════
# GAP 3: Thread creation
# ═══════════════════════════════════════════════════════════
cat > "$FIXTURE_DIR/gap_threads.cpp" << CPPEOF
#include <cstdio>
#include <iostream>
#include <pthread.h>
#include <unistd.h>
static void* worker(void*) { usleep(10000); return nullptr; }
int main() {
    int a,b; if(!(std::cin>>a>>b)) return 1;
    int count=0, max_attempt=64;
    for(int i=0;i<max_attempt;i++){
        pthread_t t;
        if(pthread_create(&t,nullptr,worker,nullptr)==0){ pthread_detach(t); count++; }
        else break;
    }
    std::cerr<<"Threads created: "<<count<<"/"<<max_attempt<<std::endl;
    std::cout<<a+b<<std::endl;
    usleep(50000);
    return 0;
}
CPPEOF

echo "--- Thread creation ---"
run_gap_fixture gap_threads || true

# ═══════════════════════════════════════════════════════════
# GAP 4: /bin/sh execution
# ═══════════════════════════════════════════════════════════
cat > "$FIXTURE_DIR/gap_shell.cpp" << CPPEOF
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
int main() {
    int a,b; if(!(std::cin>>a>>b)) return 1;
    std::cerr<<"/bin/sh exists: "<<(access("/bin/sh",F_OK)==0?"yes":"no")<<std::endl;
    int rc=system("echo SHELL_OK 2>/dev/null");
    std::cerr<<"system() rc="<<rc<<" errno="<<errno<<std::endl;
    pid_t p=fork();
    if(p==0){ execl("/bin/sh","sh","-c","echo DIRECT_OK",(char*)nullptr); std::cerr<<"execl errno="<<errno<<std::endl; _exit(127); }
    if(p>0){ waitpid(p,nullptr,0); }
    std::cout<<a+b<<std::endl;
    return 0;
}
CPPEOF

echo "--- Shell execution ---"
run_gap_fixture gap_shell || true

# ── Summary ────────────────────────────────────────────────
echo ""
echo "Security KNOWN_GAP Report"
if [ "$INFRA_FAIL" -gt 0 ]; then
    echo "INFRASTRUCTURE FAILURES: $INFRA_FAIL"
    echo "RESULT: INFRASTRUCTURE_FAILURE"
    exit 1
fi
echo "All gap fixtures executed; gap behaviors recorded above."
echo "GAPS are expected limitations — do not treat as security failures."
exit 0
