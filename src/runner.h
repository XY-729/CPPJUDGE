#pragma once
#include <string>

enum class RunResult {
    OK,
    TLE,
    MLE,
    OLE,
    RE,
    SE
};

enum class SandboxType {
    BUILTIN,
    NSJAIL,
    ISOLATE
};

struct RunInfo {
    RunResult result;
    int time_ms;
    int memory_mb;

    bool system_error = false;
    std::string error_message;
    int exit_code = -1;
    int signal = -1;
};

RunInfo run_program(
    const std::string& executable_file,
    const std::string& input_file,
    const std::string& output_file,
    int time_limit_ms,
    int memory_limit_mb,
    int output_limit_mb
);

RunInfo run_program(
    const std::string& executable_file,
    const std::string& input_file,
    const std::string& output_file,
    int time_limit_ms,
    int memory_limit_mb,
    int output_limit_mb,
    SandboxType sandbox_type
);

std::string run_result_to_string(RunResult result);
std::string sandbox_type_to_string(SandboxType type);
SandboxType sandbox_type_from_string(const std::string& type);
bool is_valid_sandbox_type(const std::string& type);
bool sandbox_preflight_check(SandboxType type, std::string& error);
