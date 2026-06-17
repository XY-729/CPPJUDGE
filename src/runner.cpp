#include "runner.h"
#include "cgroup_v2.h"
#include "seccomp_config.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <limits.h>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#if defined(__linux__)
extern char** environ;
#endif

namespace {

static constexpr SandboxType DEFAULT_SANDBOX_TYPE = SandboxType::BUILTIN;
static constexpr int BUILTIN_NOFILE_LIMIT = 64;
static constexpr int BUILTIN_NPROC_LIMIT = 16;
static constexpr int NSJAIL_AS_HEADROOM_MB = 64;
static constexpr int NSJAIL_PIDS_HEADROOM = 8; // nsjail supervisor + user processes
static constexpr int CGROUP_KILL_TIMEOUT_SEC = 5;

// ── Cgroup manager (lazy-initialized, once per process) ────

static CgroupV2Manager g_cgroup_mgr;
static bool g_cgroup_attempted = false;
static bool g_cgroup_ready = false;
static std::string g_cgroup_init_error;
static SeccompConfig g_seccomp_config;
static bool g_seccomp_checked = false;
static bool g_seccomp_ready = false;
static std::string g_seccomp_error;

static bool ensure_cgroup_ready() {
    if (g_cgroup_attempted) return g_cgroup_ready;
    g_cgroup_attempted = true;

    CgroupV2Error error;
    if (!g_cgroup_mgr.init_service(error)) {
        g_cgroup_init_error = cgroup_v2_error_message(error, "service_init");
        return false;
    }
    g_cgroup_ready = true;
    return true;
}

static bool ensure_seccomp_ready() {
    if (g_seccomp_checked) return g_seccomp_ready;
    g_seccomp_checked = true;

    g_seccomp_config.enabled = true;

    const char* env_path = std::getenv("CPPJUDGE_SECCOMP_POLICY");
    if (env_path && env_path[0] != '\0') {
        g_seccomp_config.policy_path = env_path;
    } else {
        g_seccomp_config.policy_path = default_seccomp_policy_path();
    }

    std::string validate_error;
    if (!g_seccomp_config.validate(validate_error)) {
        g_seccomp_error = "seccomp policy validation failed: " + validate_error;
        return false;
    }

    g_seccomp_ready = true;
    return true;
}

struct SandboxRunConfig {
    std::string executable_file;
    std::string input_file;
    std::string output_file;
    std::string error_file;
    std::string run_dir;
    std::string user_output_dir;
    std::string sandbox_root_dir;
    std::string sandbox_solution_file;
    std::string sandbox_user_output_dir;
    int time_limit_ms;
    int memory_limit_mb;
    int output_limit_mb;
};

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

int kb_to_mb(long kb) {
    if (kb <= 0) {
        return 0;
    }
    return static_cast<int>((kb + 1023) / 1024);
}

bool reached_memory_limit(int memory_mb, int memory_limit_mb) {
    if (memory_limit_mb <= 0) {
        return false;
    }
    return memory_mb > memory_limit_mb;
}

bool file_contains(const std::string& file_path, const std::string& keyword) {
    std::ifstream file(file_path);
    if (!file.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str().find(keyword) != std::string::npos;
}

int read_process_memory_mb(pid_t pid) {
    std::ifstream status_file("/proc/" + std::to_string(pid) + "/status");
    if (!status_file.is_open()) {
        return 0;
    }
    std::string line;
    while (std::getline(status_file, line)) {
        if (line.rfind("VmSize:", 0) == 0) {
            std::istringstream iss(line);
            std::string key;
            long kb = 0;
            std::string unit;
            iss >> key >> kb >> unit;
            return kb_to_mb(kb);
        }
    }
    return 0;
}

int rusage_memory_mb(const struct rusage& usage) {
    return kb_to_mb(usage.ru_maxrss);
}

void set_limit_or_exit(int resource, long long value) {
    struct rlimit limit {};
    limit.rlim_cur = static_cast<rlim_t>(value);
    limit.rlim_max = static_cast<rlim_t>(value);
    if (setrlimit(resource, &limit) != 0) {
        _exit(1);
    }
}

void close_extra_file_descriptors(int preserved_fd = -1) {
    for (int fd = STDERR_FILENO + 1; fd < BUILTIN_NOFILE_LIMIT; ++fd) {
        if (fd == preserved_fd) continue;
        close(fd);
    }
}

void clear_child_environment() {
#if defined(__linux__)
    environ = nullptr;
#endif
}

bool is_executable_file(const std::filesystem::path& path) {
    return access(path.c_str(), X_OK) == 0;
}

bool executable_exists_in_path(const std::string& executable_name) {
    if (executable_name.find("/") != std::string::npos) {
        return is_executable_file(executable_name);
    }
    const char* path_value = std::getenv("PATH");
    if (path_value == nullptr) {
        return false;
    }
    std::string path_list = path_value;
    std::size_t start = 0;
    while (start <= path_list.size()) {
        std::size_t end = path_list.find(":", start);
        std::string entry = path_list.substr(
            start,
            end == std::string::npos ? std::string::npos : end - start
        );
        if (entry.empty()) {
            entry = ".";
        }
        if (is_executable_file(std::filesystem::path(entry) / executable_name)) {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

void apply_builtin_child_limits(int time_limit_ms) {
    set_limit_or_exit(RLIMIT_CORE, 0);
    set_limit_or_exit(RLIMIT_NOFILE, BUILTIN_NOFILE_LIMIT);
#if defined(RLIMIT_NPROC)
    set_limit_or_exit(RLIMIT_NPROC, BUILTIN_NPROC_LIMIT);
#endif
    int cpu_seconds = (time_limit_ms + 999) / 1000 + 1;
    set_limit_or_exit(RLIMIT_CPU, cpu_seconds);
}

int nsjail_time_limit_seconds(int time_limit_ms) {
    return (time_limit_ms + 999) / 1000 + 1;
}

int nsjail_address_space_limit_mb(int memory_limit_mb) {
    return memory_limit_mb + NSJAIL_AS_HEADROOM_MB;
}

// cgroup memory.max = user limit + infrastructure headroom
std::uint64_t cgroup_memory_max_bytes(int memory_limit_mb) {
    std::uint64_t user_bytes = static_cast<std::uint64_t>(memory_limit_mb) * 1024ULL * 1024ULL;
    std::uint64_t headroom_bytes = static_cast<std::uint64_t>(NSJAIL_AS_HEADROOM_MB) * 1024ULL * 1024ULL;
    return user_bytes + headroom_bytes;
}

// cgroup pids.max = user process limit + nsjail infrastructure headroom
std::uint64_t cgroup_pids_max() {
    return static_cast<std::uint64_t>(BUILTIN_NPROC_LIMIT) + static_cast<std::uint64_t>(NSJAIL_PIDS_HEADROOM);
}

std::string absolute_path_for_nsjail(const std::string& path) {
    char resolved_path[PATH_MAX];
    if (realpath(path.c_str(), resolved_path) != nullptr) {
        return resolved_path;
    }
    return path;
}

void create_empty_file_if_missing(const std::filesystem::path& path) {
    if (std::filesystem::exists(path)) {
        return;
    }
    std::ofstream file(path);
}

void create_relative_symlink_if_missing(
    const std::filesystem::path& target,
    const std::filesystem::path& link
) {
    if (std::filesystem::exists(link) || std::filesystem::is_symlink(link)) {
        return;
    }
    std::filesystem::create_directory_symlink(target, link);
}

void add_bindmount_if_exists(
    std::vector<std::string>& args,
    const std::string& flag,
    const std::string& source,
    const std::string& destination
) {
    if (!std::filesystem::exists(source)) {
        return;
    }
    args.push_back(flag);
    args.push_back(absolute_path_for_nsjail(source) + ":" + destination);
}

void add_readonly_file_bind_if_exists(
    std::vector<std::string>& args,
    const std::string& source,
    const std::string& destination
) {
    add_bindmount_if_exists(args, "--bindmount_ro", source, destination);
}

std::vector<std::string> nsjail_required_library_paths() {
    return {
        "/usr/lib64/ld-linux-x86-64.so.2",
        "/usr/lib64/libstdc++.so.6",
        "/usr/lib64/libm.so.6",
        "/usr/lib64/libgcc_s.so.1",
        "/usr/lib64/libc.so.6"
    };
}

bool prepare_nsjail_filesystem(const SandboxRunConfig& config, std::string& error) {
    try {
        std::filesystem::path root(config.sandbox_root_dir);
        std::filesystem::path sandbox_dir = root / "sandbox";
        std::filesystem::path etc_dir = root / "etc";
        std::filesystem::path usr_dir = root / "usr";

        std::filesystem::create_directories(sandbox_dir / "user_output");
        std::filesystem::create_directories(etc_dir);
        std::filesystem::create_directories(usr_dir / "lib64");
        std::filesystem::create_directories(usr_dir / "lib");
        std::filesystem::create_directories(root / "dev");

        create_relative_symlink_if_missing("usr/lib64", root / "lib64");
        create_relative_symlink_if_missing("usr/lib", root / "lib");

        create_empty_file_if_missing(sandbox_dir / "solution");
        create_empty_file_if_missing(etc_dir / "ld.so.cache");
        create_empty_file_if_missing(etc_dir / "ld.so.conf");
        std::filesystem::create_directories(etc_dir / "ld.so.conf.d");

        for (const std::string& library_path : nsjail_required_library_paths()) {
            std::filesystem::path destination = root / std::filesystem::path(library_path).relative_path();
            std::filesystem::create_directories(destination.parent_path());
            create_empty_file_if_missing(destination);
        }
    } catch (const std::exception& exc) {
        error = "Failed to prepare nsjail filesystem: " + std::string(exc.what());
        return false;
    }
    return true;
}

SandboxRunConfig make_sandbox_run_config(
    const std::string& executable_file,
    const std::string& input_file,
    const std::string& output_file,
    int time_limit_ms,
    int memory_limit_mb,
    int output_limit_mb
) {
    std::filesystem::path output_path(output_file);
    std::filesystem::path user_output_dir = output_path.parent_path();
    std::filesystem::path run_dir = user_output_dir.parent_path();
    std::filesystem::path sandbox_root_dir = run_dir / "sandbox_root";

    return {
        executable_file,
        input_file,
        output_file,
        output_file + ".err",
        run_dir.string(),
        user_output_dir.string(),
        sandbox_root_dir.string(),
        "/sandbox/solution",
        "/sandbox/user_output",
        time_limit_ms,
        memory_limit_mb,
        output_limit_mb
    };
}

std::vector<std::string> build_nsjail_args(const SandboxRunConfig& config) {
    std::vector<std::string> args = {
        "nsjail",
        "-Me",
        "--chroot",
        absolute_path_for_nsjail(config.sandbox_root_dir),
        "--cwd",
        "/sandbox",
        "--disable_proc",
        "--time_limit",
        std::to_string(nsjail_time_limit_seconds(config.time_limit_ms)),
        "--rlimit_fsize",
        std::to_string(config.output_limit_mb),
        "--rlimit_core",
        "0",
        "--rlimit_cpu",
        std::to_string(nsjail_time_limit_seconds(config.time_limit_ms)),
        "--rlimit_nofile",
        std::to_string(BUILTIN_NOFILE_LIMIT),
        "--rlimit_nproc",
        std::to_string(BUILTIN_NPROC_LIMIT)
    };

    // Stage 3C: enforce seccomp policy (fail-closed via preflight)
    if (g_seccomp_ready) {
        args.push_back("--seccomp_policy");
        args.push_back(g_seccomp_config.policy_path);
    }

    args.push_back("--bindmount_ro");
    args.push_back(absolute_path_for_nsjail(config.executable_file) + ":" + config.sandbox_solution_file);

    args.push_back("--bindmount");
    args.push_back(absolute_path_for_nsjail(config.user_output_dir) + ":" + config.sandbox_user_output_dir);

    for (const std::string& library_path : nsjail_required_library_paths()) {
        add_readonly_file_bind_if_exists(args, library_path, library_path);
    }

    add_readonly_file_bind_if_exists(args, "/etc/ld.so.cache", "/etc/ld.so.cache");
    add_readonly_file_bind_if_exists(args, "/etc/ld.so.conf", "/etc/ld.so.conf");
    add_bindmount_if_exists(args, "--bindmount_ro", "/etc/ld.so.conf.d", "/etc/ld.so.conf.d");

    args.push_back("--");
    args.push_back(config.sandbox_solution_file);

    return args;
}

std::string join_args_for_log(const std::vector<std::string>& args) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            oss << ' ';
        }
        const std::string& arg = args[i];
        bool needs_quotes = arg.find_first_of(" \t\n\"'") != std::string::npos;
        if (!needs_quotes) {
            oss << arg;
            continue;
        }
        oss << '"';
        for (char ch : arg) {
            if (ch == '"' || ch == '\\') {
                oss << '\\';
            }
            oss << ch;
        }
        oss << '"';
    }
    return oss.str();
}

bool output_file_reached_limit(const std::string& output_file, int output_limit_mb) {
    if (output_limit_mb <= 0) {
        return false;
    }
    try {
        if (!std::filesystem::exists(output_file)) {
            return false;
        }
        std::uintmax_t output_limit_bytes =
            static_cast<std::uintmax_t>(output_limit_mb) * 1024 * 1024;
        return std::filesystem::file_size(output_file) >= output_limit_bytes;
    } catch (const std::exception&) {
        return false;
    }
}

void kill_process_group(pid_t pid) {
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
}

// Helper: create an SE RunInfo with consistent defaults
static RunInfo make_system_error(const std::string& message) {
    RunInfo info;
    info.result = RunResult::SE;
    info.time_ms = 0;
    info.memory_mb = 0;
    info.system_error = true;
    info.error_message = message;
    info.exit_code = -1;
    info.signal = -1;
    return info;
}

// ── Verdict classification (Stage 3B) ──────────────────────

// Classify the final verdict from process status + cgroup events + termination cause.
// Priority order:
//   1. SE (infrastructure / cgroup error)
//   2. TLE (parent WallClockTimeout)
//   3. OLE (parent OutputLimit)
//   4. MLE (oom_kill delta >= 1)
//   5. OLE (SIGXFSZ)
//   6. TLE (SIGXCPU)
//   7. OK (exit 0)
//   8. RE (exit non-0)
//   9. RE (signalled, not otherwise classified)
static void classify_verdict(RunInfo& info,
                              int status,
                              const CgroupV2EventSnapshot& events_delta,
                              TerminationCause cause) {
    // 1. Infrastructure error (already set by caller)
    if (info.result == RunResult::SE) return;

    // 2. Parent-initiated wall clock timeout → TLE
    if (cause == TerminationCause::WallClockTimeout) {
        info.result = RunResult::TLE;
        return;
    }

    // 3. Parent-initiated output limit → OLE
    if (cause == TerminationCause::OutputLimit) {
        info.result = RunResult::OLE;
        return;
    }

    // 4. Real OOM detected by cgroup → MLE
    if (events_delta.oom_kill >= 1) {
        info.result = RunResult::MLE;
        return;
    }

    // 5-9. Process exit / signal classification
    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        info.exit_code = exit_code;
        if (exit_code == 0) {
            info.result = RunResult::OK;
        } else {
            info.result = RunResult::RE;
        }
        return;
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        info.signal = sig;
        if (sig == SIGXFSZ) {
            info.result = RunResult::OLE;
        } else if (sig == SIGXCPU) {
            info.result = RunResult::TLE;
        } else {
            info.result = RunResult::RE;
        }
        return;
    }

    // Fallback
    info.result = RunResult::RE;
}

} // namespace

