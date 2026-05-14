#include "judge.h"

#include "compiler.h"
#include "comparer.h"
#include "config.h"
#include "runner.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "json.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

enum class FinalVerdict {
    AC,
    WA,
    TLE,
    MLE,
    OLE,
    RE,
    CE
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

static std::string read_text_file(const std::string& file_path) {
    std::ifstream file(file_path);

    if (!file.is_open()) {
        return "";
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();

    return buffer.str();
}

static void write_log_file(const json& log_json) {
    fs::create_directories(BUILD_DIR);

    std::ofstream log_file(JUDGE_LOG_FILE);
    log_file << std::setw(4) << log_json << std::endl;
}

void judge(int argc, char* argv[]) {
    std::string submission_file = SUBMISSION_FILE;
    std::string problem_dir = PROBLEM_DIR;

    int time_limit = TIME_LIMIT_MS;
    int memory_limit = MEMORY_LIMIT_MB;
    int output_limit = OUTPUT_LIMIT_MB;

    if (argc > 1) {
        submission_file = argv[1];
    }

    if (argc > 2) {
        problem_dir = argv[2];
    }

    if (argc > 3) {
        time_limit = std::stoi(argv[3]);
    }

    if (argc > 4) {
        memory_limit = std::stoi(argv[4]);
    }

    if (argc > 5) {
        output_limit = std::stoi(argv[5]);
    }

    std::string input_dir = problem_dir + "/input";
    std::string output_dir = problem_dir + "/output";

    fs::create_directories(BUILD_DIR);
    fs::create_directories(USER_OUTPUT_DIR);

    json log_json;

    log_json["submission"] = submission_file;
    log_json["problem_dir"] = problem_dir;
    log_json["time_limit_ms"] = time_limit;
    log_json["memory_limit_mb"] = memory_limit;
    log_json["output_limit_mb"] = output_limit;
    log_json["compare_mode"] = "floating";
    log_json["float_abs_eps"] = FLOAT_ABS_EPS;
    log_json["float_rel_eps"] = FLOAT_REL_EPS;
    log_json["results"] = json::array();

    if (!fs::exists(input_dir) || !fs::is_directory(input_dir)) {
        std::cout << "Input directory not found: " << input_dir << std::endl;

        log_json["final_verdict"] = final_verdict_to_string(FinalVerdict::RE);
        log_json["error"] = "Input directory not found: " + input_dir;

        write_log_file(log_json);
        return;
    }

    if (!fs::exists(output_dir) || !fs::is_directory(output_dir)) {
        std::cout << "Output directory not found: " << output_dir << std::endl;

        log_json["final_verdict"] = final_verdict_to_string(FinalVerdict::RE);
        log_json["error"] = "Output directory not found: " + output_dir;

        write_log_file(log_json);
        return;
    }

    bool compile_ok = compile_cpp(
        submission_file,
        EXECUTABLE_FILE,
        COMPILE_ERROR_FILE
    );

    if (!compile_ok) {
        std::cout << "\n========== Compile Error ==========" << std::endl;

        std::string compile_error = read_text_file(COMPILE_ERROR_FILE);
        std::cout << compile_error;

        log_json["final_verdict"] = final_verdict_to_string(FinalVerdict::CE);
        log_json["compile_error"] = compile_error;
        log_json["passed"] = 0;
        log_json["total"] = 0;

        write_log_file(log_json);
        return;
    }

    std::vector<fs::path> input_files;

    for (const auto& entry : fs::directory_iterator(input_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".in") {
            input_files.push_back(entry.path());
        }
    }

    std::sort(input_files.begin(), input_files.end());

    int accepted = 0;
    FinalVerdict final_verdict = FinalVerdict::AC;

    for (const auto& input_path : input_files) {
        std::string case_name = input_path.stem().string();

        std::string standard_output_file = output_dir + "/" + case_name + ".out";
        std::string user_output_file = USER_OUTPUT_DIR + "/" + case_name + ".out";

        json case_json;

        case_json["case"] = case_name;
        case_json["input_file"] = input_path.string();
        case_json["standard_output_file"] = standard_output_file;
        case_json["user_output_file"] = user_output_file;

        if (!fs::exists(standard_output_file)) {
            case_json["verdict"] = "WA";
            case_json["message"] = "Standard output file not found";
            case_json["time_ms"] = 0;
            case_json["memory_mb"] = 0;

            if (final_verdict == FinalVerdict::AC) {
                final_verdict = FinalVerdict::WA;
            }

            log_json["results"].push_back(case_json);
            continue;
        }

        RunInfo run_info = run_program(
            EXECUTABLE_FILE,
            input_path.string(),
            user_output_file,
            time_limit,
            memory_limit,
            output_limit
        );

        case_json["time_ms"] = run_info.time_ms;
        case_json["memory_mb"] = run_info.memory_mb;

        if (run_info.result != RunResult::OK) {
            case_json["verdict"] = run_result_to_string(run_info.result);

            if (final_verdict == FinalVerdict::AC) {
                final_verdict = run_result_to_final_verdict(run_info.result);
            }
        } else {
            bool same = compare_output_floating(
                user_output_file,
                standard_output_file,
                FLOAT_ABS_EPS,
                FLOAT_REL_EPS
            );

            if (same) {
                case_json["verdict"] = "AC";
                accepted++;
            } else {
                case_json["verdict"] = "WA";

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

    write_log_file(log_json);

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