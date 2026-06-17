#include "cgroup_v2.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

std::atomic<bool> CgroupV2Manager::s_test_root_set_{false};
std::string CgroupV2Manager::s_test_root_;

namespace {

static constexpr int CGROUP_OPS_TIMEOUT_SEC = 5;

bool is_safe_cgroup_char(char ch) {
    return (ch >= 'a' && ch <= 'z') ||
           (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') ||
           ch == '_' || ch == '-' || ch == '.' ||
           ch == '/' || ch == '@';
}

} // namespace

const char* cgroup_v2_error_code_string(CgroupV2ErrorCode code) {
    switch (code) {
        case CgroupV2ErrorCode::None:                return "NONE";
        case CgroupV2ErrorCode::NotUnifiedV2:        return "NOT_UNIFIED_V2";
        case CgroupV2ErrorCode::MountNotFound:       return "MOUNT_NOT_FOUND";
        case CgroupV2ErrorCode::CurrentPathNotFound: return "CURRENT_PATH_NOT_FOUND";
        case CgroupV2ErrorCode::DelegationMissing:   return "DELEGATION_MISSING";
        case CgroupV2ErrorCode::ControllerMissing:   return "CONTROLLER_MISSING";
        case CgroupV2ErrorCode::RootPopulated:       return "ROOT_POPULATED";
        case CgroupV2ErrorCode::CreateFailed:         return "CREATE_FAILED";
        case CgroupV2ErrorCode::LimitWriteFailed:     return "LIMIT_WRITE_FAILED";
        case CgroupV2ErrorCode::ProcessJoinFailed:    return "PROCESS_JOIN_FAILED";
        case CgroupV2ErrorCode::VerificationFailed:   return "VERIFICATION_FAILED";
        case CgroupV2ErrorCode::EventReadFailed:      return "EVENT_READ_FAILED";
        case CgroupV2ErrorCode::KillFailed:           return "KILL_FAILED";
        case CgroupV2ErrorCode::CleanupFailed:        return "CLEANUP_FAILED";
        default:                                     return "UNKNOWN";
    }
}

std::string cgroup_v2_error_message(const CgroupV2Error& err,
                                     const char* phase) {
    std::ostringstream oss;
    oss << "cgroup v2 error in phase '" << phase << "': "
        << "code=" << cgroup_v2_error_code_string(err.code);
    if (!err.message.empty()) oss << " detail=" << err.message;
    if (!err.path.empty()) oss << " path=" << err.path;
    if (err.saved_errno != 0)
        oss << " errno=" << err.saved_errno << " (" << strerror(err.saved_errno) << ")";
    return oss.str();
}

bool CgroupV2Manager::validate_no_escape(const std::string& path) {
    if (path.empty()) return false;
    if (path.find("..") != std::string::npos) return false;
    if (path.find("//") != std::string::npos) return false;
    for (char ch : path) {
        if (!is_safe_cgroup_char(ch)) return false;
    }
    return true;
}

std::string CgroupV2Manager::unescape_cgroup_path(const std::string& raw) {
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\\') return "";
        if (raw[i] == '\n' || raw[i] == '\0') return "";
    }
    return raw;
}

void CgroupV2Manager::set_test_root(const std::string& path) {
    if (path.empty()) { s_test_root_ = ""; s_test_root_set_ = false; return; }
    s_test_root_ = path;
    s_test_root_set_ = true;
}

std::string CgroupV2Manager::test_root() { return s_test_root_; }

CgroupV2Manager::CgroupV2Manager() : own_pid_(getpid()) {}

CgroupV2Manager::~CgroupV2Manager() {
    if (!run_path_.empty()) {
        CgroupV2Error ignored;
        cleanup_run(ignored);
    }
}

std::string CgroupV2Manager::cgroup_fs_path(const std::string& rel) const {
    if (s_test_root_set_ && !s_test_root_.empty()) return s_test_root_ + rel;
    return rel;
}

// ── Discovery ────────────────────────────────────────────────