std::string run_result_to_string(RunResult result) {
    switch (result) {
        case RunResult::OK:
            return "OK";
        case RunResult::TLE:
            return "TLE";
        case RunResult::MLE:
            return "MLE";
        case RunResult::OLE:
            return "OLE";
        case RunResult::RE:
            return "RE";
        case RunResult::SE:
            return "SE";
        default:
            return "UNKNOWN";
    }
}

std::string sandbox_type_to_string(SandboxType type) {
    switch (type) {
        case SandboxType::BUILTIN:
            return "builtin";
        case SandboxType::NSJAIL:
            return "nsjail";
        case SandboxType::ISOLATE:
            return "isolate";
        default:
            return "unknown";
    }
}

SandboxType sandbox_type_from_string(const std::string& type) {
    std::string normalized = to_lower(type);
    if (normalized == "nsjail") {
        return SandboxType::NSJAIL;
    }
    if (normalized == "isolate") {
        return SandboxType::ISOLATE;
    }
    return SandboxType::BUILTIN;
}

bool is_valid_sandbox_type(const std::string& type) {
    std::string normalized = to_lower(type);
    return normalized == "builtin" ||
           normalized == "nsjail" ||
           normalized == "isolate";
}

bool sandbox_preflight_check(SandboxType type, std::string& error) {
    error.clear();
    if (type == SandboxType::BUILTIN) {
        return true;
    }
    if (type == SandboxType::NSJAIL) {
        if (!executable_exists_in_path("nsjail")) {
            error = "nsjail executable not found in PATH";
            return false;
        }
        // Stage 3B: nsjail production path requires delegated cgroup
        if (!ensure_cgroup_ready()) {
            error = "cgroup v2 delegation required for nsjail: " + g_cgroup_init_error;
            return false;
        }
        // Stage 3C: nsjail production path requires seccomp policy
        if (!ensure_seccomp_ready()) {
            error = "seccomp policy required for nsjail: " + g_seccomp_error;
            return false;
        }
        return true;
    }
    if (type == SandboxType::ISOLATE) {
        return true;
    }
    error = "Unknown sandbox type: " + sandbox_type_to_string(type);
    return false;
}

