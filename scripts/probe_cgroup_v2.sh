#!/usr/bin/env bash
set -euo pipefail

LIFECYCLE_MODE=false
if [ "${1:-}" = "--lifecycle" ]; then
    LIFECYCLE_MODE=true
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROBE_TMP="/tmp/cppjudge_probe_tmp_$$"
mkdir -p "$PROBE_TMP"

# ─── Global state ──────────────────────────────────────────
DELEGATED_CGROUP=""
CGROUP2_MOUNT=""

# ─── Safe cleanup: only removes the exact directory stored in PROBE_CG ───
PROBE_CG=""
cleanup_probe_cgroup() {
    if [ -z "${PROBE_CG:-}" ] || [ ! -d "${PROBE_CG:-}" ]; then
        return
    fi
    local cg="$PROBE_CG"
    # Try cgroup.kill first if populated
    if [ -f "$cg/cgroup.kill" ]; then
        local pop
        pop=$(awk '/^populated /{print $2}' "$cg/cgroup.events" 2>/dev/null || echo "0")
        if [ "${pop:-0}" -ne 0 ]; then
            echo 1 > "$cg/cgroup.kill" 2>/dev/null || true
            local waited=0
            while [ "$waited" -lt 20 ]; do
                pop=$(awk '/^populated /{print $2}' "$cg/cgroup.events" 2>/dev/null || echo "0")
                [ "${pop:-1}" -eq 0 ] && break
                sleep 0.1
                waited=$((waited + 1))
            done
        fi
    fi
    rmdir "$cg" 2>/dev/null || true
    PROBE_CG=""
}

cleanup_all() {
    cleanup_probe_cgroup
    rm -rf "$PROBE_TMP"
}
trap cleanup_all EXIT INT TERM

# ─── Helpers ────────────────────────────────────────────────
print_section() { printf '\n===== %s =====\n' "$1"; }
print_kv() { printf '%-36s %s\n' "$1:" "$2"; }

# ─── Build C barrier helper ─────────────────────────────────
build_barrier_helper() {
    cat > "$PROBE_TMP/barrier_helper.c" << 'CPPEOF'
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>

static void touch_pages(size_t size_mb) {
    size_t bytes = size_mb * 1024ULL * 1024ULL;
    char* p = (char*)mmap(NULL, bytes, PROT_READ|PROT_WRITE,
                          MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        fprintf(stderr, "BARRIER_HELPER: mmap(%zu MB) failed: %s\n", size_mb, strerror(errno));
        _exit(1);
    }
    for (size_t i = 0; i < bytes; i += 4096) {
        p[i] = (char)(i % 256);
    }
    fprintf(stderr, "BARRIER_HELPER: SURVIVED (unexpected - %zu MB allocated)\n", size_mb);
    _exit(0);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <mode> [cgroup_path]\n", argv[0]);
        fprintf(stderr, "  mode: oom | exit42 | selfkill | exit1 | exit0 | stderr_scary\n");
        return 1;
    }
    const char* mode = argv[1];
    const char* cg_path = (argc > 2) ? argv[2] : NULL;

    /* ── Phase 1: move self into cgroup if path provided ── */
    if (cg_path != NULL && strlen(cg_path) > 0) {
        char procs_path[512];
        snprintf(procs_path, sizeof(procs_path), "%s/cgroup.procs", cg_path);
        char pid_str[32];
        snprintf(pid_str, sizeof(pid_str), "%d", getpid());

        int fd = open(procs_path, O_WRONLY);
        if (fd < 0) {
            fprintf(stderr, "BARRIER_HELPER: open(%s) failed: %s\n", procs_path, strerror(errno));
            return 2; /* signal cgroup join failure */
        }
        ssize_t n = write(fd, pid_str, strlen(pid_str));
        if (n < 0) {
            fprintf(stderr, "BARRIER_HELPER: write(%s) failed: %s\n", procs_path, strerror(errno));
            close(fd);
            return 2;
        }
        close(fd);

        /* Verify */
        char verify_cmd[256];
        snprintf(verify_cmd, sizeof(verify_cmd),
                 "grep -q '%s' /proc/self/cgroup", cg_path + 1); /* skip leading /sys/fs/cgroup */
        int verified = system(verify_cmd);
        if (verified != 0) {
            fprintf(stderr, "BARRIER_HELPER: cgroup join verification failed\n");
            return 2;
        }
    }

    /* ── Phase 2: execute the requested mode ── */
    if (strcmp(mode, "oom") == 0) {
        touch_pages(48); /* 48 MiB */
    } else if (strcmp(mode, "exit42") == 0) {
        _exit(42);   /* 42 not 137: shell cannot distinguish _exit(137) from SIGKILL */
    } else if (strcmp(mode, "selfkill") == 0) {
        kill(getpid(), SIGKILL);
        pause();
    } else if (strcmp(mode, "exit1") == 0) {
        _exit(1);
    } else if (strcmp(mode, "exit0") == 0) {
        _exit(0);
    } else if (strcmp(mode, "stderr_scary") == 0) {
        fprintf(stderr, "memory oom bad_alloc Failed to execute nsjail\n");
        fprintf(stderr, "time limit timed out File size limit exceeded\n");
        _exit(0);
    } else {
        fprintf(stderr, "BARRIER_HELPER: unknown mode: %s\n", mode);
        return 1;
    }
    return 0;
}
CPPEOF
    gcc -std=c99 -o "$PROBE_TMP/barrier_helper" "$PROBE_TMP/barrier_helper.c" 2>&1
}

