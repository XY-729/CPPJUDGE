#include "compiler.h"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace {

static constexpr int COMPILE_AS_LIMIT_MB = 1024;
static constexpr int COMPILE_FSIZE_LIMIT_MB = 128;
static constexpr int COMPILE_NOFILE_LIMIT = 128;
static constexpr int COMPILE_NPROC_LIMIT = 64;

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

void create_empty_file(const std::string& file_path) {
    int fd = open(file_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        close(fd);
    }
}

int compile_time_limit_seconds(int compile_time_limit_ms) {
    return (compile_time_limit_ms + 999) / 1000 + 1;
}

std::string absolute_path(const fs::path& path) {
    try {
        return fs::weakly_canonical(path).string();
    } catch (const std::exception&) {
        return fs::absolute(path).string();
    }
}

void add_bindmount_if_exists(
    std::vector<std::string>& args,
    const std::string& flag,
    const fs::path& source,
    const std::string& destination
) {
    if (!fs::exists(source)) {
        return;
    }

    args.push_back(flag);
    args.push_back(absolute_path(source) + ":" + destination);
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

std::vector<std::string> base_gpp_args(
    const std::string& source_file,
    const std::string& executable_file
) {
    return {"g++", source_file, "-o", executable_file, "-std=c++17", "-O2"};
}

bool wait_for_compile_process(
    pid_t pid,
    const std::string& error_file,
    int compile_time_limit_ms
) {
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

bool compile_cpp_builtin(
    const std::string& source_file,
    const std::string& executable_file,
    const std::string& error_file,
    int compile_time_limit_ms
) {
    std::vector<std::string> args = base_gpp_args(source_file, executable_file);

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

    return wait_for_compile_process(pid, error_file, compile_time_limit_ms);
}

std::vector<std::string> build_nsjail_compile_args(
    const fs::path& run_dir,
    const fs::path& sandbox_root,
    int compile_time_limit_ms
) {
    int time_limit_seconds = compile_time_limit_seconds(compile_time_limit_ms);

    std::vector<std::string> args = {
        "nsjail",
        "-Mo",
        "--chroot",
        absolute_path(sandbox_root),
        "--cwd",
        "/work",
        "--disable_proc",
        "--env",
        "PATH=/usr/bin:/bin",
        "--env",
        "TMPDIR=/tmp",
        "--time_limit",
        std::to_string(time_limit_seconds),
        "--rlimit_as",
        std::to_string(COMPILE_AS_LIMIT_MB),
        "--rlimit_fsize",
        std::to_string(COMPILE_FSIZE_LIMIT_MB),
        "--rlimit_core",
        "0",
        "--rlimit_cpu",
        std::to_string(time_limit_seconds),
        "--rlimit_nofile",
        std::to_string(COMPILE_NOFILE_LIMIT),
        "--rlimit_nproc",
        std::to_string(COMPILE_NPROC_LIMIT)
    };

    args.push_back("--bindmount");
    args.push_back(absolute_path(run_dir) + ":/work");

    add_bindmount_if_exists(args, "--bindmount", run_dir / "compile_tmp", "/tmp");
    add_bindmount_if_exists(args, "--bindmount_ro", "/usr", "/usr");
    add_bindmount_if_exists(args, "--bindmount_ro", "/lib64", "/lib64");
    add_bindmount_if_exists(args, "--bindmount_ro", "/lib", "/lib");
    add_bindmount_if_exists(args, "--bindmount_ro", "/bin", "/bin");
    add_bindmount_if_exists(args, "--bindmount_ro", "/etc/alternatives", "/etc/alternatives");

    args.push_back("--");
    args.push_back("/usr/bin/g++");
    args.push_back("/work/submission.cpp");
    args.push_back("-o");
    args.push_back("/work/solution");
    args.push_back("-std=c++17");
    args.push_back("-O2");

    return args;
}

bool compile_cpp_nsjail(
    const std::string& source_file,
    const std::string& executable_file,
    const std::string& error_file,
    int compile_time_limit_ms
) {
    fs::path run_dir = fs::path(executable_file).parent_path();
    fs::path sandbox_root = run_dir / "compile_sandbox_root";
    fs::path compile_tmp = run_dir / "compile_tmp";
    fs::path sandbox_source_file = run_dir / "submission.cpp";

    try {
        fs::create_directories(run_dir);
        fs::create_directories(sandbox_root);
        fs::create_directories(compile_tmp);
        fs::copy_file(source_file, sandbox_source_file, fs::copy_options::overwrite_existing);
    } catch (const std::exception& exc) {
        std::ofstream error_file_stream(error_file, std::ios::app);
        error_file_stream << "Failed to prepare nsjail compile filesystem: "
                          << exc.what()
                          << "\n";
        return false;
    }

    std::vector<std::string> args = build_nsjail_compile_args(
        run_dir,
        sandbox_root,
        compile_time_limit_ms
    );

    std::cout << "[Compile/nsjail] " << join_args_for_log(args)
              << " 2> " << error_file << std::endl;

    pid_t pid = fork();

    if (pid < 0) {
        std::ofstream error_file_stream(error_file, std::ios::app);
        error_file_stream << "Failed to fork nsjail compiler process\n";
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

        std::string command_log = join_args_for_log(args);
        dprintf(STDERR_FILENO, "[nsjail compile] command: %s\n", command_log.c_str());

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (std::string& arg : args) {
            argv.push_back(arg.data());
        }
        argv.push_back(nullptr);

        execvp("nsjail", argv.data());
        dprintf(STDERR_FILENO, "Failed to execute nsjail for compile\n");
        _exit(127);
    }

    return wait_for_compile_process(pid, error_file, compile_time_limit_ms);
}

} // namespace

bool compile_cpp(
    const std::string& source_file,
    const std::string& executable_file,
    const std::string& error_file,
    int compile_time_limit_ms
) {
    return compile_cpp(
        source_file,
        executable_file,
        error_file,
        compile_time_limit_ms,
        SandboxType::BUILTIN
    );
}

bool compile_cpp(
    const std::string& source_file,
    const std::string& executable_file,
    const std::string& error_file,
    int compile_time_limit_ms,
    SandboxType sandbox_type
) {
    create_parent_directory_if_needed(executable_file);
    create_parent_directory_if_needed(error_file);
    create_empty_file(error_file);

    if (sandbox_type == SandboxType::NSJAIL) {
        return compile_cpp_nsjail(
            source_file,
            executable_file,
            error_file,
            compile_time_limit_ms
        );
    }

    return compile_cpp_builtin(
        source_file,
        executable_file,
        error_file,
        compile_time_limit_ms
    );
}