// ── Builtin runner (unchanged core, Stage 3B: remove bad_alloc stderr check) ──

static RunInfo run_program_builtin(
    const std::string& executable_file,
    const std::string& input_file,
    const std::string& output_file,
    int time_limit_ms,
    int memory_limit_mb,
    int output_limit_mb
) {
    std::string error_file = output_file + ".err";

    // Pre-open files in parent so failures are detected as system errors.
    int input_fd = open(input_file.c_str(), O_RDONLY);
    if (input_fd < 0) {
        return make_system_error("Failed to open input file: " + input_file);
    }

    int output_fd = open(output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output_fd < 0) {
        close(input_fd);
        return make_system_error("Failed to open output file: " + output_file);
    }

    int error_fd = open(error_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (error_fd < 0) {
        close(input_fd);
        close(output_fd);
        return make_system_error("Failed to open error file: " + error_file + " - " + std::string(strerror(errno)));
    }

    int exec_error_pipe[2];
    if (pipe2(exec_error_pipe, O_CLOEXEC) < 0) {
        close(input_fd);
        close(output_fd);
        if (error_fd >= 0) close(error_fd);
        return make_system_error("Failed to create exec-error pipe");
    }

    pid_t pid = fork();

    if (pid < 0) {
        close(input_fd);
        close(output_fd);
        if (error_fd >= 0) close(error_fd);
        close(exec_error_pipe[0]);
        close(exec_error_pipe[1]);
        return make_system_error("Failed to fork runner process");
    }

    if (pid == 0) {
        setpgid(0, 0);
        close(exec_error_pipe[0]);

        dup2(input_fd, STDIN_FILENO);
        dup2(output_fd, STDOUT_FILENO);
        if (error_fd >= 0) {
            dup2(error_fd, STDERR_FILENO);
        }

        close(input_fd);
        close(output_fd);
        if (error_fd >= 0) {
            close(error_fd);
        }

        close_extra_file_descriptors(exec_error_pipe[1]);
        clear_child_environment();
        apply_builtin_child_limits(time_limit_ms);

        if (memory_limit_mb > 0) {
            int address_space_limit_mb = memory_limit_mb + 256;
            long long memory_bytes = 1LL * address_space_limit_mb * 1024 * 1024;
            set_limit_or_exit(RLIMIT_AS, memory_bytes);
        }

        if (output_limit_mb > 0) {
            long long output_bytes = 1LL * output_limit_mb * 1024 * 1024;
            set_limit_or_exit(RLIMIT_FSIZE, output_bytes);
        }

        execl(
            executable_file.c_str(),
            executable_file.c_str(),
            static_cast<char*>(nullptr)
        );

        // exec failed - signal parent through pipe
        char err = 1;
        ssize_t written = write(exec_error_pipe[1], &err, 1);
        (void)written;
        close(exec_error_pipe[1]);
        _exit(127);
    }

    close(input_fd);
    close(output_fd);
    if (error_fd >= 0) close(error_fd);
    close(exec_error_pipe[1]);

    auto start_time = std::chrono::steady_clock::now();
    int status = 0;
    struct rusage usage {};
    int peak_memory_mb = 0;

    while (true) {
        pid_t wait_result = wait4(pid, &status, WNOHANG, &usage);

        auto now = std::chrono::steady_clock::now();
        int elapsed_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - start_time
            ).count()
        );

        if (wait_result == pid) {
            break;
        }

        if (wait_result == -1) {
            close(exec_error_pipe[0]);
            return make_system_error("wait4 failed for runner process");
        }

        int current_memory_mb = read_process_memory_mb(pid);
        peak_memory_mb = std::max(peak_memory_mb, current_memory_mb);

        if (reached_memory_limit(current_memory_mb, memory_limit_mb)) {
            kill_process_group(pid);
            wait4(pid, &status, 0, &usage);

            auto end_time = std::chrono::steady_clock::now();
            int final_time_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time
                ).count()
            );
            int memory_mb = std::max(peak_memory_mb, rusage_memory_mb(usage));

            RunInfo info;
            info.result = RunResult::MLE;
            info.time_ms = final_time_ms;
            info.memory_mb = memory_mb;
            info.termination_cause = TerminationCause::InfrastructureFailure;
            close(exec_error_pipe[0]);
            return info;
        }

        if (elapsed_ms > time_limit_ms) {
            kill_process_group(pid);
            wait4(pid, &status, 0, &usage);

            auto end_time = std::chrono::steady_clock::now();
            int final_time_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time
                ).count()
            );
            int memory_mb = std::max(peak_memory_mb, rusage_memory_mb(usage));

            RunInfo info;
            info.result = RunResult::TLE;
            info.time_ms = final_time_ms;
            info.memory_mb = memory_mb;
            info.termination_cause = TerminationCause::WallClockTimeout;
            close(exec_error_pipe[0]);
            return info;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    auto end_time = std::chrono::steady_clock::now();
    int final_time_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time
        ).count()
    );
    int memory_mb = std::max(peak_memory_mb, rusage_memory_mb(usage));

    // Check if exec failed
    char exec_err = 0;
    ssize_t n = read(exec_error_pipe[0], &exec_err, 1);
    close(exec_error_pipe[0]);

    if (n > 0) {
        return make_system_error("Failed to execute solution: " + executable_file);
    }

    RunInfo info;
    info.time_ms = final_time_ms;
    info.memory_mb = memory_mb;

    if (WIFEXITED(status)) {
        info.exit_code = WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        info.signal = WTERMSIG(status);
    }

    // Stage 3B: removed bad_alloc stderr text check
    // MLE is detected via VmSize polling + rlimit (above)
    // exit(137) → RE (not MLE without oom_kill evidence)

    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code == 0) {
            info.result = RunResult::OK;
            return info;
        }
        if (reached_memory_limit(memory_mb, memory_limit_mb)) {
            info.result = RunResult::MLE;
            return info;
        }
        info.result = RunResult::RE;
        return info;
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        if (sig == SIGXFSZ) {
            info.result = RunResult::OLE;
            return info;
        }
        if (sig == SIGXCPU) {
            info.result = RunResult::TLE;
            return info;
        }
        if (sig == SIGKILL) {
            if (reached_memory_limit(memory_mb, memory_limit_mb)) {
                info.result = RunResult::MLE;
                return info;
            }
            info.result = RunResult::TLE;
            return info;
        }
        if (reached_memory_limit(memory_mb, memory_limit_mb)) {
            info.result = RunResult::MLE;
            return info;
        }
        info.result = RunResult::RE;
        return info;
    }

    info.result = RunResult::RE;
    return info;
}

