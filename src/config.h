#pragma once

#include <string>

const std::string PROBLEM_NAME = "NONE";

const std::string BUILD_DIR = "build";
const std::string SUBMISSION_FILE = "submissions/solution.cpp";
const std::string EXECUTABLE_FILE = BUILD_DIR + "/solution";

const std::string PROBLEM_DIR = "problems/" + PROBLEM_NAME;
const std::string INPUT_DIR = PROBLEM_DIR + "/input";
const std::string OUTPUT_DIR = PROBLEM_DIR + "/output";
const std::string USER_OUTPUT_DIR = BUILD_DIR + "/user_output";
const std::string COMPILE_ERROR_FILE = BUILD_DIR + "/compile_error.txt";
const std::string JUDGE_LOG_FILE = BUILD_DIR + "/judge_log.json";

const int TIME_LIMIT_MS = 1000;
const int MEMORY_LIMIT_MB = 128;
const int OUTPUT_LIMIT_MB = 1;
const int COMPILE_TIME_LIMIT_MS = 5000;

const double FLOAT_ABS_EPS = 1e-6;
const double FLOAT_REL_EPS = 1e-6;
