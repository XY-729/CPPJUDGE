#!/usr/bin/env bash
set -u

memory_limit_mb="${1:-128}"
pids_limit="${2:-16}"
cpu_ms_per_sec="${3:-0}"
cgroupv2_mount="${CGROUPV2_MOUNT:-/sys/fs/cgroup}"

is_positive_int() {
    [[ "$1" =~ ^[1-9][0-9]*$ ]]
}

is_nonnegative_int() {
    [[ "$1" =~ ^[0-9]+$ ]]
}

print_kv() {
    printf '%-28s %s\n' "$1:" "$2"
}

if ! is_positive_int "$memory_limit_mb"; then
    echo "Invalid memory_limit_mb: $memory_limit_mb, expected positive integer" >&2
    exit 2
fi

if ! is_positive_int "$pids_limit"; then
    echo "Invalid pids_limit: $pids_limit, expected positive integer" >&2
    exit 2
fi

if ! is_nonnegative_int "$cpu_ms_per_sec"; then
    echo "Invalid cpu_ms_per_sec: $cpu_ms_per_sec, expected non-negative integer" >&2
    exit 2
fi

memory_limit_bytes=$((memory_limit_mb * 1024 * 1024))

nsjail_help=""
if command -v nsjail >/dev/null 2>&1; then
    nsjail_help="$(nsjail --help 2>&1 || nsjail -h 2>&1 || true)"
fi

has_flag() {
    local flag="$1"
    [[ -n "$nsjail_help" ]] && grep -Fq -- "$flag" <<<"$nsjail_help"
}

missing_flags=()
for flag in --use_cgroupv2 --cgroupv2_mount --cgroup_mem_max --cgroup_pids_max --cgroup_cpu_ms_per_sec; do
    if ! has_flag "$flag"; then
        missing_flags+=("$flag")
    fi
done

cgroup_v2="no"
if [[ -f "$cgroupv2_mount/cgroup.controllers" ]]; then
    cgroup_v2="yes"
fi

cgroup_write="no"
if [[ -d "$cgroupv2_mount" ]]; then
    tmp_cgroup="$cgroupv2_mount/cppjudge_dryrun_$$"
    if mkdir "$tmp_cgroup" 2>/dev/null; then
        cgroup_write="yes"
        rmdir "$tmp_cgroup" 2>/dev/null || true
    fi
fi

args=(
    --use_cgroupv2
    --cgroupv2_mount "$cgroupv2_mount"
    --cgroup_mem_max "$memory_limit_bytes"
    --cgroup_pids_max "$pids_limit"
)

if [[ "$cpu_ms_per_sec" -gt 0 ]]; then
    args+=(--cgroup_cpu_ms_per_sec "$cpu_ms_per_sec")
fi

printf '===== CPPJUDGE nsjail cgroup v2 dry-run args =====\n'
print_kv "memory_limit_mb" "$memory_limit_mb"
print_kv "memory_limit_bytes" "$memory_limit_bytes"
print_kv "pids_limit" "$pids_limit"
print_kv "cpu_ms_per_sec" "$cpu_ms_per_sec"
print_kv "cgroupv2_mount" "$cgroupv2_mount"
print_kv "cgroup v2 detected" "$cgroup_v2"
print_kv "cgroup write permission" "$cgroup_write"

if [[ "${#missing_flags[@]}" -eq 0 ]]; then
    print_kv "nsjail cgroup flags" "available"
else
    print_kv "nsjail cgroup flags" "missing: ${missing_flags[*]}"
fi

printf '\nFuture nsjail cgroup arguments:\n'
printf '  %q' "${args[@]}"
printf '\n'

printf '\nNotes:\n'
echo '  - This script only previews future arguments; it does not execute nsjail.'
echo '  - CPPJUDGE runner behavior is unchanged by this script.'
echo '  - Keep rlimit fallback unless nsjail flags, cgroup v2 mount, and permissions are ready.'

if [[ "$cgroup_v2" != "yes" || "$cgroup_write" != "yes" || "${#missing_flags[@]}" -ne 0 ]]; then
    printf '\nRecommendation:\n'
    echo '  - Do not enable cgroup v2 runner arguments yet on this machine.'
fi

exit 0
