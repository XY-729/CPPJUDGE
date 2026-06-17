#!/usr/bin/env bash
set -euo pipefail

# probe_cgroup_v2.sh — read-only cgroup v2 preflight check
# Stage 3B: stripped of 3A lifecycle experiments and SELinux guesses.
# Use --verbose for full discovery output.
# All operations are read-only; no cgroups are created or modified.

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

print_section() { printf '\n===== %s =====\n' "$1"; }
print_kv() { printf '%-40s %s\n' "$1:" "$2"; }

VERBOSE=false
if [ "${1:-}" = "--verbose" ]; then
    VERBOSE=true
fi

# ─── System identity ────────────────────────────────────────
print_section "System identity"
print_kv "kernel" "$(uname -r)"
print_kv "uid" "$(id -u) ($(id -un))"

# ─── cgroup v2 detection ────────────────────────────────────
print_section "cgroup v2 detection"
CGROUP2_FS=$(stat -fc %T /sys/fs/cgroup 2>/dev/null || echo "unknown")
print_kv "fs type" "$CGROUP2_FS"
if [ "$CGROUP2_FS" != "cgroup2fs" ]; then
    echo "ERROR: /sys/fs/cgroup is not cgroup2 ($CGROUP2_FS)"
    echo "CPPJUDGE requires unified cgroup v2 hierarchy"
    exit 1
fi

CGROUP2_MOUNT=$(findmnt -n -o TARGET -t cgroup2 2>/dev/null || echo "/sys/fs/cgroup")
print_kv "cgroup2 mount" "$CGROUP2_MOUNT"

# ─── Current process cgroup ─────────────────────────────────
CURRENT_CGROUP=$(awk -F: '$2 == "" {print $3; exit}' /proc/self/cgroup 2>/dev/null || echo "")
if [ -z "$CURRENT_CGROUP" ]; then
    echo "ERROR: cannot determine current cgroup from /proc/self/cgroup"
    exit 1
fi
print_kv "self cgroup" "$CURRENT_CGROUP"

# ─── systemd delegation check ────────────────────────────────
print_section "systemd delegation"
if ! command -v systemctl >/dev/null 2>&1; then
    echo "ERROR: systemctl not available"
    exit 1
fi

UID_NUM=$(id -u)
SERVICE_CG=$(systemctl show "user@${UID_NUM}.service" \
    --property=ControlGroup --no-pager 2>/dev/null | cut -d= -f2 || echo "")
DELEGATE_VAL=$(systemctl show "user@${UID_NUM}.service" \
    --property=Delegate --no-pager 2>/dev/null | cut -d= -f2 || echo "")
SLICE_VAL=$(systemctl show "user@${UID_NUM}.service" \
    --property=Slice --no-pager 2>/dev/null | cut -d= -f2 || echo "")

print_kv "ControlGroup" "$SERVICE_CG"
print_kv "Delegate" "$DELEGATE_VAL"
print_kv "Slice" "$SLICE_VAL"

# ─── Delegated cgroup filesystem ─────────────────────────────
print_section "Delegated cgroup filesystem"
DELEGATED_ROOT=""
if [ -n "$SERVICE_CG" ] && [ -d "${CGROUP2_MOUNT}${SERVICE_CG}" ]; then
    DELEGATED_ROOT="${CGROUP2_MOUNT}${SERVICE_CG}"
fi

if [ -z "$DELEGATED_ROOT" ]; then
    echo "FATAL: no delegated cgroup found at ${CGROUP2_MOUNT}${SERVICE_CG}"
    echo ""
    echo "CPPJUDGE nsjail production mode requires a delegated systemd service."
    echo "See deploy/cppjudge.service.example for the required unit configuration."
    echo "The service must include: Delegate=memory pids"
    exit 2
fi

print_kv "delegated root" "$DELEGATED_ROOT"
print_kv "cgroup.type" "$(cat "$DELEGATED_ROOT/cgroup.type" 2>/dev/null || echo "N/A")"

CTRLS=$(tr '\n' ' ' < "$DELEGATED_ROOT/cgroup.controllers" 2>/dev/null || echo "N/A")
print_kv "available controllers" "$CTRLS"

SUB_CTRL=$(tr '\n' ' ' < "$DELEGATED_ROOT/cgroup.subtree_control" 2>/dev/null || echo "(empty)")
print_kv "subtree_control" "$SUB_CTRL"

print_kv "memory.max" "$(cat "$DELEGATED_ROOT/memory.max" 2>/dev/null || echo "N/A")"
print_kv "memory.swap.max" "$(cat "$DELEGATED_ROOT/memory.swap.max" 2>/dev/null || echo "N/A")"
print_kv "cgroup.procs writable" "$([ -w "$DELEGATED_ROOT/cgroup.procs" ] && echo "yes" || echo "no")"
print_kv "subtree_control writable" "$([ -w "$DELEGATED_ROOT/cgroup.subtree_control" ] && echo "yes" || echo "no")"

# ─── Controller availability ─────────────────────────────────
print_section "Required controllers"
HAS_MEMORY=false
HAS_PIDS=false
for ctrl in $CTRLS; do
    [ "$ctrl" = "memory" ] && HAS_MEMORY=true
    [ "$ctrl" = "pids" ] && HAS_PIDS=true
done

print_kv "memory" "$($HAS_MEMORY && echo 'yes' || echo 'MISSING')"
print_kv "pids" "$($HAS_PIDS && echo 'yes' || echo 'MISSING')"

# ─── Local events support ────────────────────────────────────
print_section "Event source"

# Quick read-only test: mkdir+rmdir in tmp to check .local availability
TMPDIR="${DELEGATED_ROOT}/.cppjudge_preflight_$$"
HAS_LOCAL=false
if mkdir "$TMPDIR" 2>/dev/null; then
    if [ -f "$TMPDIR/memory.events.local" ]; then
        HAS_LOCAL=true
        print_kv "memory.events.local" "available"
    else
        print_kv "memory.events.local" "not available (will use memory.events)"
    fi
    if [ -f "$TMPDIR/cgroup.kill" ]; then
        print_kv "cgroup.kill" "available"
    else
        print_kv "cgroup.kill" "MISSING - cleanup will use fallback"
    fi
    if [ -f "$TMPDIR/memory.oom.group" ]; then
        print_kv "memory.oom.group" "available"
    else
        print_kv "memory.oom.group" "MISSING - oom group kill not supported"
    fi
    rmdir "$TMPDIR"
else
    print_kv "mkdir test" "FAILED - cannot create child cgroups"
fi

# ─── Delegation summary ──────────────────────────────────────
print_section "Delegation summary"
DELEGATION_OK=true

if [ "$DELEGATE_VAL" != "yes" ]; then
    echo "FAIL: Delegate= is '$DELEGATE_VAL', expected 'yes'"
    DELEGATION_OK=false
fi

if ! $HAS_MEMORY; then
    echo "FAIL: memory controller not available"
    DELEGATION_OK=false
fi

if ! $HAS_PIDS; then
    echo "FAIL: pids controller not available"
    DELEGATION_OK=false
fi

if ! $DELEGATION_OK; then
    echo ""
    echo "RESULT: cgroup delegation is NOT ready for CPPJUDGE production use."
    exit 2
fi

echo ""
echo "RESULT: cgroup delegation ready for CPPJUDGE."
echo "  Event source: $($HAS_LOCAL && echo 'memory.events.local' || echo 'memory.events')"
echo ""
echo "Preflight complete. All checks passed."
