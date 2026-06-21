#pragma once

#include <optional>
#include <string>

enum class CliCommand {
    Judge,
    Version,
    Doctor
};

struct CliOptions {
    CliCommand command = CliCommand::Judge;

    bool user_facing = false;
    bool exit_requested = false;
    int exit_code = 0;

    std::string submission_file;
    std::string problem_dir;

    std::optional<std::string> time_limit_ms;
    std::optional<std::string> memory_limit_mb;
    std::optional<std::string> output_limit_mb;
    std::optional<std::string> compare_mode;
    std::optional<std::string> compile_time_limit_ms;
    std::optional<std::string> sandbox_type;
};

CliOptions parse_cli(int argc, char* argv[]);
void print_general_help();
void print_judge_help();
int print_version();
int run_doctor();
