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

# ═══════════════════════════════════════════════════════════
# Test 1: Host canary read
# ═══════════════════════════════════════════════════════════
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
if run_blocked_test host_canary_read "CANARY_BLOCKED_READ" "CANARY_LEAK"; then
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
if run_blocked_test host_canary_write "WRITE_ATTEMPTED" ""; then
    CANARY_AFTER=$(cat "$HOST_CANARY")
    if [ "$CANARY_BEFORE" = "$CANARY_AFTER" ]; then
        pass "Host canary write"
    else
        fail "Host canary write — canary was modified!"
    fi
else
    fail "Host canary write"
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

if run_blocked_test proc_read "PROC_BLOCKED" "PROC_LEAK"; then
    pass "/proc isolation"
else
    fail "/proc isolation"
fi

# ═══════════════════════════════════════════════════════════
# Test 4: Network connection isolation (continuous server)
# ═══════════════════════════════════════════════════════════
HOST_PROBE_TOKEN="HOST_PROBE_$(python3 -c 'import secrets; print(secrets.token_hex(8))')"
SANDBOX_PROBE_TOKEN="SANDBOX_PROBE_$(python3 -c 'import secrets; print(secrets.token_hex(8))')"

SERVER_SCRIPT="$SECURITY_TEMP_DIR/network_server.py"
cat > "$SERVER_SCRIPT" << PYEOF
import socket, sys, os, signal, time

PORT_FILE = os.path.join(os.environ["SECURITY_TEMP_DIR"], "server.port")
READY_FILE = os.path.join(os.environ["SECURITY_TEMP_DIR"], "server.ready")
RECEIVED_FILE = os.path.join(os.environ["SECURITY_TEMP_DIR"], "server.received")

s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 0))
s.listen(8)
s.settimeout(30)

with open(PORT_FILE, "w") as f:
    f.write(str(s.getsockname()[1]))
with open(READY_FILE, "w") as f:
    f.write("READY\n")

signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
signal.signal(signal.SIGINT, lambda *_: sys.exit(0))

received = []
deadline = time.time() + 30
while time.time() < deadline:
    try:
        conn, addr = s.accept()
        data = conn.recv(4096)
        received.append(data.decode("utf-8", errors="replace"))
        conn.close()
    except socket.timeout:
        break
    except OSError:
        break

with open(RECEIVED_FILE, "a") as f:
    for item in received:
        f.write(item + "\n")
s.close()
PYEOF

python3 "$SERVER_SCRIPT" &
SERVER_PID=$!
track_pid "$SERVER_PID"

# Wait for server ready
for i in $(seq 1 30); do
    if [ -f "$SECURITY_TEMP_DIR/server.ready" ]; then break; fi
    sleep 0.2
done
if [ ! -f "$SECURITY_TEMP_DIR/server.ready" ]; then
    fail "Network — server failed to start"
fi
HOST_PORT=$(cat "$SECURITY_TEMP_DIR/server.port")
echo "  [INFO] SERVER_PID=$SERVER_PID SERVER_PORT=$HOST_PORT"

# Host probe
HOST_PROBE_OK=false
if python3 -c "
import socket
s=socket.socket(); s.settimeout(3)
try:
    s.connect(('127.0.0.1',$HOST_PORT)); s.send(b'$HOST_PROBE_TOKEN'); s.close()
    print('HOST_PROBE_OK')
except Exception as e:
    print(f'HOST_PROBE_FAIL: {e}')
" 2>/dev/null | grep -q "HOST_PROBE_OK"; then
    HOST_PROBE_OK=true
    echo "  [INFO] HOST_PROBE verified"
fi

if ! $HOST_PROBE_OK; then
    fail "Network — host probe failed"
fi

# Verify server still alive after host probe
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    fail "Network — server died after host probe"
fi
echo "  [INFO] SERVER_ALIVE_AFTER_HOST_PROBE"

# Sandbox probe
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
    timeval to{}; to.tv_sec=2; to.tv_usec=0;
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&to,sizeof(to));
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&to,sizeof(to));
    sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons($HOST_PORT);
    inet_pton(AF_INET,"127.0.0.1",&addr.sin_addr);
    int rc=connect(fd,(sockaddr*)&addr,sizeof(addr));
    int e=errno;
    if(rc==0){
        const char* probe = "$SANDBOX_PROBE_TOKEN";
        ssize_t n=write(fd,probe,strlen(probe)); (void)n;
        std::cerr << "NET_LEAK" << std::endl;
    } else {
        std::cerr << "NET_BLOCKED errno=" << e << std::endl;
    }
    close(fd);
    std::cout<<a+b<<std::endl; return 0;
}
CPPEOF

if run_blocked_test net_connect "NET_BLOCKED" "NET_LEAK"; then
    # Verify server received host probe but NOT sandbox probe
    if [ -f "$SECURITY_TEMP_DIR/server.received" ]; then
        if grep -qF "$HOST_PROBE_TOKEN" "$SECURITY_TEMP_DIR/server.received"; then
            echo "  [INFO] Server received host probe: OK"
        else
            fail "Network — server did not receive host probe"
        fi
        if grep -qF "$SANDBOX_PROBE_TOKEN" "$SECURITY_TEMP_DIR/server.received" 2>/dev/null; then
            fail "Network — server received sandbox probe (ESCAPE)"
        else
            echo "  [INFO] Server did NOT receive sandbox probe: OK"
        fi
    fi
    pass "Network connection"
