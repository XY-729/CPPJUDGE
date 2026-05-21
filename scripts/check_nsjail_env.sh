#!/usr/bin/env bash
set -u

print_section() {
    printf '\n===== %s =====\n' "$1"
}

print_kv() {
    printf '%-32s %s\n' "$1:" "$2"
}

has_nsjail=0
cgroup_v2_detected=0
cgroup_write_permission="no"
found_flag_count=0
required_flag_count=5
cgroup2_mount_point=""

print_section "System"
print_kv "uname" "$(uname -a 2>/dev/null || echo unavailable)"
print_kv "user" "$(id 2>/dev/null || echo unavailable)"
print_kv "cwd" "$(pwd 2>/dev/null || echo unavailable)"

print_section "nsjail"
nsjail_path="$(command -v nsjail 2>/dev/null || true)"
if [[ -n "$nsjail_path" ]]; then
    has_nsjail=1
    print_kv "nsjail" "found: $nsjail_path"

    version_output="$(nsjail --version 2>&1 || true)"
    if [[ -n "$version_output" ]] && ! grep -qiE 'unknown option|invalid option|usage' <<<"$version_output"; then
        print_kv "nsjail --version" "$version_output"
    else
        print_kv "nsjail --version" "not supported or unavailable"
        echo "nsjail --help preview:"
        nsjail --help 2>&1 | head -8 || true
    fi
else
    print_kv "nsjail" "not found"
fi

print_section "nsjail cgroup flags"
nsjail_help=""
if [[ "$has_nsjail" -eq 1 ]]; then
    nsjail_help="$(nsjail --help 2>&1 || nsjail -h 2>&1 || true)"
fi

check_flag() {
    local flag="$1"
    if [[ -n "$nsjail_help" ]] && grep -Fq -- "$flag" <<<"$nsjail_help"; then
        print_kv "$flag" "available"
        found_flag_count=$((found_flag_count + 1))
    else
        print_kv "$flag" "missing"
    fi
}

check_flag "--use_cgroupv2"
check_flag "--cgroupv2_mount"
check_flag "--cgroup_mem_max"
check_flag "--cgroup_pids_max"
check_flag "--cgroup_cpu_ms_per_sec"

print_section "/sys/fs/cgroup"
if [[ -d /sys/fs/cgroup ]]; then
    print_kv "/sys/fs/cgroup" "exists"
else
    print_kv "/sys/fs/cgroup" "missing"
fi

if [[ -f /sys/fs/cgroup/cgroup.controllers ]]; then
    cgroup_v2_detected=1
    print_kv "cgroup.controllers" "exists"
    printf 'controllers: '
    cat /sys/fs/cgroup/cgroup.controllers 2>/dev/null || true
    printf '\n'
else
    print_kv "cgroup.controllers" "missing"
fi

if [[ -f /sys/fs/cgroup/cgroup.subtree_control ]]; then
    print_kv "cgroup.subtree_control" "exists"
    printf 'subtree_control: '
    cat /sys/fs/cgroup/cgroup.subtree_control 2>/dev/null || true
    printf '\n'
else
    print_kv "cgroup.subtree_control" "missing"
fi

print_section "Mount info"
if [[ -r /proc/self/mountinfo ]]; then
    cgroup2_line="$(awk '$0 ~ / - cgroup2 / {print; exit}' /proc/self/mountinfo 2>/dev/null || true)"
    if [[ -n "$cgroup2_line" ]]; then
        cgroup_v2_detected=1
        cgroup2_mount_point="$(awk '$0 ~ / - cgroup2 / {print $5; exit}' /proc/self/mountinfo 2>/dev/null || true)"
        print_kv "cgroup2 mount" "detected"
        print_kv "cgroup2 mount point" "${cgroup2_mount_point:-unknown}"
        echo "$cgroup2_line"
    else
        print_kv "cgroup2 mount" "not detected"
    fi
else
    print_kv "/proc/self/mountinfo" "not readable"
    mount | grep cgroup2 || true
fi

print_section "Current process cgroup"
if [[ -r /proc/self/cgroup ]]; then
    cat /proc/self/cgroup
else
    print_kv "/proc/self/cgroup" "not readable"
fi

print_section "cgroup write permission"
if [[ -d /sys/fs/cgroup ]]; then
    tmp_cgroup="/sys/fs/cgroup/cppjudge_precheck_$$"
    mkdir_error="$(mkdir "$tmp_cgroup" 2>&1 || true)"
    if [[ -d "$tmp_cgroup" ]]; then
        cgroup_write_permission="yes"
        print_kv "create child cgroup" "success: $tmp_cgroup"
        rmdir "$tmp_cgroup" 2>/dev/null || true
    else
        print_kv "create child cgroup" "failed: ${mkdir_error:-permission denied or read-only}"
    fi
else
    print_kv "create child cgroup" "skipped: /sys/fs/cgroup missing"
fi

print_section "Manual nsjail tests"
if [[ -f scripts/run_nsjail_tests.sh ]]; then
    print_kv "manual test" "bash scripts/run_nsjail_tests.sh"
else
    print_kv "manual test" "scripts/run_nsjail_tests.sh not found"
fi

if [[ -f scripts/preview_nsjail_cgroup_args.sh ]]; then
    print_kv "cgroup dry-run" "bash scripts/preview_nsjail_cgroup_args.sh 128 16 0"
else
    print_kv "cgroup dry-run" "scripts/preview_nsjail_cgroup_args.sh not found"
fi

print_section "CPPJUDGE nsjail environment summary"
if [[ "$has_nsjail" -eq 1 ]]; then
    print_kv "nsjail" "found"
else
    print_kv "nsjail" "not found"
fi

if [[ "$cgroup_v2_detected" -eq 1 ]]; then
    print_kv "cgroup v2" "detected"
else
    print_kv "cgroup v2" "not detected"
fi

print_kv "cgroup write permission" "$cgroup_write_permission"

if [[ "$found_flag_count" -eq "$required_flag_count" ]]; then
    print_kv "nsjail cgroup flags" "available"
elif [[ "$found_flag_count" -gt 0 ]]; then
    print_kv "nsjail cgroup flags" "partial ($found_flag_count/$required_flag_count)"
else
    print_kv "nsjail cgroup flags" "missing"
fi

printf '\nrecommendation:\n'
if [[ "$has_nsjail" -eq 1 && "$cgroup_v2_detected" -eq 1 && "$found_flag_count" -eq "$required_flag_count" && "$cgroup_write_permission" == "yes" ]]; then
    echo "  - cgroup v2 integration can be tested on this machine."
else
    echo "  - keep the current rlimit fallback for now."
    echo "  - enable cgroup v2 runner integration only after nsjail flags, mount, and write permissions are available."
fi

exit 0
