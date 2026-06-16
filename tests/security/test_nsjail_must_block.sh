#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT_DIR"
source tests/support/security_test_helpers.sh

FIXTURE_DIR="$SECURITY_TEMP_DIR/fixtures"
mkdir -p "$FIXTURE_DIR"
NS_PROBLEM=$(make_nsjail_problem)

echo "Security MUST_BLOCK Tests"
echo "========================="

# Helper: run a fixture source as submission, check that stderr contains marker.
# The fixture source has the canary path / port baked in at generation time.
run_blocked_test() {
    local name="$1" marker="$2"
    local sub="$FIXTURE_DIR/sub_${name}.cpp"
    cp "$FIXTURE_DIR/${name}.cpp" "$sub"
    run_nsjail_judge "$NS_PROBLEM" "$sub" || true
    local uo_dir err_file
    uo_dir=$(user_output_dir)
    err_file="$uo_dir/1.out.err"
    if [ -f "$err_file" ] && grep -qF "$marker" "$err_file" 2>/dev/null; then
        return 0
    fi
    # Debug: show what happened
    echo "  [DEBUG] expected marker: $marker"
    echo "  [DEBUG] stderr contents:"
    cat "$err_file" 2>/dev/null | head -5 || echo "  (no stderr file)"
    return 1
}

# ═══════════════════════════════════════════════════════════
# Test 1: Host canary read
# ═══════════════════════════════════════════════════════════
# IMPORTANT: unquoted heredoc so $HOST_CANARY is expanded and \" is NOT needed.
cat > "$FIXTURE_DIR/host_canary_read.cpp" << CPPEOF
#include <cstdio>
#include <fstream>
#include <iostream>
int main() {
    int a,b; if(!(std::cin>>a>>b)) return 1;
    std::ifstream f("$HOST_CANARY");
    if(f.is_open()) {
        std::string line; std::getline(f,line);
        std::cerr << "CANARY_LEAK: " << line << std::endl;
        return 1;
    }
    std::cerr << "CANARY_BLOCKED_READ" << std::endl;
    std::cout << a+b << std::endl;
    return 0;
}
CPPEOF

CANARY_BEFORE=$(cat "$HOST_CANARY")
if run_blocked_test host_canary_read "CANARY_BLOCKED_READ"; then
    CANARY_AFTER=$(cat "$HOST_CANARY")
    if [ "$CANARY_BEFORE" = "$CANARY_AFTER" ]; then
        pass "Host canary read"
    else
        fail "Host canary read — canary modified!"
    fi
else
    fail "Host canary read"
fi

# ═══════════════════════════════════════════════════════════
# Test 2: Host canary write
# ═══════════════════════════════════════════════════════════
cat > "$FIXTURE_DIR/host_canary_write.cpp" << CPPEOF
#include <cstdio>
#include <fstream>
#include <iostream>
int main() {
    int a,b; if(!(std::cin>>a>>b)) return 1;
    std::ofstream f("$HOST_CANARY", std::ios::app);
    if(f.is_open()) { f << "OVERWRITE" << std::endl; f.close(); }
    std::cerr << "WRITE_ATTEMPTED" << std::endl;
    std::cout << a+b << std::endl;
    return 0;
}
CPPEOF

CANARY_BEFORE=$(cat "$HOST_CANARY")
run_blocked_test host_canary_write "WRITE_ATTEMPTED" || true
CANARY_AFTER=$(cat "$HOST_CANARY")
if [ "$CANARY_BEFORE" = "$CANARY_AFTER" ]; then
    pass "Host canary write"
else
    fail "Host canary write — canary was modified!"
fi

# ═══════════════════════════════════════════════════════════
# Test 3: /proc isolation
# ═══════════════════════════════════════════════════════════
cat > "$FIXTURE_DIR/proc_read.cpp" << CPPEOF
#include <cstdio>
#include <fstream>
#include <iostream>
int main() {
    int a,b; if(!(std::cin>>a>>b)) return 1;
    bool leaked = false;
    const char* paths[] = {"/proc/self/status","/proc/1/status","/proc/mounts",nullptr};
    for(int i=0; paths[i]; i++) {
        std::ifstream f(paths[i]);
        if(f.is_open()) { std::cerr << "PROC_LEAK:" << paths[i] << std::endl; leaked=true; }
    }
    if(!leaked) std::cerr << "PROC_BLOCKED" << std::endl;
    std::cout << a+b << std::endl;
    return 0;
}
CPPEOF