# ─── Discovery (always run, read-only) ──────────────────────

print_section "System identity"
print_kv "kernel" "$(uname -r)"
print_kv "uid" "$(id -u) ($(id -un))"

print_section "cgroup v2 detection"
CGROUP2_FS=$(stat -fc %T /sys/fs/cgroup 2>/dev/null || echo "unknown")
print_kv "fs type" "$CGROUP2_FS"
CGROUP2_MOUNT=$(findmnt -n -o TARGET -t cgroup2 2>/dev/null || echo "/sys/fs/cgroup")
print_kv "cgroup2 mount" "$CGROUP2_MOUNT"

print_section "Current process cgroup"
CURRENT_CGROUP=$(cat /proc/self/cgroup 2>/dev/null | head -1 | sed 's/^0:://')
print_kv "cgroup path" "$CURRENT_CGROUP"

print_section "SELinux cgroup boolean"
SELINUX_CGROUP_BOOL=$(getsebool container_manage_cgroup 2>/dev/null | awk '{print $NF}' || echo "unknown")
print_kv "container_manage_cgroup" "$SELINUX_CGROUP_BOOL"

print_section "systemd delegation"
CTLGROUP_USER=""
DELEGATE_VALUE=""
if command -v systemctl >/dev/null 2>&1; then
    UID_NUM=$(id -u)
    CTLGROUP_USER=$(systemctl show "user@${UID_NUM}.service" --property=ControlGroup --no-pager 2>/dev/null | cut -d= -f2 || echo "")
    DELEGATE_VALUE=$(systemctl show "user@${UID_NUM}.service" --property=Delegate --no-pager 2>/dev/null | cut -d= -f2 || echo "unknown")
    SLICE_USER=$(systemctl show "user@${UID_NUM}.service" --property=Slice --no-pager 2>/dev/null | cut -d= -f2 || echo "")
    print_kv "ControlGroup" "$CTLGROUP_USER"
    print_kv "Delegate" "$DELEGATE_VALUE"
    print_kv "Slice" "$SLICE_USER"
else
    print_kv "systemctl" "not available"
fi

print_section "Delegated cgroup filesystem"
DELEGATED_CGROUP=""
if [ -n "$CTLGROUP_USER" ] && [ -d "${CGROUP2_MOUNT}${CTLGROUP_USER}" ]; then
    DELEGATED_CGROUP="${CGROUP2_MOUNT}${CTLGROUP_USER}"
fi

if [ -z "$DELEGATED_CGROUP" ] || [ ! -d "$DELEGATED_CGROUP" ]; then
    echo "FATAL: no delegated cgroup found"
    echo "Systemd user@$(id -u).service with Delegate=yes is required"
    exit 1
fi

