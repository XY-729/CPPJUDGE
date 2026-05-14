#include "judge.h"
#include "config.h"
#include "compiler.h"
#include "runner.h"
#include "comparer.h"
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <../include/json.hpp> // JSON库, 需安装 https://github.com/nlohmann/json

namespace fs = std::filesystem;
using json = nlohmann::json;

enum class FinalVerdict { AC, WA, TLE, MLE, OLE, RE, CE };

std::string final_verdict_to_string(FinalVerdict verdict) {
    switch (verdict) {
        case FinalVerdict::AC: return "Accepted";
        case FinalVerdict::WA: return "Wrong Answer";
        case FinalVerdict::TLE: return "Time Limit Exceeded";
        case FinalVerdict::MLE: return "Memory Limit Exceeded";
        case FinalVerdict::OLE: return "Output Limit Exceeded";
        case FinalVerdict::RE: return "Runtime Error";
        case FinalVerdict::CE: return "Compile Error";
        default: return "Unknown";
    }
}

bool compare_output_floating(const std::string& user_output_file, const std::string& standard_output_file, double eps = 1e-6) {
    std::ifstream user_file(user_output_file);
    std::ifstream std_file(standard_output_file);
    if (!user_file.is_open() || !std_file.is_open()) return false;

    std::string uline, sline;
    while (std::getline(user_file, uline) && std::getline(std_file, sline)) {
        std::istringstream us(uline), ss(sline);
        std::string utok, stok;
        while (us >> utok && ss >> stok) {
            try {
                double uval = std::stod(utok);
                double sval = std::stod(stok);
                if (std::fabs(uval - sval) > eps) return false;
            } catch (...) {
                if (utok != stok) return false;
            }
        }
        if ((us >> utok) || (ss >> stok)) return false;
    }
    if ((std::getline(user_file, uline)) || (std::getline(std_file, sline))) return false;
    return true;
}

void judge(int argc, char* argv[]) {
    std::string submission_file = SUBMISSION_FILE;
    std::string problem_dir = PROBLEM_DIR;
    int time_limit = TIME_LIMIT_MS;
    int memory_limit = MEMORY_LIMIT_MB;

    if (argc > 1) submission_file = argv[1];
    if (argc > 2) problem_dir = argv[2];
    if (argc > 3) time_limit = std::stoi(argv[3]);
    if (argc > 4) memory_limit = std::stoi(argv[4]);

    std::string input_dir = problem_dir + "/input";
    std::string output_dir = problem_dir + "/output";
    std::string build_dir = "build";
    std::string user_output_dir = build_dir + "/user_output";

    fs::create_directories(user_output_dir);

    bool compile_ok = compile_cpp(submission_file, EXECUTABLE_FILE, build_dir + "/compile_error.txt");

    json log_json;
    log_json["submission"] = submission_file;
    log_json["results"] = json::array();

    if (!compile_ok) {
        std::cout << "\n========== Compile Error ==========" << std::endl;
        std::ifstream error_file(build_dir + "/compile_error.txt");
        std::stringstream ss;
        if (error_file.is_open()) {
            std::string line;
            while (std::getline(error_file, line)) {
                std::cout << line << std::endl;
                ss << line << "\n";
            }
        }
        log_json["final_verdict"] = "CE";
        log_json["compile_error"] = ss.str();
        std::ofstream logf(build_dir + "/judge_log.json");
        logf << std::setw(4) << log_json << std::endl;
        return;
    }

    std::vector<fs::path> input_files;
    for (const auto& entry : fs::directory_iterator(input_dir))
        if (entry.is_regular_file()) input_files.push_back(entry.path());
    std::sort(input_files.begin(), input_files.end());

    int accepted = 0;
    FinalVerdict final_verdict = FinalVerdict::AC;

    for (const auto& input_path : input_files) {
        std::string filename = input_path.filename().string();
        std::string case_name = filename.substr(0, filename.find_last_of('.'));
        std::string standard_output_file = output_dir + "/" + case_name + ".out";
        std::string user_output_file = user_output_dir + "/" + case_name + ".out";

        RunInfo run_info = run_program(EXECUTABLE_FILE, input_path.string(), user_output_file, time_limit, memory_limit, OUTPUT_LIMIT_MB);

        json case_json;
        case_json["case"] = case_name;
        case_json["time_ms"] = run_info.time_ms;
        case_json["memory_mb"] = run_info.memory_mb;

        if (run_info.result != RunResult::OK) {
            case_json["verdict"] = run_result_to_string(run_info.result);
            if (final_verdict == FinalVerdict::AC) {
                if (run_info.result == RunResult::TLE) final_verdict = FinalVerdict::TLE;
                else if (run_info.result == RunResult::MLE) final_verdict = FinalVerdict::MLE;
                else if (run_info.result == RunResult::OLE) final_verdict = FinalVerdict::OLE;
                else final_verdict = FinalVerdict::RE;
            }
        } else {
            bool same = compare_output_floating(user_output_file, standard_output_file);
            if (same) {
                case_json["verdict"] = "AC";
                accepted++;
            } else {
                case_json["verdict"] = "WA";
                if (final_verdict == FinalVerdict::AC) final_verdict = FinalVerdict::WA;
            }
        }

        log_json["results"].push_back(case_json);
    }

    log_json["final_verdict"] = final_verdict_to_string(final_verdict);
    log_json["passed"] = accepted;
    log_json["total"] = input_files.size();

    std::ofstream logf(build_dir + "/judge_log.json");
    logf << std::setw(4) << log_json << std::endl;

    std::cout << "\n========== Final Result ==========" << std::endl;
    std::cout << "Final Verdict: " << final_verdict_to_string(final_verdict) << std::endl;
    std::cout << "Passed: " << accepted << " / " << input_files.size() << std::endl;
}
