#include "cli.h"

#include "version.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/utsname.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

CliOptions cli_error(const std::string& message) {
    std::cerr << "ERROR: " << message << "\n";
    std::cerr << "Try 'cppjudge judge --help' for usage.\n";
    CliOptions result;
    result.user_facing = true;
    result.exit_requested = true;
    result.exit_code = 2;
    return result;
}

bool take_value(
    int argc,
    char* argv[],
    int& index,
    const std::string& option,
    std::string& value,
    CliOptions& error_result
) {
    if (index + 1 >= argc) {
        error_result = cli_error("missing value for " + option);
        return false;
    }
    value = argv[++index];
    return true;
}

std::string read_file(const fs::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        return "";
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string find_executable_in_path(const std::string& executable) {
    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return "";
    }

    std::stringstream path_stream(path_env);
    std::string directory;
    while (std::getline(path_stream, directory, ':')) {
        if (directory.empty()) {
            continue;
        }
        fs::path candidate = fs::path(directory) / executable;
        if (access(candidate.c_str(), X_OK) == 0) {
            return candidate.string();
        }
    }
    return "";
}

std::string current_cgroup_path() {
    std::ifstream cgroup_file("/proc/self/cgroup");
    std::string line;
    while (std::getline(cgroup_file, line)) {
        const std::string prefix = "0::";
        if (line.rfind(prefix, 0) == 0) {
            return line.substr(prefix.size());
        }
    }
    return "unknown";
}

bool cgroup_controller_visible(const std::string& controllers, const std::string& name) {
    std::stringstream stream(controllers);
    std::string token;
    while (stream >> token) {
        if (token == name) {
            return true;
        }
    }
    return false;
}

bool can_create_current_cgroup_child(const std::string& cgroup_path, std::string& error) {
    fs::path base = "/sys/fs/cgroup";
    fs::path relative = cgroup_path == "/" ? fs::path() : fs::path(cgroup_path).relative_path();
    fs::path target = base / relative / ("cppjudge_doctor_" + std::to_string(getpid()));

    std::error_code ec;
    if (!fs::create_directory(target, ec)) {
        error = ec ? ec.message() : "create_directory returned false";
        return false;
    }

    fs::remove(target, ec);
    return true;
}

}  // namespace

void print_general_help() {
    std::cout
        << "Usage:\n"
        << "  cppjudge judge --problem <problem_dir> --submission <source.cpp> [options]\n"
        << "  cppjudge judge <source.cpp> --problem <problem_dir> [options]\n"
        << "  cppjudge doctor\n"
        << "  cppjudge --version\n"
        << "  cppjudge <submission> <problem> <time_ms> <memory_mb> <output_mb> "
           "<compare_mode> <compile_time_ms>  (legacy)\n\n"
        << "Commands:\n"
        << "  cppjudge judge      Judge one C++ submission\n"
        << "  cppjudge doctor     Check local product-sandbox readiness\n"
        << "  cppjudge version    Print version information\n\n"
        << "Required judge options:\n"
        << "  --problem DIR       Problem directory containing problem.json\n"
        << "  --submission FILE   C++ source file\n\n"
        << "Other options:\n"
        << "  --help              Show help\n"
        << "  --version           Show version\n";
}

void print_judge_help() {
    std::cout
        << "Usage: cppjudge judge --problem <problem_dir> --submission <source.cpp>\n"
        << "       cppjudge judge <source.cpp> --problem <problem_dir>\n\n"
        << "Limits are read from <problem_dir>/problem.json by default.\n"
        << "Optional overrides:\n"
        << "  --time-limit-ms N\n"
        << "  --memory-limit-mb N\n"
        << "  --output-limit-mb N\n"
        << "  --compare-mode exact|floating\n"
        << "  --compile-time-limit-ms N\n"
        << "  --sandbox-type builtin|nsjail|isolate\n"
        << "  --help\n";
}

int print_version() {
    std::cout << "cppjudge " << CPPJUDGE_VERSION
              << " (git: " << CPPJUDGE_GIT_COMMIT << ")\n";
    return 0;
}