CgroupV2Paths CgroupV2Manager::discover(CgroupV2Error& error) {
    CgroupV2Paths paths;
    error.code = CgroupV2ErrorCode::None;

    std::string effective_mount = "/sys/fs/cgroup";
    if (s_test_root_set_ && !s_test_root_.empty()) effective_mount = s_test_root_;

    {
        std::ifstream mountinfo("/proc/self/mountinfo");
        if (!mountinfo.is_open()) {
            error.code = CgroupV2ErrorCode::MountNotFound;
            error.message = "Cannot open /proc/self/mountinfo";
            error.saved_errno = errno;
            return paths;
        }
        std::string line;
        bool found = false;
        while (std::getline(mountinfo, line)) {
            if (line.find(" - cgroup2") == std::string::npos &&
                line.find(" - cgroup2 ") == std::string::npos) continue;
            std::istringstream iss(line);
            std::string field;
            for (int i = 0; i < 5; ++i) { if (!(iss >> field)) break; }
            if (!field.empty() && field[0] == '/') { paths.mount = field; found = true; break; }
        }
        if (!found) {
            error.code = CgroupV2ErrorCode::MountNotFound;
            error.message = "No cgroup2 mount found in /proc/self/mountinfo";
            return paths;
        }
    }

    {
        std::ifstream self_cgroup("/proc/self/cgroup");
        if (!self_cgroup.is_open()) {
            error.code = CgroupV2ErrorCode::CurrentPathNotFound;
            error.message = "Cannot open /proc/self/cgroup";
            error.saved_errno = errno;
            return paths;
        }
        std::string line;
        while (std::getline(self_cgroup, line)) {
            if (line.rfind("0::", 0) != 0) continue;
            std::string raw = line.substr(3);
            std::string cleaned = unescape_cgroup_path(raw);
            if (cleaned.empty()) {
                error.code = CgroupV2ErrorCode::CurrentPathNotFound;
                error.message = "Invalid cgroup path in /proc/self/cgroup";
                error.path = raw;
                return paths;
            }
            if (!validate_no_escape(cleaned)) {
                error.code = CgroupV2ErrorCode::CurrentPathNotFound;
                error.message = "Unsafe cgroup path";
                error.path = cleaned;
                return paths;
            }
            paths.self_relative = cleaned;
            paths.service_root = effective_mount + cleaned;
            return paths;
        }
        error.code = CgroupV2ErrorCode::CurrentPathNotFound;
        error.message = "No unified (0::) hierarchy in /proc/self/cgroup";
        return paths;
    }
}

// ── Low-level file I/O ──────────────────────────────────────

bool CgroupV2Manager::write_cgroup_file(const std::string& cg_dir,
                                         const std::string& filename,
                                         const std::string& value,
                                         CgroupV2Error& error,
                                         const char* op_name) {
    std::string full_path = cg_dir + "/" + filename;
    int fd = open(full_path.c_str(), O_WRONLY);
    if (fd < 0) {
        error.code = CgroupV2ErrorCode::LimitWriteFailed;
        error.message = std::string(op_name) + ": open failed";
        error.path = full_path;
        error.saved_errno = errno;
        return false;
    }
    ssize_t n = write(fd, value.c_str(), value.size());
    int write_errno = errno;
    close(fd);
    if (n < 0 || static_cast<size_t>(n) != value.size()) {
        error.code = CgroupV2ErrorCode::LimitWriteFailed;
        error.message = std::string(op_name) + ": write failed";
        error.path = full_path;
        error.saved_errno = write_errno;
        return false;
    }
    return true;
}