// ── nsjail runner (Stage 3B: cgroup v2 + barrier + trusted verdict) ──

static RunInfo run_program_nsjail(
    const std::string& executable_file,
    const std::string& input_file,
    const std::string& output_file,
    int time_limit_ms,
    int memory_limit_mb,
    int output_limit_mb
)
{
    SandboxRunConfig config = make_sandbox_run_config(
        executable_file,
        input_file,
        output_file,
        time_limit_ms,
        memory_limit_mb,
        output_limit_mb
    );

    // Preflight: cgroup delegation must be ready
    if (!g_cgroup_ready) {
        return make_system_error(
            "nsjail production mode requires delegated cgroup v2: " + g_cgroup_init_error);
    }

    std::string prepare_error;
    if (!prepare_nsjail_filesystem(config, prepare_error)) {
        std::ofstream error_stream(config.error_file, std::ios::app);
        error_stream << prepare_error << "\n";
        return make_system_error(prepare_error);
    }

    // ── Create run cgroup ──────────────────────────────────

    CgroupV2Limits cg_limits;
    cg_limits.memory_max_bytes = cgroup_memory_max_bytes(memory_limit_mb);
    cg_limits.memory_swap_max_bytes = 0;
    cg_limits.pids_max = cgroup_pids_max();

    CgroupV2Error cg_error;
    if (!g_cgroup_mgr.create_run(cg_limits, cg_error)) {
        return make_system_error(
            cgroup_v2_error_message(cg_error, "run_create"));
    }

    // ── Pipes ─────────────────────────────────────────────

    int exec_error_pipe[2];
    if (pipe2(exec_error_pipe, O_CLOEXEC) < 0) {
        g_cgroup_mgr.cleanup_run(cg_error);
        return make_system_error("Failed to create exec-error pipe for nsjail runner");
    }

    int barrier_pipe[2];
    if (pipe2(barrier_pipe, O_CLOEXEC) < 0) {
        close(exec_error_pipe[0]);
        close(exec_error_pipe[1]);
        g_cgroup_mgr.cleanup_run(cg_error);
        return make_system_error("Failed to create barrier pipe for nsjail runner");
    }

    // ── Pre-open files ─────────────────────────────────────

    int input_fd = open(config.input_file.c_str(), O_RDONLY);
    if (input_fd < 0) {
        close(exec_error_pipe[0]); close(exec_error_pipe[1]);
        close(barrier_pipe[0]); close(barrier_pipe[1]);
        g_cgroup_mgr.cleanup_run(cg_error);
        return make_system_error("Failed to open nsjail input file: " + config.input_file + " - " + std::string(strerror(errno)));
    }

    int output_fd = open(config.output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (output_fd < 0) {
        close(input_fd);
        close(exec_error_pipe[0]); close(exec_error_pipe[1]);
        close(barrier_pipe[0]); close(barrier_pipe[1]);
        g_cgroup_mgr.cleanup_run(cg_error);
        return make_system_error("Failed to open nsjail output file: " + config.output_file + " - " + std::string(strerror(errno)));
    }

    int error_fd = open(config.error_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (error_fd < 0) {
        close(input_fd);
        close(output_fd);
        close(exec_error_pipe[0]); close(exec_error_pipe[1]);
        close(barrier_pipe[0]); close(barrier_pipe[1]);
        g_cgroup_mgr.cleanup_run(cg_error);
        return make_system_error("Failed to open nsjail error file: " + config.error_file + " - " + std::string(strerror(errno)));
    }

    // ── Fork ──────────────────────────────────────────────

    pid_t pid = fork();

    if (pid < 0) {
        close(input_fd); close(output_fd); close(error_fd);
        close(exec_error_pipe[0]); close(exec_error_pipe[1]);
        close(barrier_pipe[0]); close(barrier_pipe[1]);
        g_cgroup_mgr.cleanup_run(cg_error);
        return make_system_error("Failed to fork nsjail runner process");
    }

    if (pid == 0) {
        // ── Child ──────────────────────────────────────────
        setpgid(0, 0);
        close(exec_error_pipe[0]);
        close(barrier_pipe[1]); // close write end

        dup2(input_fd, STDIN_FILENO);
        dup2(output_fd, STDOUT_FILENO);
        dup2(error_fd, STDERR_FILENO);

        close(input_fd);
        close(output_fd);
        close(error_fd);

        if (config.output_limit_mb > 0) {
            long long output_bytes = 1LL * config.output_limit_mb * 1024 * 1024;
            set_limit_or_exit(RLIMIT_FSIZE, output_bytes);
        }

        set_limit_or_exit(RLIMIT_CORE, 0);

        // ── Barrier: wait for parent to add us to cgroup ──
        char barrier_byte = 0;
        ssize_t br = read(barrier_pipe[0], &barrier_byte, 1);
        close(barrier_pipe[0]);
        if (br != 1) {
            // Parent failed — exit immediately
            _exit(126);
        }

        // ── Exec nsjail ────────────────────────────────────
        std::vector<std::string> args = build_nsjail_args(config);
        std::string command_log = join_args_for_log(args);
        dprintf(STDERR_FILENO, "[nsjail] command: %s\n", command_log.c_str());

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (std::string& arg : args) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        execvp("nsjail", argv.data());

        // exec failed
        dprintf(STDERR_FILENO, "Failed to execute nsjail\n");
        char err = 1;
        ssize_t written = write(exec_error_pipe[1], &err, 1);
        (void)written;
        close(exec_error_pipe[1]);
        _exit(127);
    }

    // ── Parent ────────────────────────────────────────────
    close(input_fd);
    close(output_fd);
    close(error_fd);
    close(exec_error_pipe[1]);
    close(barrier_pipe[0]);

    // Join child to run cgroup
    CgroupV2EventSnapshot events_before;
    {
        CgroupV2Stats stats_before;
        if (g_cgroup_mgr.read_stats(stats_before, cg_error)) {
            events_before = make_snapshot(stats_before);
        }
        // Continue even if read fails — baseline may be partial
    }

    if (!g_cgroup_mgr.join_process(pid, cg_error)) {
        // Join failed — kill waiting child and return SE
        kill(pid, SIGKILL);
        waitpid(pid, nullptr, 0);
        close(exec_error_pipe[0]);
        close(barrier_pipe[1]);
        g_cgroup_mgr.cleanup_run(cg_error);
        return make_system_error(
            cgroup_v2_error_message(cg_error, "process_join"));
    }

    // Release child
    {
        char go = 'G';
        ssize_t bw = write(barrier_pipe[1], &go, 1);
        (void)bw;
    }
    close(barrier_pipe[1]);

    // ── Monitor loop ──────────────────────────────────────
    auto start_time = std::chrono::steady_clock::now();
    int status = 0;
    struct rusage usage {};
    TerminationCause cause = TerminationCause::None;

    while (true) {
        pid_t wait_result = wait4(pid, &status, WNOHANG, &usage);

        auto now = std::chrono::steady_clock::now();
        int elapsed_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - start_time
            ).count()
        );

        if (wait_result == pid) {
            break;
        }

        if (wait_result == -1) {
            close(exec_error_pipe[0]);
            g_cgroup_mgr.cleanup_run(cg_error);
            return make_system_error("wait4 failed for nsjail runner process");
        }

        // OLE check
        if (output_file_reached_limit(config.output_file, config.output_limit_mb)) {
            cause = TerminationCause::OutputLimit;
            // Kill via cgroup first, then process group
            g_cgroup_mgr.kill_run(cg_error);
            kill_process_group(pid);
            wait4(pid, &status, 0, &usage);
            break;
        }

        // TLE check
        if (elapsed_ms > config.time_limit_ms) {
            cause = TerminationCause::WallClockTimeout;
            g_cgroup_mgr.kill_run(cg_error);
            kill_process_group(pid);
            wait4(pid, &status, 0, &usage);
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    auto end_time = std::chrono::steady_clock::now();
    int final_time_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time
        ).count()
    );
    int memory_mb = rusage_memory_mb(usage);

    // ── Read cgroup events after ──────────────────────────
    CgroupV2EventSnapshot events_after;
    unsigned long long cgroup_peak = 0;
    {
        CgroupV2Stats stats_after;
        if (g_cgroup_mgr.read_stats(stats_after, cg_error)) {
            events_after = make_snapshot(stats_after);
            cgroup_peak = stats_after.memory_peak_bytes;
        }
    }

    CgroupV2EventSnapshot events_delta = delta(events_after, events_before);

    // ── Check if nsjail exec itself failed ────────────────
    char exec_err = 0;
    ssize_t n = read(exec_error_pipe[0], &exec_err, 1);
    close(exec_error_pipe[0]);

    if (n > 0) {
        g_cgroup_mgr.cleanup_run(cg_error);
        return make_system_error("Failed to execute nsjail for running phase");
    }

    // ── Build result ──────────────────────────────────────
    RunInfo info;
    info.time_ms = final_time_ms;
    info.memory_mb = memory_mb;
    info.termination_cause = cause;
    info.cgroup_memory_peak_bytes = cgroup_peak;

    if (WIFEXITED(status)) {
        info.exit_code = WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        info.signal = WTERMSIG(status);
    }

    // Classify verdict using cgroup events + termination cause
    info.result = RunResult::OK;
    classify_verdict(info, status, events_delta, cause);

    // ── Cleanup run cgroup ────────────────────────────────
    g_cgroup_mgr.cleanup_run(cg_error);
    // Cleanup failure does not change verdict — it's a resource leak warning

    return info;
}

