#include "compiler.h"

#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

static void create_parent_directory_if_needed(const std::string& file_path) {
    fs::path path(file_path);
    fs::path parent = path.parent_path();

    if (!parent.empty()) {
        fs::create_directories(parent);
    }
}

bool compile_cpp(
    const std::string& source_file,
    const std::string& executable_file,
    const std::string& error_file
) {
    create_parent_directory_if_needed(executable_file);
    create_parent_directory_if_needed(error_file);

    // 先清空旧的编译错误文件
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
        return false;
    }

    if (pid == 0) {
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

        // execvp 失败才会执行到这里
        _exit(127);
    }

    int status = 0;

    if (waitpid(pid, &status, 0) < 0) {
        return false;
    }

    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}