#include "runner.h"
#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>
#include <chrono>
#include <thread>

std::string run_result_to_string(RunResult result) {
    switch (result) {
        case RunResult::OK: return "OK";
        case RunResult::TLE: return "TLE";
        case RunResult::MLE: return "MLE";
        case RunResult::OLE: return "OLE";
        case RunResult::RE: return "RE";
        default: return "UNKNOWN";
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

    if (pid < 0) return {RunResult::RE, 0, 0};

    if (pid == 0) {
        int input_fd = open(input_file.c_str(), O_RDONLY);
        if (input_fd < 0) _exit(1);

        int output_fd = open(output_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (output_fd < 0) _exit(1);

        dup2(input_fd, STDIN_FILENO);
        dup2(output_fd, STDOUT_FILENO);

        close(input_fd);
        close(output_fd);

        // 限制内存
        struct rlimit memory_limit;
        memory_limit.rlim_cur = memory_limit_mb * 1024LL * 1024LL;
        memory_limit.rlim_max = memory_limit_mb * 1024LL * 1024LL;
        setrlimit(RLIMIT_AS, &memory_limit);

        // 限制输出文件大小
        struct rlimit output_limit;
        output_limit.rlim_cur = output_limit_mb * 1024LL * 1024LL;
        output_limit.rlim_max = output_limit_mb * 1024LL * 1024LL;
        setrlimit(RLIMIT_FSIZE, &output_limit);

        execl(executable_file.c_str(), executable_file.c_str(), nullptr);
        _exit(1);
    }

    auto start_time = std::chrono::steady_clock::now();
    int status = 0;

    while (true) {
        pid_t wait_result = waitpid(pid, &status, WNOHANG);
        auto now = std::chrono::steady_clock::now();
        int elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

        if (wait_result == pid) break;
        if (wait_result == -1) return {RunResult::RE, elapsed_ms, 0};
        if (elapsed_ms > time_limit_ms) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            auto end_time = std::chrono::steady_clock::now();
            int final_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
            return {RunResult::TLE, final_time_ms, 0};
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    auto end_time = std::chrono::steady_clock::now();
    int final_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    // 获取内存使用情况
    struct rusage usage;
    getrusage(RUSAGE_CHILDREN, &usage);
    int memory_mb = usage.ru_maxrss / 1024; // ru_maxrss 单位 KB，转 MB

    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code == 0) return {RunResult::OK, final_time_ms, memory_mb};
        return {RunResult::RE, final_time_ms, memory_mb};
    }

    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        if (sig == SIGXFSZ) return {RunResult::OLE, final_time_ms, memory_mb};
        if (sig == SIGKILL) return {RunResult::TLE, final_time_ms, memory_mb};
        if (sig == SIGSEGV || sig == SIGABRT) return {RunResult::MLE, final_time_ms, memory_mb};
        return {RunResult::RE, final_time_ms, memory_mb};
    }

    return {RunResult::RE, final_time_ms, memory_mb};
}