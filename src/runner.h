#pragma once
#include <string>

enum class RunResult {
    OK,
    TLE,
    MLE,
    OLE,
    RE
};

struct RunInfo {
    RunResult result;
    int time_ms;
    int memory_mb;
};

RunInfo run_program(
    const std::string& executable_file,
    const std::string& input_file,
    const std::string& output_file,
    int time_limit_ms,
    int memory_limit_mb,
    int output_limit_mb
);

std::string run_result_to_string(RunResult result);