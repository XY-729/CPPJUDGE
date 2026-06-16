#pragma once
#include <string>

#include "runner.h"

enum class CompileResult {
    OK,
    CE,
    SE
};

struct CompileInfo {
    CompileResult result = CompileResult::OK;
    bool system_error = false;
    std::string error_message;
};

CompileInfo compile_cpp_structured(
    const std::string& source_file,
    const std::string& executable_file,
    const std::string& error_file,
    int compile_time_limit_ms,
    SandboxType sandbox_type
);

bool compile_cpp(
    const std::string& source_file,
    const std::string& executable_file,
    const std::string& error_file,
    int compile_time_limit_ms
);

bool compile_cpp(
    const std::string& source_file,
    const std::string& executable_file,
    const std::string& error_file,
    int compile_time_limit_ms,
    SandboxType sandbox_type
);
