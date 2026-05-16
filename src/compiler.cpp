#include "compiler.h"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

void create_parent_directory_if_needed(const std::string& file_path) {
    fs::path path(file_path);
    fs::path parent = path.parent_path();

    if (!parent.empty()) {
        fs::create_directories(parent);
    }
}

void kill_process_group(pid_t pid) {
    kill(-pid, SIGKILL);
    kill(pid, SIGKILL);
}

} // namespace

bool compile_cpp(
    const std::string& source_file,
    const std::string& executable_file,
    const std::string& error_file,
    int compile_time_limit_ms
) {
    create_parent_directory_if_needed(executable_file);
    create_parent_directory_if_needed(error_file);

    {
        int fd = open(error_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) {
            close(fd);
        }
    }

    std::vector<std::string> args = {
        "g++",
        source_file,
        "-o",
        executable_file,
        "-std=c++17",
        "-O2"
    };

    std::cout << "[Compile]";
    for (const auto& arg : args) {
        std::cout << ' ' << arg;
    }
    std::cout << " 2> " << error_file << std::endl;

    pid_t pid = fork();

    if (pid < 0) {
        std::ofstream error_file_stream(error_file, std::ios::app);
        error_file_stream << "Failed to fork compiler process\n";
        return false;
    }

    if (pid == 0) {
        setpgid(0, 0);

        int err_fd = open(error_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (err_fd < 0) {
            _exit(127);
        }

        dup2(err_fd, STDERR_FILENO);
        dup2(err_fd, STDOUT_FILENO);
        close(err_fd);

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);

        for (auto& arg : args) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        std::cout << "Failed to execute g++" << std::endl;
        _exit(127);
    }

    int status = 0;
    auto start_time = std::chrono::steady_clock::now();

    while (true) {
        pid_t wait_result = waitpid(pid, &status, WNOHANG);

        auto now = std::chrono::steady_clock::now();
        int elapsed_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - start_time
            ).count()
        );

        if (wait_result == pid) {
            break;
        }

        if (wait_result < 0) {
            return false;
        }

        if (compile_time_limit_ms > 0 && elapsed_ms > compile_time_limit_ms) {
            kill_process_group(pid);
            waitpid(pid, &status, 0);

            std::ofstream error_file_stream(error_file, std::ios::app);
            error_file_stream << "Compile Time Limit Exceeded: "
                              << elapsed_ms
                              << " ms > "
                              << compile_time_limit_ms
                              << " ms\n";
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