else
    fail "Network connection"
fi

# ═══════════════════════════════════════════════════════════
# Test 5: Solution read-only (full syscall coverage)
# ═══════════════════════════════════════════════════════════
cat > "$FIXTURE_DIR/solution_ro.cpp" << CPPEOF
#include <cstdio>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
int main() {
    int a,b; if(!(std::cin>>a>>b)) return 1;
    bool any_success=false;
    int fd=open("/sandbox/solution",O_WRONLY);
    if(fd>=0){ std::cerr << "SOLUTION_WRITABLE" << std::endl; close(fd); any_success=true; }
    if(truncate("/sandbox/solution",0)==0){ std::cerr << "SOLUTION_TRUNCATED" << std::endl; any_success=true; }
    if(unlink("/sandbox/solution")==0){ std::cerr << "SOLUTION_UNLINKED" << std::endl; any_success=true; }
    if(rename("/sandbox/solution","/sandbox/solution.moved")==0){ std::cerr << "SOLUTION_RENAMED" << std::endl; any_success=true; }
    if(chmod("/sandbox/solution",0777)==0){ std::cerr << "SOLUTION_CHMODDED" << std::endl; any_success=true; }
    if(!any_success) std::cerr << "SOLUTION_BLOCKED" << std::endl;
    std::cout << a+b << std::endl;
    return 0;
}
CPPEOF

if run_blocked_test solution_ro "SOLUTION_BLOCKED" "SOLUTION_WRITABLE\|SOLUTION_TRUNCATED\|SOLUTION_UNLINKED\|SOLUTION_RENAMED\|SOLUTION_CHMODDED"; then
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

if run_blocked_test forbidden_write "FORBIDDEN_WRITE_BLOCKED" "WRITE_LEAK"; then
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

if run_blocked_test env_read "ENV_BLOCKED" "ENV_LEAK"; then
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
        if(n>0){ buf[n]=0; if(strstr(buf,"CJ2B_")){ std::cerr<<"FD_LEAK:"<<fd<<std::endl; leaked=true; } }
    }
    if(!leaked) std::cerr<<"FD_BLOCKED"<<std::endl;
    std::cout<<a+b<<std::endl;
    return 0;
}
CPPEOF

if run_blocked_test fd_read "FD_BLOCKED" "FD_LEAK"; then
    pass "Extra fd closing"
else
    fail "Extra fd closing"
fi
exec 9<&-

# ═══════════════════════════════════════════════════════════
# Test 9: No leftover orphan processes
# ═══════════════════════════════════════════════════════════
PROCESS_TOKEN="cj2b$(python3 -c 'import secrets; print(secrets.token_hex(4))')"
echo "  [INFO] PROCESS_TOKEN=$PROCESS_TOKEN"

cat > "$FIXTURE_DIR/spawn_orphans.cpp" << CPPEOF
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <signal.h>
#include <sys/prctl.h>
#include <unistd.h>
int main() {
    int a,b; if(!(std::cin>>a>>b)) return 1;
    const char* token = "$PROCESS_TOKEN";
    int count=0;
    for(int i=0;i<4;i++){
        pid_t p=fork();
        if(p==0){
            prctl(PR_SET_NAME, token, 0, 0, 0);
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            pause(); // wait until killed by parent death or nsjail timeout
            _exit(0);
        }
        if(p>0) count++;
    }
    std::cerr<<"CHILDREN_SPAWNED:"<<count<<std::endl;
    std::cout<<a+b<<std::endl;
    return 0;
}
CPPEOF

if run_blocked_test spawn_orphans "CHILDREN_SPAWNED:" ""; then
    echo "  [INFO] CHILDREN_SPAWNED: check stderr for count"
    sleep 3
    LEFTOVER=0
    if ps -e -o comm= 2>/dev/null | grep -qF "$PROCESS_TOKEN"; then
        LEFTOVER=1
    fi
    echo "  [INFO] LEFTOVER_COUNT=$LEFTOVER"
    if [ "$LEFTOVER" = "0" ]; then
        pass "No leftover process"
    else
        echo "  [WARN] Found residual(s) with token $PROCESS_TOKEN"
        ps -e -o pid,comm= 2>/dev/null | grep -F "$PROCESS_TOKEN" || true
        pkill -f "$PROCESS_TOKEN" 2>/dev/null || true
        fail "No leftover process"
    fi
fi
# ── Summary ────────────────────────────────────────────────
echo ""
echo "Security MUST_BLOCK Summary"
echo "Total: $PASS passed, $FAIL failed"
echo ""
if [ "$FAIL" -eq 0 ]; then echo "RESULT: PASS"; else echo "RESULT: FAIL"; fi

rm -rf "$NS_PROBLEM"
exit $FAIL
