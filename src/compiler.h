#pragma once
#include <string>

#include "runner.h"

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
