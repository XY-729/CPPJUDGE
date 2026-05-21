#include "judge.h"

#include "compiler.h"
#include "comparer.h"
#include "config.h"
#include "runner.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

#include "json.hpp"
#include <unistd.h>

namespace fs = std::filesystem;
using json = nlohmann::json;

enum class FinalVerdict {
    AC,
    WA,
    TLE,
    MLE,
    OLE,
    RE,
    SE,
    CE
};

struct ProblemConfig {
    std::string title = PROBLEM_NAME;
    int time_limit_ms = TIME_LIMIT_MS;
    int memory_limit_mb = MEMORY_LIMIT_MB;
    int output_limit_mb = OUTPUT_LIMIT_MB;
    int compile_time_limit_ms = COMPILE_TIME_LIMIT_MS;
    CompareMode compare_mode = CompareMode::FLOATING;
    SandboxType sandbox_type = SandboxType::BUILTIN;
    double float_abs_eps = FLOAT_ABS_EPS;
    double float_rel_eps = FLOAT_REL_EPS;
};

static std::string final_verdict_to_string(FinalVerdict verdict) {
    switch (verdict) {
        case FinalVerdict::AC:
            return "Accepted";
        case FinalVerdict::WA:
            return "Wrong Answer";
        case FinalVerdict::TLE:
            return "Time Limit Exceeded";
        case FinalVerdict::MLE:
            return "Memory Limit Exceeded";
        case FinalVerdict::OLE:
            return "Output Limit Exceeded";
        case FinalVerdict::RE:
            return "Runtime Error";
        case FinalVerdict::SE:
            return "System Error";
        case FinalVerdict::CE:
            return "Compile Error";
        default:
            return "Unknown";
    }
}

static FinalVerdict run_result_to_final_verdict(RunResult result) {
    switch (result) {
        case RunResult::TLE:
            return FinalVerdict::TLE;
        case RunResult::MLE:
            return FinalVerdict::MLE;
        case RunResult::OLE:
            return FinalVerdict::OLE;
        case RunResult::RE:
            return FinalVerdict::RE;
        case RunResult::OK:
            return FinalVerdict::AC;
        default:
            return FinalVerdict::RE;
    }
}

static std::string read_text_file(const std::string& file_path);

static bool is_sandbox_system_error(
    const ProblemConfig& problem_config,
    const RunInfo& run_info,
    const std::string& user_error_file
) {
    if (problem_config.sandbox_type == SandboxType::BUILTIN ||
        run_info.result != RunResult::RE) {
        return false;
    }

    std::string stderr_content = read_text_file(user_error_file);

    if (problem_config.sandbox_type == SandboxType::NSJAIL) {
        return stderr_content.find("Failed to execute nsjail") != std::string::npos ||
               stderr_content.find("Failed to prepare nsjail filesystem") != std::string::npos;
    }

    if (problem_config.sandbox_type == SandboxType::ISOLATE) {
        return stderr_content.find("Sandbox type not implemented: isolate") != std::string::npos;
    }

    return false;
}