print_kv "delegated root" "$DELEGATED_CGROUP"
print_kv "cgroup.type" "$(cat "$DELEGATED_CGROUP/cgroup.type" 2>/dev/null || echo "N/A")"
print_kv "controllers" "$(tr '\n' ' ' < "$DELEGATED_CGROUP/cgroup.controllers" 2>/dev/null || echo "N/A")"
print_kv "subtree_control" "$(tr '\n' ' ' < "$DELEGATED_CGROUP/cgroup.subtree_control" 2>/dev/null || echo "N/A")"
print_kv "memory.max" "$(cat "$DELEGATED_CGROUP/memory.max" 2>/dev/null || echo "N/A")"
print_kv "memory.swap.max" "$(cat "$DELEGATED_CGROUP/memory.swap.max" 2>/dev/null || echo "N/A")"
print_kv "cgroup.procs writable" "$([ -w "$DELEGATED_CGROUP/cgroup.procs" ] && echo "yes" || echo "no")"
print_kv "subtree_control writable" "$([ -w "$DELEGATED_CGROUP/cgroup.subtree_control" ] && echo "yes" || echo "no")"

# Read-only mkdir test (create then immediately remove)
TESTDIR="${DELEGATED_CGROUP}/cppjudge_probe_ro_$$"
if mkdir "$TESTDIR" 2>/dev/null; then
    print_kv "mkdir child" "OK"
    # Check if child inherits controllers
    if [ -f "$TESTDIR/cgroup.controllers" ]; then
        print_kv "child controllers" "$(tr '\n' ' ' < "$TESTDIR/cgroup.controllers")"
    fi
    if [ -f "$TESTDIR/memory.events.local" ]; then
        print_kv "child has .local" "yes"
    else
        print_kv "child has .local" "no (will use memory.events)"
    fi
    rmdir "$TESTDIR"
else
    print_kv "mkdir child" "FAILED"
fi

# ─── Lifecycle barrier ──────────────────────────────────────
if ! $LIFECYCLE_MODE; then
    echo ""
    echo "Read-only probe complete. Use --lifecycle for full OOM validation."
    exit 0
fi

if [ -z "$DELEGATED_CGROUP" ] || [ ! -d "$DELEGATED_CGROUP" ]; then
    echo "ERROR: No delegated cgroup found. Cannot run lifecycle tests."
    exit 1
fi

# Build the C helper
build_barrier_helper
if [ ! -x "$PROBE_TMP/barrier_helper" ]; then
    echo "FATAL: failed to build barrier_helper"
    exit 1
fi

