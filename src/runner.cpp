#include "runner.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {

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

void set_limit_or_exit(int resource, long long bytes) {
    struct rlimit limit {};

    limit.rlim_cur = static_cast<rlim_t>(bytes);
    limit.rlim_max = static_cast<rlim_t>(bytes);

    if (setrlimit(resource, &limit) != 0) {
        _exit(1);
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

RunInfo run_program(
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

        if (memory_limit_mb > 0) {
            long long memory_bytes = 1LL * memory_limit_mb * 1024 * 1024;
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