if run_blocked_test proc_read "PROC_BLOCKED"; then
    pass "/proc isolation"
else
    fail "/proc isolation"
fi

# ═══════════════════════════════════════════════════════════
# Test 4: Network connection isolation
# ═══════════════════════════════════════════════════════════
HOST_PORT=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1]); s.close()')

python3 -c "
import socket, sys
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', $HOST_PORT))
s.listen(1)
s.settimeout(5)
try:
    conn, addr = s.accept()
    conn.close()
    sys.exit(0)
except socket.timeout:
    sys.exit(1)
" &
SERVER_PID=$!
track_pid "$SERVER_PID"
sleep 1

if python3 -c "
import socket
s=socket.socket(); s.settimeout(2)
try:
    s.connect(('127.0.0.1',$HOST_PORT)); s.send(b'host-check'); s.close()
    print('SERVER_OK')
except: print('SERVER_FAIL')
" 2>/dev/null | grep -q "SERVER_OK"; then
    echo "  [INFO] Test server reachable on 127.0.0.1:$HOST_PORT"
else
    fail "Network — test server not reachable from host"
fi

cat > "$FIXTURE_DIR/net_connect.cpp" << CPPEOF
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
int main() {
    int a,b; if(!(std::cin>>a>>b)) return 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd<0){ std::cerr << "SOCKET_FAIL errno=" << errno << std::endl; std::cout<<a+b<<std::endl; return 0; }
    timeval to{}; to.tv_sec=1; to.tv_usec=0;
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&to,sizeof(to));
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&to,sizeof(to));
    sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons($HOST_PORT);
    inet_pton(AF_INET,"127.0.0.1",&addr.sin_addr);
    int rc=connect(fd,(sockaddr*)&addr,sizeof(addr));
    int e=errno; close(fd);
    if(rc==0){ std::cerr << "NET_LEAK" << std::endl; std::cout<<a+b<<std::endl; return 0; }
    std::cerr << "NET_BLOCKED errno=" << e << std::endl;
    std::cout<<a+b<<std::endl; return 0;
}
CPPEOF

if run_blocked_test net_connect "NET_BLOCKED"; then
    pass "Network connection"
else
    fail "Network connection"
fi

# ═══════════════════════════════════════════════════════════
# Test 5: Solution read-only
# ═══════════════════════════════════════════════════════════
cat > "$FIXTURE_DIR/solution_ro.cpp" << CPPEOF
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>
int main() {
    int a,b; if(!(std::cin>>a>>b)) return 1;
    bool blocked=true;
    int fd=open("/sandbox/solution",O_WRONLY);
    if(fd>=0){ std::cerr << "SOLUTION_WRITABLE" << std::endl; close(fd); blocked=false; }
    if(truncate("/sandbox/solution",0)==0){ std::cerr << "SOLUTION_TRUNCATED" << std::endl; blocked=false; }
    if(blocked) std::cerr << "SOLUTION_BLOCKED" << std::endl;
    std::cout << a+b << std::endl;
    return 0;
}
CPPEOF

if run_blocked_test solution_ro "SOLUTION_BLOCKED"; then
    pass "Solution read-only"
else
    fail "Solution read-only"
fi

# ═══════════════════════════════════════════════════════════
# Test 6: Forbidden directory write
# ═══════════════════════════════════════════════════════════
cat > "$FIXTURE_DIR/forbidden_write.cpp" << CPPEOF
#include <cstdio>
#include <fstream>
#include <iostream>
int main() {
    int a,b; if(!(std::cin>>a>>b)) return 1;
    bool all_blocked=true;
    const char* paths[]={"/etc/cppjudge_escape_test","/bin/cppjudge_escape_test","/cppjudge_escape_test",nullptr};
    for(int i=0;paths[i];i++){
        std::ofstream f(paths[i]);
        if(f.is_open()){ std::cerr<<"WRITE_LEAK:"<<paths[i]<<std::endl; f.close(); all_blocked=false; }
    }
    if(all_blocked) std::cerr<<"FORBIDDEN_WRITE_BLOCKED"<<std::endl;
    std::cout<<a+b<<std::endl;
    return 0;
}
CPPEOF

if run_blocked_test forbidden_write "FORBIDDEN_WRITE_BLOCKED"; then
    LEAKS=0
    for p in /etc/cppjudge_escape_test /bin/cppjudge_escape_test /cppjudge_escape_test; do
        [ -f "$p" ] && { echo "  [WARN] Leaked file found: $p"; LEAKS=$((LEAKS+1)); }
    done
    if [ "$LEAKS" -eq 0 ]; then
        pass "Forbidden write"
    else
        fail "Forbidden write — $LEAKS file(s) leaked to host"
    fi
