#pragma once

#include <atomic>
#include <cstdint>
#include <string>

// ── Error codes ─────────────────────────────────────────────

enum class CgroupV2ErrorCode {
    None = 0,
    NotUnifiedV2,
    MountNotFound,
    CurrentPathNotFound,
    DelegationMissing,
    ControllerMissing,
    RootPopulated,
    CreateFailed,
    LimitWriteFailed,
    ProcessJoinFailed,
    VerificationFailed,
    EventReadFailed,
    KillFailed,
    CleanupFailed
};

struct CgroupV2Error {
    CgroupV2ErrorCode code = CgroupV2ErrorCode::None;
    std::string message;
    std::string path;
    int saved_errno = 0;

    bool ok() const { return code == CgroupV2ErrorCode::None; }
};

// ── Stats ───────────────────────────────────────────────────

struct CgroupV2Stats {
    std::uint64_t oom = 0;
    std::uint64_t oom_kill = 0;
    std::uint64_t oom_group_kill = 0;
    std::uint64_t max_events = 0;
    std::uint64_t memory_peak_bytes = 0;
};

// ── Limits ──────────────────────────────────────────────────

struct CgroupV2Limits {
    std::uint64_t memory_max_bytes = 0;
    std::uint64_t memory_swap_max_bytes = 0;
    std::uint64_t pids_max = 0;
};

// ── Discovered paths ────────────────────────────────────────

struct CgroupV2Paths {
    std::string mount;          // e.g. /sys/fs/cgroup
    std::string self_relative;  // e.g. /user.slice/user-1000.slice/...
    std::string service_root;   // full path: mount + self_relative
};

// ── Event snapshot for delta calculation ────────────────────

struct CgroupV2EventSnapshot {
    std::uint64_t oom = 0;
    std::uint64_t oom_kill = 0;
    std::uint64_t oom_group_kill = 0;
    std::uint64_t max_events = 0;
};

inline CgroupV2EventSnapshot make_snapshot(const CgroupV2Stats& s) {
    return {s.oom, s.oom_kill, s.oom_group_kill, s.max_events};
}

inline CgroupV2EventSnapshot delta(const CgroupV2EventSnapshot& after,
                                    const CgroupV2EventSnapshot& before) {
    return {
        after.oom > before.oom ? after.oom - before.oom : 0,
        after.oom_kill > before.oom_kill ? after.oom_kill - before.oom_kill : 0,
        after.oom_group_kill > before.oom_group_kill ? after.oom_group_kill - before.oom_group_kill : 0,
        after.max_events > before.max_events ? after.max_events - before.max_events : 0
    };
}

// ── Error helpers ───────────────────────────────────────────

const char* cgroup_v2_error_code_string(CgroupV2ErrorCode code);

// Build a human-readable error message with phase / code / path / errno.
std::string cgroup_v2_error_message(const CgroupV2Error& err,
                                     const char* phase);

// ── Manager ─────────────────────────────────────────────────

class CgroupV2Manager {
public:
    CgroupV2Manager();
    ~CgroupV2Manager();

    // Not copyable or movable — owns cgroup state.
    CgroupV2Manager(const CgroupV2Manager&) = delete;
    CgroupV2Manager& operator=(const CgroupV2Manager&) = delete;

    // ── Discovery (static, read-only, no side effects) ──────

    // Parse /proc/self/mountinfo and /proc/self/cgroup to find
    // the cgroup2 mount point and our own cgroup path.
    // On failure returns error with code set.
    static CgroupV2Paths discover(CgroupV2Error& error);

    // ── Service initialization ──────────────────────────────

    // Initialize the cgroup hierarchy under service_root:
    //   1. Create manager_<pid> child cgroup
    //   2. Move own PID into it
    //   3. Verify via /proc/self/cgroup
    //   4. Check service_root cgroup.procs is empty (or only us)
    //   5. Enable +memory +pids in subtree_control
    //   6. Verify controllers are enabled
    //
    // After success, the service is ready for per-run cgroups.
    bool init_service(CgroupV2Error& error);

    // ── Per-run cgroup lifecycle ─────────────────────────────

    // Create a uniquely-named child of the manager cgroup.
    // Sets memory.max, memory.swap.max, pids.max, memory.oom.group.
    // Returns false if creation or limit writes fail.
    bool create_run(const CgroupV2Limits& limits, CgroupV2Error& error);

    // Join a process to the current run cgroup.
    // Writes pid to run/cgroup.procs, then verifies /proc/<pid>/cgroup.
    bool join_process(pid_t pid, CgroupV2Error& error);

    // Read memory events and peak from the current run cgroup.
    // Prefers memory.events.local; falls back to memory.events.
    bool read_stats(CgroupV2Stats& stats, CgroupV2Error& error);

    // Kill all processes in the current run cgroup.
    // Uses cgroup.kill if available; otherwise iterates cgroup.procs.
    bool kill_run(CgroupV2Error& error);

    // Wait for cgroup.events populated=0 with timeout.
    bool wait_empty(int timeout_sec, CgroupV2Error& error);

    // Clean up the current run cgroup:
    //   1. Read final events
    //   2. If populated, write cgroup.kill
    //   3. Wait for populated=0
    //   4. rmdir
    //   5. Verify directory is gone
    bool cleanup_run(CgroupV2Error& error);

    // ── Accessors ────────────────────────────────────────────

    bool is_initialized() const { return initialized_; }
    const std::string& service_root() const { return service_root_; }
    const std::string& manager_path() const { return manager_path_; }
    const std::string& run_path() const { return run_path_; }
    const std::string& run_name() const { return run_name_; }
    bool has_local_events() const { return has_local_events_; }

    // ── Test support ─────────────────────────────────────────

    // Override the cgroup filesystem root for testing.
    // Must be set before init_service().  The override is validated
    // for path containment (no escapes, no symlink following).
    // Pass empty string to clear.
    static void set_test_root(const std::string& path);
    static std::string test_root();

private:
    bool initialized_ = false;
    bool has_local_events_ = false;

    std::string mount_;          // cgroup2 mount point
    std::string service_root_;   // delegated service root
    std::string manager_path_;   // manager_<pid> cgroup
    std::string run_path_;       // current run cgroup
    std::string run_name_;       // short name of current run
    pid_t own_pid_ = 0;

    static std::atomic<bool> s_test_root_set_;
    static std::string s_test_root_;

    // ── Internal helpers ─────────────────────────────────────

    std::string cgroup_fs_path(const std::string& rel) const;

    bool write_cgroup_file(const std::string& cg_dir,
                           const std::string& filename,
                           const std::string& value,
                           CgroupV2Error& error,
                           const char* op_name);

    bool read_cgroup_file(const std::string& cg_dir,
                          const std::string& filename,
                          std::string& out,
                          CgroupV2Error& error);

    bool enable_controllers(const std::string& cg_dir,
                            const std::string& controllers,
                            CgroupV2Error& error);

    bool parse_events(const std::string& text, CgroupV2Stats& stats,
                      CgroupV2Error& error);

    bool parse_uint64(const char* start, const char* end,
                      std::uint64_t& out);

    void cleanup_run_impl();

public:
    // Public for testing — path safety validation
    static bool validate_no_escape(const std::string& path);
    static std::string unescape_cgroup_path(const std::string& raw);
};