int run_doctor() {
    std::string nsjail_path = find_executable_in_path("nsjail");
    bool nsjail_found = !nsjail_path.empty();

    bool cgroup_v2_detected = fs::exists("/sys/fs/cgroup/cgroup.controllers");
    std::string controllers = read_file("/sys/fs/cgroup/cgroup.controllers");
    bool memory_visible = cgroup_controller_visible(controllers, "memory");
    bool pids_visible = cgroup_controller_visible(controllers, "pids");

    fs::path seccomp_policy = fs::current_path() / "sandbox/seccomp/cppjudge-runtime.kafel";
    bool seccomp_policy_readable = fs::exists(seccomp_policy) &&
                                   fs::is_regular_file(seccomp_policy) &&
                                   access(seccomp_policy.c_str(), R_OK) == 0;

    std::string cgroup_path = current_cgroup_path();

    struct utsname uts {};
    std::string platform = "unknown";
    if (uname(&uts) == 0) {
        platform = std::string(uts.sysname) + " " + uts.release + " " + uts.machine;
    }

    std::string delegation_error;
    bool delegated_writable = cgroup_v2_detected &&
                              can_create_current_cgroup_child(cgroup_path, delegation_error);

    bool dependencies_ready = nsjail_found &&
                              cgroup_v2_detected &&
                              memory_visible &&
                              pids_visible &&
                              seccomp_policy_readable;

    std::string readiness = "NOT_READY";
    int exit_code = 2;
    if (dependencies_ready && delegated_writable) {
        readiness = "READY";
        exit_code = 0;
    } else if (dependencies_ready && !delegated_writable) {
        readiness = "NOT_VERIFIED";
        exit_code = 2;
    }

    std::cout << "cppjudge doctor\n";
    std::cout << "cppjudge version: " << CPPJUDGE_VERSION << "\n";
    std::cout << "git commit: " << CPPJUDGE_GIT_COMMIT << "\n";
    std::cout << "platform: " << platform << "\n";
    std::cout << "current working directory: " << fs::current_path().string() << "\n";
    std::cout << "nsjail: " << (nsjail_found ? "found " + nsjail_path : "not found") << "\n";
    std::cout << "cgroup v2: " << (cgroup_v2_detected ? "detected" : "not detected") << "\n";
    std::cout << "current cgroup path: " << cgroup_path << "\n";
    std::cout << "memory controller: " << (memory_visible ? "visible" : "not visible") << "\n";
    std::cout << "pids controller: " << (pids_visible ? "visible" : "not visible") << "\n";
    std::cout << "seccomp policy: " << (seccomp_policy_readable ? "readable" : "not readable")
              << " (" << seccomp_policy.string() << ")\n";
    std::cout << "delegated cgroup write check: "
              << (delegated_writable ? "writable" : "not writable") << "\n";
    if (!delegated_writable && !delegation_error.empty()) {
        std::cout << "delegated cgroup write detail: " << delegation_error << "\n";
    }
    std::cout << "product sandbox readiness: " << readiness << "\n";

    return exit_code;
}

CliOptions parse_cli(int argc, char* argv[]) {
    CliOptions result;

    if (argc >= 2 && std::string(argv[1]) == "--help") {
        print_general_help();
        result.exit_requested = true;
        return result;
    }

    if (argc >= 2 && (std::string(argv[1]) == "--version" || std::string(argv[1]) == "version")) {
        result.command = CliCommand::Version;
        return result;
    }

    if (argc >= 2 && std::string(argv[1]) == "doctor") {
        result.command = CliCommand::Doctor;
        return result;
    }

    if (argc < 2 || std::string(argv[1]) != "judge") {
        return result;
    }

    result.user_facing = true;

    for (int index = 2; index < argc; ++index) {
        std::string argument = argv[index];

        if (argument == "--help") {
            print_judge_help();
            result.exit_requested = true;
            return result;
        }

        std::string value;
        if (argument == "--problem") {
            if (!take_value(argc, argv, index, argument, value, result)) return result;
            result.problem_dir = value;
        } else if (argument == "--submission") {
            if (!take_value(argc, argv, index, argument, value, result)) return result;
            result.submission_file = value;
        } else if (argument == "--time-limit-ms") {
            if (!take_value(argc, argv, index, argument, value, result)) return result;
            result.time_limit_ms = value;
        } else if (argument == "--memory-limit-mb") {
            if (!take_value(argc, argv, index, argument, value, result)) return result;
            result.memory_limit_mb = value;
        } else if (argument == "--output-limit-mb") {
            if (!take_value(argc, argv, index, argument, value, result)) return result;
            result.output_limit_mb = value;
        } else if (argument == "--compare-mode") {
            if (!take_value(argc, argv, index, argument, value, result)) return result;
            result.compare_mode = value;
        } else if (argument == "--compile-time-limit-ms") {
            if (!take_value(argc, argv, index, argument, value, result)) return result;
            result.compile_time_limit_ms = value;
        } else if (argument == "--sandbox-type") {
            if (!take_value(argc, argv, index, argument, value, result)) return result;
            result.sandbox_type = value;
        } else if (!argument.empty() && argument[0] == '-') {
            return cli_error("unknown option: " + argument);
        } else if (result.submission_file.empty()) {
            result.submission_file = argument;
        } else {
            return cli_error("unexpected positional argument: " + argument);
        }
    }

    if (result.problem_dir.empty()) {
        return cli_error("missing required --problem");
    }
    if (result.submission_file.empty()) {
        return cli_error("missing required --submission");
    }

    return result;
}
