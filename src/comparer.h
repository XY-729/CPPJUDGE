#pragma once

#include <string>

bool compare_output(
    const std::string& user_output_file,
    const std::string& standard_output_file
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