else
    fail "Forbidden write"
fi

# ═══════════════════════════════════════════════════════════
# Test 7: Environment variable clearing
# ═══════════════════════════════════════════════════════════
export CPPJUDGE_SECRET_CANARY="$CANARY_VALUE"
export CPPJUDGE_SECOND_SECRET="SECOND_$CANARY_VALUE"

cat > "$FIXTURE_DIR/env_read.cpp" << CPPEOF
#include <cstdlib>
#include <iostream>
int main() {
    int a,b; if(!(std::cin>>a>>b)) return 1;
    const char* c1 = getenv("CPPJUDGE_SECRET_CANARY");
    const char* c2 = getenv("CPPJUDGE_SECOND_SECRET");
    if(c1||c2) std::cerr << "ENV_LEAK" << std::endl;
    else std::cerr << "ENV_BLOCKED" << std::endl;
    std::cout << a+b << std::endl;
    return 0;
}
CPPEOF

if run_blocked_test env_read "ENV_BLOCKED"; then
    pass "Environment clearing"
else
    fail "Environment clearing"
fi

# ═══════════════════════════════════════════════════════════
# Test 8: Extra file descriptor closing
# ═══════════════════════════════════════════════════════════
FD_CANARY_FILE="$SECURITY_TEMP_DIR/fd_canary.txt"
printf '%s\n' "$CANARY_VALUE" > "$FD_CANARY_FILE"
exec 9<"$FD_CANARY_FILE"
if [ -e "/proc/$$/fd/9" ]; then
    echo "  [INFO] fd 9 open and verified before judge"
else
    fail "Extra fd — cannot verify fd 9 before judge"
fi

cat > "$FIXTURE_DIR/fd_read.cpp" << CPPEOF
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>
int main() {
    int a,b; if(!(std::cin>>a>>b)) return 1;
    bool leaked=false;
    for(int fd=3;fd<=64;fd++){
        char buf[256]; ssize_t n=read(fd,buf,sizeof(buf)-1);
        if(n>0){ buf[n]=0; if(strstr(buf,"CPPJUDGE_CANARY")){ std::cerr<<"FD_LEAK:"<<fd<<std::endl; leaked=true; } }
    }
    if(!leaked) std::cerr<<"FD_BLOCKED"<<std::endl;
    std::cout<<a+b<<std::endl;
    return 0;
}
CPPEOF

if run_blocked_test fd_read "FD_BLOCKED"; then
    pass "Extra fd closing"
else
    fail "Extra fd closing"
fi
exec 9<&-

# ═══════════════════════════════════════════════════════════
# Test 9: No leftover processes
cat > "$FIXTURE_DIR/spawn_brief.cpp" << CPPEOF
#include <cstdio>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
int main() {
    int a,b; if(!(std::cin>>a>>b)) return 1;
    int count=0;
    for(int i=0;i<8;i++){
        pid_t p=fork();
        if(p==0){ usleep(100000); _exit(0); }
        if(p>0) count++;
    }
    for(int i=0;i<count;i++) wait(nullptr);
    std::cerr<<"SPAWN_COUNT:"<<count<<std::endl;
    std::cout<<a+b<<std::endl;
    return 0;
}
CPPEOF

run_blocked_test spawn_brief "SPAWN_COUNT:" || true
sleep 2
MY_UID=$(id -u)
# Check for residual processes: only look for nsjail or solution binaries
# that might still be running, excluding the test script and build tools.
set +o pipefail
LEFTOVER=$(ps -u "$MY_UID" -o pid,cmd --no-headers 2>/dev/null | grep -E "nsjail" | grep -v grep | grep -v "test_nsjail" | wc -l)
set -o pipefail
if [ "$LEFTOVER" -eq 0 ]; then
    pass "No leftover process"
else
    echo "  [WARN] Found $LEFTOVER nsjail residual(s)"
    ps -u "$MY_UID" -o pid,cmd --no-headers 2>/dev/null | grep -E "nsjail" | grep -v grep
    fail "No leftover process"
fi

# Summary
echo ""
echo "Security MUST_BLOCK Summary"
echo "Total: $PASS passed, $FAIL failed"
echo ""
if [ "$FAIL" -eq 0 ]; then echo "RESULT: PASS"; else echo "RESULT: FAIL"; fi

rm -rf "$NS_PROBLEM"
exit $FAIL