bool CgroupV2Manager::read_cgroup_file(const std::string& cg_dir,
                                        const std::string& filename,
                                        std::string& out,
                                        CgroupV2Error& error) {
    std::string full_path = cg_dir + "/" + filename;
    std::ifstream file(full_path);
    if (!file.is_open()) {
        error.code = CgroupV2ErrorCode::EventReadFailed;
        error.message = "Cannot open " + filename;
        error.path = full_path;
        error.saved_errno = errno;
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    if (file.bad()) {
        error.code = CgroupV2ErrorCode::EventReadFailed;
        error.message = "Read error on " + filename;
        error.path = full_path;
        error.saved_errno = errno;
        return false;
    }
    return true;
}

bool CgroupV2Manager::enable_controllers(const std::string& cg_dir,
                                          const std::string& controllers,
                                          CgroupV2Error& error) {
    return write_cgroup_file(cg_dir, "cgroup.subtree_control",
                             controllers, error, "enable_controllers");
}

// ── Service initialization ──────────────────────────────────

bool CgroupV2Manager::init_service(CgroupV2Error& error) {
    if (initialized_) return true;

    CgroupV2Paths paths = discover(error);
    if (!error.ok()) return false;

    mount_ = paths.mount;
    service_root_ = paths.service_root;

    struct stat st;
    if (stat(service_root_.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        error.code = CgroupV2ErrorCode::DelegationMissing;
        error.message = "Service root cgroup does not exist";
        error.path = service_root_;
        error.saved_errno = errno;
        return false;
    }

    // Create manager_<pid>
    manager_path_ = service_root_ + "/manager_" + std::to_string(own_pid_);
    if (mkdir(manager_path_.c_str(), 0755) != 0) {
        if (errno != EEXIST) {
            error.code = CgroupV2ErrorCode::CreateFailed;
            error.message = "Cannot create manager cgroup";
            error.path = manager_path_;
            error.saved_errno = errno;
            return false;
        }
    }

    // Move self into manager
    {
        std::string pid_str = std::to_string(own_pid_);
        if (!write_cgroup_file(manager_path_, "cgroup.procs", pid_str,
                               error, "move self to manager")) {
            rmdir(manager_path_.c_str());
            manager_path_.clear();
            return false;
        }
    }

    // Verify self is now in manager
    {
        std::string vpath = "/proc/" + std::to_string(own_pid_) + "/cgroup";
        std::ifstream vf(vpath);
        if (!vf.is_open()) {
            error.code = CgroupV2ErrorCode::VerificationFailed;
            error.message = "Cannot verify cgroup after self-move";
            return false;
        }
        std::string vline;
        bool ok = false;
        std::string expect = "manager_" + std::to_string(own_pid_);
        while (std::getline(vf, vline)) {
            if (vline.rfind("0::", 0) != 0) continue;
            if (vline.find(expect) != std::string::npos) { ok = true; break; }
        }
        if (!ok) {
            error.code = CgroupV2ErrorCode::VerificationFailed;
            error.message = "Self not in manager after move";
            error.path = manager_path_;
            return false;
        }
    }

    // Enable controllers in service_root (now we're in manager, sr is empty)
    {
        std::string ctrls_str;
        if (!read_cgroup_file(service_root_, "cgroup.controllers", ctrls_str, error))
            return false;
        while (!ctrls_str.empty() && (ctrls_str.back() == '\n' || ctrls_str.back() == ' '))
            ctrls_str.pop_back();

        bool has_mem = false, has_pids = false;
        std::istringstream ciss(ctrls_str);
        std::string c;
        while (ciss >> c) {
            if (c == "memory") has_mem = true;
            if (c == "pids") has_pids = true;
        }
        if (!has_mem || !has_pids) {
            error.code = CgroupV2ErrorCode::ControllerMissing;
            error.message = "Required controllers missing: " + ctrls_str;
            error.path = service_root_ + "/cgroup.controllers";
            return false;
        }
        if (!enable_controllers(service_root_, "+memory +pids", error))
            return false;

        std::string sub_ctrl;
        if (!read_cgroup_file(service_root_, "cgroup.subtree_control", sub_ctrl, error))
            return false;
        if (sub_ctrl.find("memory") == std::string::npos ||
            sub_ctrl.find("pids") == std::string::npos) {
            error.code = CgroupV2ErrorCode::VerificationFailed;
            error.message = "Controllers not enabled: " + sub_ctrl;
            error.path = service_root_ + "/cgroup.subtree_control";
            return false;
        }
    }

    // Check for memory.events.local
    {
        CgroupV2Error d;
        std::string dummy;
        has_local_events_ = read_cgroup_file(manager_path_, "memory.events.local", dummy, d);
    }

    initialized_ = true;
    return true;
}

// ── Run cgroup lifecycle ────────────────────────────────────

bool CgroupV2Manager::create_run(const CgroupV2Limits& limits,
                                  CgroupV2Error& error) {
    if (!initialized_) {
        error.code = CgroupV2ErrorCode::CreateFailed;
        error.message = "Service not initialized";
        return false;
    }

    if (!run_path_.empty()) {
        CgroupV2Error ignored;
        cleanup_run(ignored);
    }

    static std::atomic<std::uint64_t> run_counter{0};
    std::uint64_t c = run_counter.fetch_add(1, std::memory_order_relaxed);
    run_name_ = "run_" + std::to_string(own_pid_) + "_" + std::to_string(c);
    run_path_ = service_root_ + "/" + run_name_;

    if (mkdir(run_path_.c_str(), 0755) != 0) {
        error.code = CgroupV2ErrorCode::CreateFailed;
        error.message = "Cannot create run cgroup";
        error.path = run_path_;
        error.saved_errno = errno;
        run_path_.clear(); run_name_.clear();
        return false;
    }

    if (limits.memory_max_bytes > 0) {
        if (!write_cgroup_file(run_path_, "memory.max",
                               std::to_string(limits.memory_max_bytes),
                               error, "set memory.max")) {
            cleanup_run_impl(); return false;
        }
    }

    if (!write_cgroup_file(run_path_, "memory.swap.max",
                           std::to_string(limits.memory_swap_max_bytes),
                           error, "set memory.swap.max")) {
        cleanup_run_impl(); return false;
    }

    if (limits.pids_max > 0) {
        if (!write_cgroup_file(run_path_, "pids.max",
                               std::to_string(limits.pids_max),
                               error, "set pids.max")) {
            cleanup_run_impl(); return false;
        }
    }

    // memory.oom.group (best-effort)
    {
        std::string oomg = run_path_ + "/memory.oom.group";
        int fd = open(oomg.c_str(), O_WRONLY);
        if (fd >= 0) { const char* one = "1"; write(fd, one, 1); close(fd); }
    }

    // Baseline read
    { CgroupV2Stats bs; CgroupV2Error d; read_stats(bs, d); }

    return true;
}

// ── Process join ─────────────────────────────────────────────

bool CgroupV2Manager::join_process(pid_t pid, CgroupV2Error& error) {
    if (run_path_.empty()) {
        error.code = CgroupV2ErrorCode::ProcessJoinFailed;
        error.message = "No active run cgroup";
        return false;
    }
    if (!write_cgroup_file(run_path_, "cgroup.procs", std::to_string(pid),
                           error, "join process")) return false;

    std::string vp = "/proc/" + std::to_string(pid) + "/cgroup";
    std::ifstream vf(vp);
    if (!vf.is_open()) return true; // process already gone
    std::string line;
    bool found = false;
    while (std::getline(vf, line)) {
        if (line.rfind("0::", 0) != 0) continue;
        if (line.find(run_name_) != std::string::npos) { found = true; break; }
    }
    if (!found) {
        error.code = CgroupV2ErrorCode::VerificationFailed;
        error.message = "Process not in run cgroup after join";
        error.path = run_path_;
        return false;
    }
    return true;
}

// ── Event parsing ────────────────────────────────────────────

bool CgroupV2Manager::parse_uint64(const char* s, const char* e, std::uint64_t& out) {
    if (s >= e) return false;
    std::uint64_t v = 0;
    for (const char* p = s; p < e; ++p) {
        if (*p < '0' || *p > '9') return false;
        std::uint64_t old = v;
        v = v * 10 + static_cast<std::uint64_t>(*p - '0');
        if (v < old) return false;
    }
    out = v;
    return true;
}

bool CgroupV2Manager::parse_events(const std::string& text,
                                    CgroupV2Stats& stats, CgroupV2Error& error) {
    bool has_oom = false, has_oom_kill = false;
    const char* p = text.c_str();
    const char* e = p + text.size();
    while (p < e) {
        while (p < e && (*p == ' ' || *p == '\n' || *p == '\r')) ++p;
        if (p >= e) break;
        const char* ks = p;
        while (p < e && *p != ' ' && *p != '\n' && *p != '\r') ++p;
        const char* ke = p;
        while (p < e && *p == ' ') ++p;
        const char* vs = p;
        while (p < e && *p != ' ' && *p != '\n' && *p != '\r') ++p;
        const char* ve = p;
        std::string key(ks, ke - ks);
        std::uint64_t val = 0;
        if (!parse_uint64(vs, ve, val)) continue;
        if (key == "oom") { stats.oom = val; has_oom = true; }
        else if (key == "oom_kill") { stats.oom_kill = val; has_oom_kill = true; }
        else if (key == "oom_group_kill") { stats.oom_group_kill = val; }
        else if (key == "max") { stats.max_events = val; }
    }
    if (!has_oom || !has_oom_kill) {
        error.code = CgroupV2ErrorCode::EventReadFailed;
        error.message = "Required event fields missing from memory.events";
        return false;
    }
    return true;
}

// ── Stats reading ────────────────────────────────────────────

bool CgroupV2Manager::read_stats(CgroupV2Stats& stats, CgroupV2Error& error) {
    if (run_path_.empty()) {
        error.code = CgroupV2ErrorCode::EventReadFailed;
        error.message = "No active run cgroup";
        return false;
    }
    std::string text;
    CgroupV2Error rd;
    if (has_local_events_) {
        if (read_cgroup_file(run_path_, "memory.events.local", text, rd)) goto parse;
    }
    if (!read_cgroup_file(run_path_, "memory.events", text, rd)) { error = rd; return false; }
parse:
    if (!parse_events(text, stats, error)) return false;
    {
        std::string pt;
        CgroupV2Error pe;
        if (read_cgroup_file(run_path_, "memory.peak", pt, pe)) {
            std::uint64_t peak = 0;
            const char* pp = pt.c_str();
            while (*pp == ' ' || *pp == '\n') ++pp;
            parse_uint64(pp, pp + pt.size(), peak);
            stats.memory_peak_bytes = peak;
        }
    }
    return true;
}

// ── Kill ─────────────────────────────────────────────────────

bool CgroupV2Manager::kill_run(CgroupV2Error& error) {
    if (run_path_.empty()) {
        error.code = CgroupV2ErrorCode::KillFailed;
        error.message = "No active run cgroup";
        return false;
    }
    std::string kp = run_path_ + "/cgroup.kill";
    int fd = open(kp.c_str(), O_WRONLY);
    if (fd >= 0) { const char* one = "1"; ssize_t n = write(fd, one, 1); close(fd); if (n == 1) return true; }

    std::string procs;
    CgroupV2Error rd;
    if (!read_cgroup_file(run_path_, "cgroup.procs", procs, rd)) {
        error = rd; error.code = CgroupV2ErrorCode::KillFailed; return false;
    }
    std::istringstream iss(procs);
    std::string ps;
    while (iss >> ps) {
        pid_t p = 0;
        try { p = static_cast<pid_t>(std::stoll(ps)); } catch (...) { continue; }
        if (p > 0 && p != own_pid_) kill(p, SIGKILL);
    }
    return true;
}

// ── Wait for empty ──────────────────────────────────────────

bool CgroupV2Manager::wait_empty(int timeout_sec, CgroupV2Error& error) {
    if (run_path_.empty()) return true;
    auto start = std::chrono::steady_clock::now();
    while (true) {
        std::string text;
        CgroupV2Error rd;
        if (!read_cgroup_file(run_path_, "cgroup.events", text, rd)) { error = rd; return false; }
        const char* p = text.c_str();
        const char* e = p + text.size();
        int pop = -1;
        while (p < e) {
            if (strncmp(p, "populated ", 10) == 0) {
                p += 10; pop = 0;
                while (p < e && *p >= '0' && *p <= '9') { pop = pop * 10 + (*p - '0'); ++p; }
                break;
            }
            while (p < e && *p != '\n') ++p;
            if (p < e) ++p;
        }
        if (pop == 0) return true;
        auto now = std::chrono::steady_clock::now();
        int el = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - start).count());
        if (el >= timeout_sec) {
            error.code = CgroupV2ErrorCode::CleanupFailed;
            error.message = "Timeout waiting for cgroup to empty";
            error.path = run_path_;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// ── Cleanup ──────────────────────────────────────────────────

void CgroupV2Manager::cleanup_run_impl() {
    if (run_path_.empty()) return;
    { CgroupV2Error e; kill_run(e); }
    { CgroupV2Error e; wait_empty(CGROUP_OPS_TIMEOUT_SEC, e); }
    rmdir(run_path_.c_str());
    run_path_.clear(); run_name_.clear();
}

bool CgroupV2Manager::cleanup_run(CgroupV2Error& error) {
    if (run_path_.empty()) return true;
    CgroupV2Stats fs; read_stats(fs, error);
    kill_run(error);
    wait_empty(CGROUP_OPS_TIMEOUT_SEC, error);
    if (rmdir(run_path_.c_str()) != 0) {
        if (errno == ENOENT) { /* ok */ }
        else if (errno == EBUSY) {
            error.code = CgroupV2ErrorCode::CleanupFailed;
            error.message = "Run cgroup still busy after kill+wait";
            error.path = run_path_; error.saved_errno = errno; return false;
        } else {
            error.code = CgroupV2ErrorCode::CleanupFailed;
            error.message = "Cannot rmdir run cgroup";
            error.path = run_path_; error.saved_errno = errno; return false;
        }
    }
    run_path_.clear(); run_name_.clear();
    return true;
}
