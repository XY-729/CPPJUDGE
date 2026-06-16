#include "compiler.h"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <cstring>
#include <cerrno>
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

// Returns: 0 = process finished ok, 1 = exit code non-zero (CE), -1 = timeout, -2 = waitpid error
int wait_for_compile_process_ex(
    pid_t pid,
    const std::string& error_file,
    int compile_time_limit_ms,
    int& elapsed_ms_out
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
        elapsed_ms_out = elapsed_ms;

        if (wait_result == pid) {
            return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
        }

        if (wait_result < 0) {
            return -2;
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
            return -1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

CompileInfo compile_cpp_builtin_structured(
    const std::string& source_file,
    const std::string& executable_file,
    const std::string& error_file,
    int compile_time_limit_ms
) {
    CompileInfo info;

    std::vector<std::string> args = base_gpp_args(source_file, executable_file);

    std::cout << "[Compile]";
    for (const auto& arg : args) {
        std::cout << ' ' << arg;
    }
    std::cout << " 2> " << error_file << std::endl;

    int exec_error_pipe[2];
    if (pipe2(exec_error_pipe, O_CLOEXEC) < 0) {
        info.result = CompileResult::SE;
        info.system_error = true;
        info.error_message = "Failed to create exec-error pipe for compiler";
        return info;
    }


    // Pre-open compile error file in parent
    int err_fd = open(error_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (err_fd < 0) {
        close(exec_error_pipe[0]);
        close(exec_error_pipe[1]);
        info.result = CompileResult::SE;
        info.system_error = true;
        info.error_message = "Failed to open compile error file: " + error_file + " - " + std::string(strerror(errno));
        return info;
    }

    pid_t pid = fork();

    if (pid < 0) {
        close(exec_error_pipe[0]);
        close(exec_error_pipe[1]);
        info.result = CompileResult::SE;
        info.system_error = true;
        close(err_fd);
        info.error_message = "Failed to fork compiler process";
        return info;
    }

    if (pid == 0) {
        setpgid(0, 0);
        close(exec_error_pipe[0]);


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

        // exec failed
        dprintf(STDERR_FILENO, "Failed to execute g++\n");
        char err = 1;
        ssize_t written = write(exec_error_pipe[1], &err, 1);
        (void)written;
        close(exec_error_pipe[1]);
        _exit(127);
    }

    close(exec_error_pipe[1]);
    close(err_fd);

    int elapsed_ms = 0;
    int wait_rc = wait_for_compile_process_ex(pid, error_file, compile_time_limit_ms, elapsed_ms);

    // Check for exec failure first
    char exec_err = 0;
    ssize_t n = read(exec_error_pipe[0], &exec_err, 1);
    close(exec_error_pipe[0]);

    if (n > 0) {
        info.result = CompileResult::SE;
        info.system_error = true;
        info.error_message = "Failed to execute g++";
        return info;
    }

    if (wait_rc == -2) {
        info.result = CompileResult::SE;
        info.system_error = true;
        info.error_message = "waitpid failed for compiler process";
        return info;
    }

    if (wait_rc == 0) {
        info.result = CompileResult::OK;
        return info;
    }

    // wait_rc == 1 (non-zero exit) or -1 (timeout): both are CE for user code
    info.result = CompileResult::CE;
    return info;
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

CompileInfo compile_cpp_nsjail_structured(
    const std::string& source_file,
    const std::string& executable_file,
    const std::string& error_file,
    int compile_time_limit_ms
) {
    CompileInfo info;

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
        info.result = CompileResult::SE;
        info.system_error = true;
        info.error_message = "Failed to prepare nsjail compile filesystem: "
                             + std::string(exc.what());
        return info;
    }

    std::vector<std::string> args = build_nsjail_compile_args(
        run_dir,
        sandbox_root,
        compile_time_limit_ms
    );

    std::cout << "[Compile/nsjail] " << join_args_for_log(args)
              << " 2> " << error_file << std::endl;

    int exec_error_pipe[2];
    if (pipe2(exec_error_pipe, O_CLOEXEC) < 0) {
        info.result = CompileResult::SE;
        info.system_error = true;
        info.error_message = "Failed to create exec-error pipe for nsjail compiler";
        return info;
    }


    // Pre-open compile error file in parent
    int err_fd = open(error_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (err_fd < 0) {
        close(exec_error_pipe[0]);
        close(exec_error_pipe[1]);
        info.result = CompileResult::SE;
        info.system_error = true;
        info.error_message = "Failed to open compile error file: " + error_file + " - " + std::string(strerror(errno));
        return info;
    }

    pid_t pid = fork();

    if (pid < 0) {
        close(exec_error_pipe[0]);
        close(exec_error_pipe[1]);
        info.result = CompileResult::SE;
        info.system_error = true;
        close(err_fd);
        info.error_message = "Failed to fork nsjail compiler process";
        return info;
    }

    if (pid == 0) {
        setpgid(0, 0);
        close(exec_error_pipe[0]);


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
        char err = 1;
        ssize_t written = write(exec_error_pipe[1], &err, 1);
        (void)written;
        close(exec_error_pipe[1]);
        _exit(127);
    }

    close(exec_error_pipe[1]);
    close(err_fd);

    int elapsed_ms = 0;
    int wait_rc = wait_for_compile_process_ex(pid, error_file, compile_time_limit_ms, elapsed_ms);

    char exec_err = 0;
    ssize_t n = read(exec_error_pipe[0], &exec_err, 1);
    close(exec_error_pipe[0]);

    if (n > 0) {
        info.result = CompileResult::SE;
        info.system_error = true;
        info.error_message = "Failed to execute nsjail for compile phase";
        return info;
    }

    if (wait_rc == -2) {
        info.result = CompileResult::SE;
        info.system_error = true;
        info.error_message = "waitpid failed for nsjail compiler process";
        return info;
    }

    if (wait_rc == 0) {
        info.result = CompileResult::OK;
        return info;
    }

    info.result = CompileResult::CE;
    return info;
}

} // namespace

CompileInfo compile_cpp_structured(
    const std::string& source_file,
    const std::string& executable_file,
    const std::string& error_file,
    int compile_time_limit_ms,
    SandboxType sandbox_type
) {
    try {
        create_parent_directory_if_needed(executable_file);
        create_parent_directory_if_needed(error_file);
        create_empty_file(error_file);
    } catch (const std::exception& exc) {
        CompileInfo info;
        info.result = CompileResult::SE;
        info.system_error = true;
        info.error_message = "Filesystem error during compile setup: " + std::string(exc.what());
        return info;
    }

    switch (sandbox_type) {
        case SandboxType::BUILTIN:
            return compile_cpp_builtin_structured(
                source_file,
                executable_file,
                error_file,
                compile_time_limit_ms
            );
        case SandboxType::NSJAIL:
            return compile_cpp_nsjail_structured(
                source_file,
                executable_file,
                error_file,
                compile_time_limit_ms
            );
        case SandboxType::ISOLATE: {
            CompileInfo info;
            info.result = CompileResult::SE;
            info.system_error = true;
            info.error_message = "Sandbox type not implemented for compile: isolate";
            return info;
        }
        default: {
            CompileInfo info;
            info.result = CompileResult::SE;
            info.system_error = true;
            info.error_message = "Unknown sandbox type for compile: " + sandbox_type_to_string(sandbox_type);
            return info;
        }
    }
}

// Legacy wrappers - kept for backward compatibility
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
    CompileInfo info = compile_cpp_structured(
        source_file,
        executable_file,
        error_file,
        compile_time_limit_ms,
        sandbox_type
    );
    return info.result == CompileResult::OK;
}
