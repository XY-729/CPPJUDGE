#include "runner.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdlib>
#include <cstdio>
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

// The BUILTIN sandbox is a lightweight runner for local development and
// teaching. It provides stdin/stdout/stderr redirection, time limits, memory
// checks, output size limits, process-group cleanup, and basic rlimit usage.
// It is not a complete security sandbox: it does not provide namespaces,
// chroot/pivot_root, seccomp, cgroups, network isolation, or a dedicated
// low-privilege user. Do not expose it directly to the public internet or run
// arbitrary untrusted submissions with it.

#if defined(__linux__)
extern char** environ;
#endif

namespace {

static constexpr SandboxType DEFAULT_SANDBOX_TYPE = SandboxType::BUILTIN;
static constexpr int BUILTIN_NOFILE_LIMIT = 64;
static constexpr int BUILTIN_NPROC_LIMIT = 16;

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
        // RLIMIT_AS 限制的是虚拟地址空间，所以这里读 VmSize，不读 VmRSS
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
    // Linux 下 ru_maxrss 单位是 KB
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

void close_extra_file_descriptors() {
    for (int fd = STDERR_FILENO + 1; fd < BUILTIN_NOFILE_LIMIT; ++fd) {
        close(fd);
    }
}

void clear_child_environment() {
#if defined(__linux__)
    // clearenv() is not consistently declared by all standard library modes.
    // Resetting environ keeps the child from inheriting PATH and unrelated
    // environment variables; execl uses an explicit executable path here.
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
    // RLIMIT_NPROC is per real user ID on Linux and may be ignored for root or
    // affected by the account already running other processes. It is a basic
    // fork-bomb guard for local testing, not a replacement for cgroups.
    set_limit_or_exit(RLIMIT_NPROC, BUILTIN_NPROC_LIMIT);
#endif

    int cpu_seconds = (time_limit_ms + 999) / 1000 + 1;
    set_limit_or_exit(RLIMIT_CPU, cpu_seconds);
}

int nsjail_time_limit_seconds(int time_limit_ms) {
    return (time_limit_ms + 999) / 1000 + 1;
}

int nsjail_address_space_limit_mb(int memory_limit_mb) {
    // Dynamically linked C++ programs need startup headroom, but nsjail should
    // still trip memory-heavy submissions promptly in the MVP tests. cgroup v2
    // will be the better long-term memory counter.
    static constexpr int NSJAIL_AS_HEADROOM_MB = 64;
    return memory_limit_mb + NSJAIL_AS_HEADROOM_MB;
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
    // First isolation pass: run inside a per-run chroot and expose only the
    // compiled solution, the output directory, and a small set of dynamic-linker
    // files needed by ordinary C++ binaries on Rocky Linux.
    // TODO: generate this library list from a trusted dependency scanner or
    // provide a managed minimal rootfs.
    // TODO: add cgroup memory limit for stronger MLE enforcement.
    // TODO: add seccomp policy.
    // TODO: add explicit low-privilege user mapping.
    std::vector<std::string> args = {
        "nsjail",
        "-Mo",
        "--chroot",
        absolute_path_for_nsjail(config.sandbox_root_dir),
        "--cwd",
        "/sandbox",
        "--disable_proc",
        // Keep nsjail's default network namespace isolation. Do not pass
        // --disable_clone_newnet, because that disables the network namespace.
        // scripts/run_nsjail_tests.sh verifies connect() fails in this mode.
        "--time_limit",
        std::to_string(nsjail_time_limit_seconds(config.time_limit_ms)),
        "--rlimit_as",
        std::to_string(nsjail_address_space_limit_mb(config.memory_limit_mb)),
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

std::string read_file_to_string(const std::string& file_path) {
    std::ifstream file(file_path);

    if (!file.is_open()) {
        return "";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool text_contains_case_insensitive(const std::string& text, const std::string& keyword) {
    return to_lower(text).find(to_lower(keyword)) != std::string::npos;
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
    // 先杀进程组，防止用户程序 fork 子进程逃逸
    kill(-pid, SIGKILL);

    // 兜底再杀主进程
    kill(pid, SIGKILL);
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
        return true;
    }

    if (type == SandboxType::ISOLATE) {
        return true;
    }

    error = "Unknown sandbox type: " + sandbox_type_to_string(type);
    return false;
}

static RunInfo run_program_builtin(
    const std::string& executable_file,
    const std::string& input_file,
    const std::string& output_file,
    int time_limit_ms,
    int memory_limit_mb,
    int output_limit_mb
) {
    pid_t pid = fork();

    if (pid < 0) {
        return {RunResult::RE, 0, 0};
    }

    if (pid == 0) {
        setpgid(0, 0);

        int input_fd = open(input_file.c_str(), O_RDONLY);
        if (input_fd < 0) {
            _exit(1);
        }

        int output_fd = open(output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (output_fd < 0) {
            close(input_fd);
            _exit(1);
        }

        std::string error_file = output_file + ".err";
        int error_fd = open(error_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);

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

        close_extra_file_descriptors();
        clear_child_environment();
        apply_builtin_child_limits(time_limit_ms);

        if (memory_limit_mb > 0) {
            // 不能直接把 RLIMIT_AS 设成题目的内存限制。
            // 动态链接的 C++ 程序启动时需要额外虚拟地址空间；
            // 这种限制太小会导致 execl / 动态链接阶段失败，最后被误判成 RE。
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

        _exit(1);
    }

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
            return {RunResult::RE, elapsed_ms, 0};
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

            return {RunResult::MLE, final_time_ms, memory_mb};
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

            return {RunResult::TLE, final_time_ms, memory_mb};
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

    std::string error_file = output_file + ".err";

    bool bad_alloc_error =
        file_contains(error_file, "bad_alloc") ||
        file_contains(error_file, "Cannot allocate memory") ||
        file_contains(error_file, "cannot allocate memory");

    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);

        if (exit_code == 0) {
            return {RunResult::OK, final_time_ms, memory_mb};
        }

        if (bad_alloc_error || reached_memory_limit(memory_mb, memory_limit_mb)) {
            return {RunResult::MLE, final_time_ms, memory_mb};
        }

        return {RunResult::RE, final_time_ms, memory_mb};
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);

        if (sig == SIGXFSZ) {
            return {RunResult::OLE, final_time_ms, memory_mb};
        }

        if (sig == SIGXCPU) {
            return {RunResult::TLE, final_time_ms, memory_mb};
        }

        if (sig == SIGKILL) {
            if (reached_memory_limit(memory_mb, memory_limit_mb)) {
                return {RunResult::MLE, final_time_ms, memory_mb};
            }

            return {RunResult::TLE, final_time_ms, memory_mb};
        }

        if (bad_alloc_error || reached_memory_limit(memory_mb, memory_limit_mb)) {
            return {RunResult::MLE, final_time_ms, memory_mb};
        }

        return {RunResult::RE, final_time_ms, memory_mb};
    }

    return {RunResult::RE, final_time_ms, memory_mb};
}

static RunInfo run_program_nsjail(
    const std::string& executable_file,
    const std::string& input_file,
    const std::string& output_file,
    int time_limit_ms,
    int memory_limit_mb,
    int output_limit_mb
) {
    SandboxRunConfig config = make_sandbox_run_config(
        executable_file,
        input_file,
        output_file,
        time_limit_ms,
        memory_limit_mb,
        output_limit_mb
    );

    std::string prepare_error;
    if (!prepare_nsjail_filesystem(config, prepare_error)) {
        std::ofstream error_stream(config.error_file, std::ios::app);
        error_stream << prepare_error << "\n";
        return {RunResult::RE, 0, 0};
    }

    pid_t pid = fork();

    if (pid < 0) {
        std::ofstream error_stream(config.error_file, std::ios::app);
        error_stream << "Failed to fork nsjail runner process\n";
        return {RunResult::RE, 0, 0};
    }

    if (pid == 0) {
        setpgid(0, 0);

        int input_fd = open(config.input_file.c_str(), O_RDONLY);
        if (input_fd < 0) {
            _exit(1);
        }

        int output_fd = open(config.output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (output_fd < 0) {
            close(input_fd);
            _exit(1);
        }

        int error_fd = open(config.error_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (error_fd < 0) {
            close(input_fd);
            close(output_fd);
            _exit(1);
        }

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
        dprintf(STDERR_FILENO, "Failed to execute nsjail\n");
        _exit(127);
    }

    auto start_time = std::chrono::steady_clock::now();

    int status = 0;
    struct rusage usage {};

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
            return {RunResult::RE, elapsed_ms, 0};
        }

        if (output_file_reached_limit(config.output_file, config.output_limit_mb)) {
            kill_process_group(pid);
            wait4(pid, &status, 0, &usage);

            auto end_time = std::chrono::steady_clock::now();
            int final_time_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time
                ).count()
            );
            int memory_mb = rusage_memory_mb(usage);

            return {RunResult::OLE, final_time_ms, memory_mb};
        }

        if (elapsed_ms > config.time_limit_ms) {
            kill_process_group(pid);
            wait4(pid, &status, 0, &usage);

            auto end_time = std::chrono::steady_clock::now();
            int final_time_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time
                ).count()
            );
            int memory_mb = rusage_memory_mb(usage);

            if (output_file_reached_limit(config.output_file, config.output_limit_mb)) {
                return {RunResult::OLE, final_time_ms, memory_mb};
            }

            return {RunResult::TLE, final_time_ms, memory_mb};
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
    std::string stderr_content = read_file_to_string(config.error_file);

    bool stderr_says_tle =
        text_contains_case_insensitive(stderr_content, "time limit") ||
        text_contains_case_insensitive(stderr_content, "timed out");
    bool stderr_says_mle =
        text_contains_case_insensitive(stderr_content, "memory") ||
        text_contains_case_insensitive(stderr_content, "oom") ||
        text_contains_case_insensitive(stderr_content, "bad_alloc");
    bool stderr_says_ole =
        text_contains_case_insensitive(stderr_content, "File size limit exceeded") ||
        output_file_reached_limit(config.output_file, config.output_limit_mb);

    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);