# ─── Single test lifecycle ──────────────────────────────────
run_lifecycle_test() {
    local test_name="$1"
    local helper_mode="$2"
    local expect_oom="${3:-0}"       # 0 = expect no oom_kill, 1 = expect oom_kill

    local unique_id="${$}_${RANDOM}"
    PROBE_CG="${DELEGATED_CGROUP}/cppjudge_probe_${unique_id}"

    print_section "Lifecycle: $test_name"

    # Step 1: create
    if ! mkdir "$PROBE_CG" 2>/dev/null; then
        echo "  FAIL: mkdir $PROBE_CG"
        PROBE_CG=""
        return 1
    fi
    echo "  created: $PROBE_CG"

    # Step 2: set limits
    echo 33554432 > "$PROBE_CG/memory.max" 2>/dev/null || { echo "  FAIL: write memory.max"; return 1; }
    echo 0 > "$PROBE_CG/memory.swap.max" 2>/dev/null || { echo "  FAIL: write memory.swap.max"; return 1; }
    echo 8 > "$PROBE_CG/pids.max" 2>/dev/null || { echo "  FAIL: write pids.max"; return 1; }
    echo "  limits: memory.max=32M swap.max=0 pids.max=8"

    # Step 3: read baseline from the EXACT probe cgroup (prefer .local)
    local events_file
    if [ -f "$PROBE_CG/memory.events.local" ]; then
        events_file="$PROBE_CG/memory.events.local"
        echo "  source: memory.events.local"
    else
        events_file="$PROBE_CG/memory.events"
        echo "  source: memory.events (no .local available)"
    fi

    local oom_kill_before oom_before max_before
    oom_kill_before=$(awk '/^oom_kill /{print $2}' "$events_file" 2>/dev/null || echo "0")
    oom_before=$(awk '/^oom /{print $2}' "$events_file" 2>/dev/null || echo "0")
    max_before=$(awk '/^max /{print $2}' "$events_file" 2>/dev/null || echo "0")
    echo "  baseline: oom_kill=${oom_kill_before} oom=${oom_before} max=${max_before}"

    # Step 4: start child with barrier
    local barrier_pipe="$PROBE_TMP/pipe_${unique_id}"
    mkfifo "$barrier_pipe"

    # Child: waits for parent to add it to cgroup, then runs the helper
    (
        read < "$barrier_pipe"
        exec "$PROBE_TMP/barrier_helper" "$helper_mode" ""
    ) &
    local child_pid=$!
    echo "  child PID: $child_pid"

    # Step 5: parent writes child PID to cgroup.procs
    local join_ok=true
    if ! echo "$child_pid" > "$PROBE_CG/cgroup.procs" 2>/dev/null; then
        echo "  cgroup.procs write: FAILED (SELinux container_manage_cgroup=$SELINUX_CGROUP_BOOL)"
        join_ok=false
    else
        echo "  cgroup.procs write: OK"
    fi

    # Step 6: verify
    if $join_ok; then
        if grep -q "$(echo "$PROBE_CG" | sed 's|^/sys/fs/cgroup||')" "/proc/${child_pid}/cgroup" 2>/dev/null; then
            echo "  verify in cgroup: OK"
        else
            echo "  verify in cgroup: FAILED"
            join_ok=false
        fi
    fi

    # Step 7: release child
    echo "go" > "$barrier_pipe"
    rm -f "$barrier_pipe"

    # Step 8: wait for child
    local child_status=0
    set +e
    wait $child_pid 2>/dev/null
    child_status=$?
    set -e

    echo "  child: raw_wait_status=$child_status"

    # Step 9: read after events from same file
    local oom_kill_after oom_after max_after peak
    oom_kill_after=$(awk '/^oom_kill /{print $2}' "$events_file" 2>/dev/null || echo "0")
    oom_after=$(awk '/^oom /{print $2}' "$events_file" 2>/dev/null || echo "0")
    max_after=$(awk '/^max /{print $2}' "$events_file" 2>/dev/null || echo "0")
    peak=$(cat "$PROBE_CG/memory.peak" 2>/dev/null || echo "N/A")
    echo "  after: oom_kill=${oom_kill_after} oom=${oom_after} max=${max_after}"

    # Step 10: compute delta
    local oom_kill_delta=$((oom_kill_after - oom_kill_before))
    local oom_delta=$((oom_after - oom_before))
    local max_delta=$((max_after - max_before))
    echo "  delta: oom_kill=${oom_kill_delta} oom=${oom_delta} max=${max_delta}"
    echo "  peak=${peak}"

    # Step 11: verdict
    echo ""
    if [ "$expect_oom" -eq 1 ]; then
        if [ "$oom_kill_delta" -ge 1 ]; then
            echo "  VERDICT: MLE (oom_kill delta=$oom_kill_delta, cgroup events confirm OOM)"
        elif [ "$join_ok" = false ]; then
            echo "  VERDICT: BLOCKED (cgroup.procs join failed - SELinux)"
        else
            echo "  VERDICT: UNEXPECTED (expected oom_kill>=1, got $oom_kill_delta)"
        fi
    else
        if [ "$oom_kill_delta" -eq 0 ]; then
            echo "  VERDICT: OK (no oom_kill, as expected)"
        else
            echo "  VERDICT: FAIL (unexpected oom_kill delta=$oom_kill_delta)"
        fi
    fi

    # Step 12: clean up only this probe cgroup
    cleanup_probe_cgroup
}

# ─── Run tests ──────────────────────────────────────────────

run_lifecycle_test "OOM (32M limit, 48M alloc)" "oom" 1

# Only run controls if OOM test's cgroup.procs join succeeded
if [ -x "$PROBE_TMP/barrier_helper" ]; then
    run_lifecycle_test "exit(42)" "exit42" 0
    run_lifecycle_test "self SIGKILL" "selfkill" 0
    run_lifecycle_test "exit(1)" "exit1" 0
    run_lifecycle_test "exit(0)" "exit0" 0
    run_lifecycle_test "stderr scary text" "stderr_scary" 0
fi

print_section "Residual check"
if ls -d "$DELEGATED_CGROUP"/cppjudge_probe_* 2>/dev/null; then
    echo "WARNING: residual probe cgroups found (manual cleanup may be needed)"
else
    echo "CLEAN - no residual probe cgroups"
fi

echo ""
echo "Lifecycle probe complete."