static RunInfo run_program_isolate(
    const std::string& executable_file,
    const std::string& input_file,
    const std::string& output_file,
    int time_limit_ms,
    int memory_limit_mb,
    int output_limit_mb
) {
    (void)executable_file;
    (void)input_file;
    (void)output_file;
    (void)time_limit_ms;
    (void)memory_limit_mb;
    (void)output_limit_mb;

    std::ofstream error_file(output_file + ".err", std::ios::trunc);
    error_file << "Sandbox type not implemented: isolate\n";

    return make_system_error("Sandbox type not implemented: isolate");
}

RunInfo run_program(
    const std::string& executable_file,
    const std::string& input_file,
    const std::string& output_file,
    int time_limit_ms,
    int memory_limit_mb,
    int output_limit_mb
) {
    return run_program(
        executable_file,
        input_file,
        output_file,
        time_limit_ms,
        memory_limit_mb,
        output_limit_mb,
        DEFAULT_SANDBOX_TYPE
    );
}

RunInfo run_program(
    const std::string& executable_file,
    const std::string& input_file,
    const std::string& output_file,
    int time_limit_ms,
    int memory_limit_mb,
    int output_limit_mb,
    SandboxType sandbox_type
) {
    switch (sandbox_type) {
        case SandboxType::BUILTIN:
            return run_program_builtin(
                executable_file,
                input_file,
                output_file,
                time_limit_ms,
                memory_limit_mb,
                output_limit_mb
            );
        case SandboxType::NSJAIL:
            return run_program_nsjail(
                executable_file,
                input_file,
                output_file,
                time_limit_ms,
                memory_limit_mb,
                output_limit_mb
            );
        case SandboxType::ISOLATE:
            return run_program_isolate(
                executable_file,
                input_file,
                output_file,
                time_limit_ms,
                memory_limit_mb,
                output_limit_mb
            );
        default:
            return make_system_error(
                "Unknown sandbox type: " + sandbox_type_to_string(sandbox_type)
            );
    }
}