        if (exit_code == 0) {
            return {RunResult::OK, final_time_ms, memory_mb};
        }

        if (exit_code == 127 && stderr_content.find("Failed to execute nsjail") != std::string::npos) {
            return {RunResult::RE, final_time_ms, memory_mb};
        }

        if (stderr_says_tle) {
            return {RunResult::TLE, final_time_ms, memory_mb};
        }

        if (stderr_says_mle) {
            return {RunResult::MLE, final_time_ms, memory_mb};
        }

        if (stderr_says_ole) {
            return {RunResult::OLE, final_time_ms, memory_mb};
        }

        return {RunResult::RE, final_time_ms, memory_mb};
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);

        if (sig == SIGXFSZ) {
            return {RunResult::OLE, final_time_ms, memory_mb};
        }

        if (sig == SIGKILL) {
            if (final_time_ms > config.time_limit_ms) {
                return {RunResult::TLE, final_time_ms, memory_mb};
            }

            return {RunResult::RE, final_time_ms, memory_mb};
        }

        return {RunResult::RE, final_time_ms, memory_mb};
    }

    return {RunResult::RE, final_time_ms, memory_mb};
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

    // TODO: integrate isolate sandbox runner.
    return {RunResult::RE, 0, 0};
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
            return run_program_builtin(
                executable_file,
                input_file,
                output_file,
                time_limit_ms,
                memory_limit_mb,
                output_limit_mb
            );
    }
}
