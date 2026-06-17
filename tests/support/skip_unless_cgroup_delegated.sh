#!/usr/bin/env bash
# skip_unless_cgroup_delegated.sh
# Source this file, then call: skip_unless_cgroup_delegated
# Exits 77 if not running inside a delegated cgroup with memory+pids controllers.
# Returns 0 (no-op) if delegation is available.

skip_unless_cgroup_delegated() {
    local cg test_dir
    cg=$(grep '^0::' /proc/self/cgroup 2>/dev/null | head -1 | sed 's/^0:://')
    if [ -z "$cg" ]; then
        echo "SKIP: cannot determine cgroup from /proc/self/cgroup"
        exit 77
    fi

    test_dir="/sys/fs/cgroup${cg}/.cppjudge_ns_del_$$"
    if ! mkdir "$test_dir" 2>/dev/null; then
        echo "SKIP: cgroup delegation not available (nsjail tests require Delegate=memory pids)"
        exit 77
    fi

    # Verify controllers are available
    local ctrls ok
    ok=true
    if [ -f "$test_dir/cgroup.controllers" ]; then
        ctrls=$(tr '\n' ' ' < "$test_dir/cgroup.controllers" 2>/dev/null)
        if ! echo "$ctrls" | grep -q "memory"; then ok=false; fi
        if ! echo "$ctrls" | grep -q "pids"; then ok=false; fi
    fi
    rmdir "$test_dir" 2>/dev/null || true

    if ! $ok; then
        echo "SKIP: controllers missing in delegated cgroup (have: ${ctrls:-none}, need: memory pids)"
        exit 77
    fi
    return 0
}