static std::string read_text_file(const std::string& file_path) {
    std::ifstream file(file_path);

    if (!file.is_open()) {
        return "";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    return buffer.str();
}

static bool load_string_config_field(
    const json& problem_json,
    const std::string& name,
    std::string& target,
    std::string& error
) {
    if (!problem_json.contains(name)) {
        return true;
    }

    const json& value = problem_json.at(name);
    if (!value.is_string()) {
        error = "Invalid problem.json field " + name + ": expected string";
        return false;
    }

    target = value.get<std::string>();
    return true;
}

static bool load_positive_int_config_field(
    const json& problem_json,
    const std::string& name,
    int& target,
    std::string& error
) {
    if (!problem_json.contains(name)) {
        return true;
    }

    const json& value = problem_json.at(name);
    if (!value.is_number_integer()) {
        error = "Invalid problem.json field " + name + ": expected positive integer";
        return false;
    }

    long long parsed_value = value.get<long long>();
    if (parsed_value <= 0) {
        error = "Invalid problem.json field " + name + ": must be positive";
        return false;
    }

    if (parsed_value > std::numeric_limits<int>::max()) {
        error = "Invalid problem.json field " + name + ": value is too large";
        return false;
    }

    target = static_cast<int>(parsed_value);
    return true;
}

static bool load_nonnegative_double_config_field(
    const json& problem_json,
    const std::string& name,
    double& target,
    std::string& error
) {
    if (!problem_json.contains(name)) {
        return true;
    }

    const json& value = problem_json.at(name);
    if (!value.is_number()) {
        error = "Invalid problem.json field " + name + ": expected non-negative number";
        return false;
    }

    double parsed_value = value.get<double>();
    if (parsed_value < 0) {
        error = "Invalid problem.json field " + name + ": must be non-negative";
        return false;
    }

    target = parsed_value;
    return true;
}

static bool validate_problem_config(
    const ProblemConfig& config,
    std::string& error
) {
    if (config.time_limit_ms <= 0) {
        error = "Invalid problem config: time_limit_ms must be positive";
        return false;
    }

    if (config.memory_limit_mb <= 0) {
        error = "Invalid problem config: memory_limit_mb must be positive";
        return false;
    }

    if (config.output_limit_mb <= 0) {
        error = "Invalid problem config: output_limit_mb must be positive";
        return false;
    }

    if (config.compile_time_limit_ms <= 0) {
        error = "Invalid problem config: compile_time_limit_ms must be positive";
        return false;
    }

    if (config.float_abs_eps < 0) {
        error = "Invalid problem config: float_abs_eps must be non-negative";
        return false;
    }

    if (config.float_rel_eps < 0) {
        error = "Invalid problem config: float_rel_eps must be non-negative";
        return false;
    }

    return true;
}

static ProblemConfig load_problem_config(
    const std::string& problem_dir,
    std::string& error
) {
    ProblemConfig config;
    fs::path config_path = fs::path(problem_dir) / "problem.json";

    if (!fs::exists(config_path)) {
        return config;
    }

    std::ifstream config_file(config_path);
    if (!config_file.is_open()) {
        error = "Failed to open problem.json: " + config_path.string();
        return config;
    }

    try {
        json problem_json;
        config_file >> problem_json;

        if (!problem_json.is_object()) {
            error = "Invalid problem.json: expected JSON object";
            return config;
        }

        if (!load_string_config_field(problem_json, "title", config.title, error) ||
            !load_positive_int_config_field(problem_json, "time_limit_ms", config.time_limit_ms, error) ||
            !load_positive_int_config_field(problem_json, "memory_limit_mb", config.memory_limit_mb, error) ||
            !load_positive_int_config_field(problem_json, "output_limit_mb", config.output_limit_mb, error) ||
            !load_positive_int_config_field(problem_json, "compile_time_limit_ms", config.compile_time_limit_ms, error) ||
            !load_nonnegative_double_config_field(problem_json, "float_abs_eps", config.float_abs_eps, error) ||
            !load_nonnegative_double_config_field(problem_json, "float_rel_eps", config.float_rel_eps, error)) {
            return config;
        }

        if (problem_json.contains("compare_mode")) {
            const json& compare_mode_value = problem_json.at("compare_mode");
            if (!compare_mode_value.is_string()) {
                error = "Invalid problem.json field compare_mode: expected string";
                return config;
            }

            std::string compare_mode = compare_mode_value.get<std::string>();
            if (!is_valid_compare_mode(compare_mode)) {
                error = "Invalid compare_mode in problem.json: " + compare_mode +
                        ", expected exact or floating";
                return config;
            }
            config.compare_mode = compare_mode_from_string(compare_mode);
        }

        if (problem_json.contains("sandbox_type")) {
            const json& sandbox_type_value = problem_json.at("sandbox_type");
            if (!sandbox_type_value.is_string()) {
                error = "Invalid problem.json field sandbox_type: expected string";
                return config;
            }

            std::string sandbox_type = sandbox_type_value.get<std::string>();
            if (!is_valid_sandbox_type(sandbox_type)) {
                error = "Invalid sandbox_type in problem.json: " + sandbox_type +
                        ", expected builtin, nsjail, or isolate";
                return config;
            }
            config.sandbox_type = sandbox_type_from_string(sandbox_type);
        }

        validate_problem_config(config, error);
    } catch (const std::exception& e) {
        error = "Invalid problem.json: " + std::string(e.what());
    }

    return config;
}

static std::string make_run_id() {
    auto now = std::chrono::system_clock::now();
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    std::tm local_time {};
    localtime_r(&now_time, &local_time);

    std::ostringstream ss;
    ss << std::put_time(&local_time, "%Y%m%d_%H%M%S")
       << "_"
       << getpid();

    return ss.str();
}

static void write_log_file(
    const std::string& judge_log_file,
    const json& log_json
) {
    fs::create_directories(BUILD_DIR);

    std::ofstream run_log_file(judge_log_file);
    run_log_file << std::setw(4) << log_json << std::endl;

    std::ofstream latest_log_file(JUDGE_LOG_FILE);
    latest_log_file << std::setw(4) << log_json << std::endl;
}

static bool parse_int_arg(
    const char* value,
    const std::string& name,
    int& target,
    std::string& error
) {
    try {
        size_t parsed_chars = 0;
        int parsed_value = std::stoi(value, &parsed_chars);

        if (parsed_chars != std::string(value).size()) {
            error = "Invalid integer for " + name + ": " + value;
            return false;
        }

        if (parsed_value <= 0) {
            error = "Invalid integer for " + name + ": " + value + ", must be positive";
            return false;
        }

        target = parsed_value;
        return true;
    } catch (const std::exception&) {
        error = "Invalid integer for " + name + ": " + value;
        return false;
    }
}

void judge(int argc, char* argv[]) {
    std::string submission_file = SUBMISSION_FILE;
    std::string problem_dir = PROBLEM_DIR;

    if (argc > 1) {
        submission_file = argv[1];
    }

    if (argc > 2) {
        problem_dir = argv[2];
    }

    std::string problem_config_error;
    std::string argument_error;
    ProblemConfig problem_config = load_problem_config(problem_dir, problem_config_error);

    if (problem_config_error.empty() && argc > 3 && !parse_int_arg(
        argv[3],
        "time_limit_ms",
        problem_config.time_limit_ms,
        argument_error
    )) {
        // handled after log_json is initialized
    }

    if (problem_config_error.empty() && argument_error.empty() && argc > 4 && !parse_int_arg(
        argv[4],
        "memory_limit_mb",
        problem_config.memory_limit_mb,
        argument_error
    )) {
        // handled after log_json is initialized
    }

    if (problem_config_error.empty() && argument_error.empty() && argc > 5 && !parse_int_arg(
        argv[5],
        "output_limit_mb",
        problem_config.output_limit_mb,
        argument_error
    )) {
        // handled after log_json is initialized
    }

    if (problem_config_error.empty() && argument_error.empty() && argc > 6) {
        if (!is_valid_compare_mode(argv[6])) {
            argument_error = "Invalid compare_mode: " + std::string(argv[6]) +
                             ", expected exact or floating";
        } else {
            problem_config.compare_mode = compare_mode_from_string(argv[6]);
        }
    }

    if (problem_config_error.empty() && argument_error.empty() && argc > 7 && !parse_int_arg(
        argv[7],
        "compile_time_limit_ms",
        problem_config.compile_time_limit_ms,
        argument_error
    )) {
        // handled after log_json is initialized
    }

    if (problem_config_error.empty() && argument_error.empty()) {
        validate_problem_config(problem_config, argument_error);
    }

    std::string input_dir = problem_dir + "/input";
    std::string output_dir = problem_dir + "/output";
    std::string run_id = make_run_id();
    std::string run_dir = BUILD_DIR + "/runs/" + run_id;
    std::string executable_file = run_dir + "/solution";
    std::string compile_error_file = run_dir + "/compile_error.txt";
    std::string user_output_dir = run_dir + "/user_output";
    std::string judge_log_file = run_dir + "/judge_log.json";

    fs::create_directories(BUILD_DIR);
    fs::create_directories(user_output_dir);

    json log_json;

    log_json["submission"] = submission_file;
    log_json["run_id"] = run_id;
    log_json["run_dir"] = run_dir;
    log_json["problem_dir"] = problem_dir;
    log_json["executable_file"] = executable_file;
    log_json["compile_error_file"] = compile_error_file;
    log_json["user_output_dir"] = user_output_dir;
    log_json["time_limit_ms"] = problem_config.time_limit_ms;
    log_json["memory_limit_mb"] = problem_config.memory_limit_mb;
    log_json["output_limit_mb"] = problem_config.output_limit_mb;
    log_json["compile_time_limit_ms"] = problem_config.compile_time_limit_ms;
    log_json["compare_mode"] = compare_mode_to_string(problem_config.compare_mode);
    log_json["sandbox_type"] = sandbox_type_to_string(problem_config.sandbox_type);
    log_json["float_abs_eps"] = problem_config.float_abs_eps;
    log_json["float_rel_eps"] = problem_config.float_rel_eps;
    log_json["results"] = json::array();

    if (!problem_config_error.empty()) {
        std::cout << problem_config_error << std::endl;

        log_json["final_verdict"] = final_verdict_to_string(FinalVerdict::SE);
        log_json["error"] = problem_config_error;
        log_json["passed"] = 0;
        log_json["total"] = 0;

        write_log_file(judge_log_file, log_json);
        return;
    }

    if (!argument_error.empty()) {
        std::cout << argument_error << std::endl;

        log_json["final_verdict"] = final_verdict_to_string(FinalVerdict::SE);
        log_json["error"] = argument_error;
        log_json["passed"] = 0;
        log_json["total"] = 0;

        write_log_file(judge_log_file, log_json);
        return;
    }

    if (!fs::exists(input_dir) || !fs::is_directory(input_dir)) {
        std::cout << "Input directory not found: " << input_dir << std::endl;

        log_json["final_verdict"] = final_verdict_to_string(FinalVerdict::SE);
        log_json["error"] = "Input directory not found: " + input_dir;
        log_json["passed"] = 0;
        log_json["total"] = 0;

        write_log_file(judge_log_file, log_json);
        return;
    }

    if (!fs::exists(output_dir) || !fs::is_directory(output_dir)) {
        std::cout << "Output directory not found: " << output_dir << std::endl;

        log_json["final_verdict"] = final_verdict_to_string(FinalVerdict::SE);
        log_json["error"] = "Output directory not found: " + output_dir;
        log_json["passed"] = 0;
        log_json["total"] = 0;

        write_log_file(judge_log_file, log_json);
        return;
    }

    std::vector<fs::path> input_files;

    for (const auto& entry : fs::directory_iterator(input_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".in") {
            input_files.push_back(entry.path());
        }
    }

    std::sort(input_files.begin(), input_files.end());

    if (input_files.empty()) {
        std::cout << "No input files found in: " << input_dir << std::endl;

        log_json["final_verdict"] = final_verdict_to_string(FinalVerdict::SE);
        log_json["error"] = "No input files found in: " + input_dir;
        log_json["passed"] = 0;
        log_json["total"] = 0;

        write_log_file(judge_log_file, log_json);
        return;
    }

    bool compile_ok = compile_cpp(
        submission_file,
        executable_file,
        compile_error_file,
        problem_config.compile_time_limit_ms
    );

    if (!compile_ok) {
        std::cout << "\n========== Compile Error ==========" << std::endl;

        std::string compile_error = read_text_file(compile_error_file);
        std::cout << compile_error;

        log_json["final_verdict"] = final_verdict_to_string(FinalVerdict::CE);
        log_json["compile_error"] = compile_error;
        log_json["passed"] = 0;
        log_json["total"] = input_files.size();

        write_log_file(judge_log_file, log_json);
        return;
    }

    int accepted = 0;
    FinalVerdict final_verdict = FinalVerdict::AC;

    for (const auto& input_path : input_files) {
        std::string case_name = input_path.stem().string();

        std::string standard_output_file = output_dir + "/" + case_name + ".out";
        std::string user_output_file = user_output_dir + "/" + case_name + ".out";

        json case_json;

        case_json["case"] = case_name;
        case_json["input_file"] = input_path.string();
        case_json["standard_output_file"] = standard_output_file;
        case_json["user_output_file"] = user_output_file;
        case_json["user_error_file"] = user_output_file + ".err";

        if (!fs::exists(standard_output_file)) {
            case_json["verdict"] = "SE";
            case_json["message"] = "Standard output file not found";
            case_json["run_result"] = "SYSTEM_ERROR";
            case_json["time_ms"] = 0;
            case_json["memory_mb"] = 0;

            if (final_verdict == FinalVerdict::AC) {
                final_verdict = FinalVerdict::SE;
            }

            log_json["results"].push_back(case_json);
            continue;
        }

        RunInfo run_info = run_program(
            executable_file,
            input_path.string(),
            user_output_file,
            problem_config.time_limit_ms,
            problem_config.memory_limit_mb,
            problem_config.output_limit_mb,
            problem_config.sandbox_type
        );

        case_json["run_result"] = run_result_to_string(run_info.result);
        case_json["time_ms"] = run_info.time_ms;
        case_json["memory_mb"] = run_info.memory_mb;

        if (run_info.result != RunResult::OK) {
            if (is_sandbox_system_error(problem_config, run_info, case_json["user_error_file"])) {
                case_json["verdict"] = "SE";
                case_json["message"] = "Sandbox runner failed: " +
                                       sandbox_type_to_string(problem_config.sandbox_type);

                if (final_verdict == FinalVerdict::AC) {
                    final_verdict = FinalVerdict::SE;
                }
            } else {
                case_json["verdict"] = run_result_to_string(run_info.result);
                case_json["message"] = run_result_to_string(run_info.result);

                if (final_verdict == FinalVerdict::AC) {
                    final_verdict = run_result_to_final_verdict(run_info.result);
                }
            }
        } else {
            bool same = compare_output(
                user_output_file,
                standard_output_file,
                problem_config.compare_mode,
                problem_config.float_abs_eps,
                problem_config.float_rel_eps
            );

            if (same) {
                case_json["verdict"] = "AC";
                accepted++;
            } else {
                case_json["verdict"] = "WA";
                case_json["message"] = "Output differs from standard answer";

                if (final_verdict == FinalVerdict::AC) {
                    final_verdict = FinalVerdict::WA;
                }
            }
        }

        log_json["results"].push_back(case_json);
    }

    log_json["final_verdict"] = final_verdict_to_string(final_verdict);
    log_json["passed"] = accepted;
    log_json["total"] = input_files.size();

    write_log_file(judge_log_file, log_json);

    std::cout << "\n========== Final Result ==========" << std::endl;
    std::cout << "Final Verdict: "
              << final_verdict_to_string(final_verdict)
              << std::endl;
    std::cout << "Passed: "
              << accepted
              << " / "
              << input_files.size()
              << std::endl;
}
