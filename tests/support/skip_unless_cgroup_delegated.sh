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

    # Verify controllers are available in the service root (parent cgroup).
    # Child cgroup controllers are only inherited after subtree_control is enabled
    # (done by CPPJUDGE init_service), not needed for delegation detection.
    local ctrls=""
    local ok=true
    local sr_ctrl="/sys/fs/cgroup${cg}/cgroup.controllers"
    if [[ ! -r "$sr_ctrl" ]]; then
        ok=false
    else
        ctrls="$(tr '\n' ' ' <"$sr_ctrl" 2>/dev/null)" || ok=false
        [[ " $ctrls " == *" memory "* ]] || ok=false
        [[ " $ctrls " == *" pids "* ]] || ok=false
    fi
    rmdir "$test_dir" 2>/dev/null || true

    if ! $ok; then
        echo "SKIP: controllers missing in service root cgroup (have: ${ctrls:-none}, need: memory pids)"
        exit 77
    fi
    return 0
}
