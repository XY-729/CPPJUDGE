#pragma once

#include <string>

enum class CompareMode {
    EXACT,
    FLOATING
};

CompareMode compare_mode_from_string(const std::string& mode);
std::string compare_mode_to_string(CompareMode mode);

bool compare_output(
    const std::string& user_output_file,
    const std::string& standard_output_file
);

bool compare_output(
    const std::string& user_output_file,
    const std::string& standard_output_file,
    CompareMode mode,
    double abs_eps,
    double rel_eps
);

bool compare_output_exact(
    const std::string& user_output_file,
    const std::string& standard_output_file
);

bool compare_output_floating(
    const std::string& user_output_file,
    const std::string& standard_output_file,
    double abs_eps = 1e-6,
    double rel_eps = 1e-6
);
