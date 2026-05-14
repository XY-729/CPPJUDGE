#pragma once
#include <string>

const std::string PROBLEM_NAME = "A+B";

const std::string SUBMISSION_FILE = "../submissions/solution.cpp";
const std::string EXECUTABLE_FILE = "../build/solution";

const std::string PROBLEM_DIR = "../problems/" + PROBLEM_NAME;
const std::string INPUT_DIR = PROBLEM_DIR + "/input";
const std::string OUTPUT_DIR = PROBLEM_DIR + "/output";

const std::string USER_OUTPUT_DIR = "../build/user_output";

const std::string COMPILE_ERROR_FILE = "../build/compile_error.txt";

const int TIME_LIMIT_MS = 1000;
const int MEMORY_LIMIT_MB = 128;
const int OUTPUT_LIMIT_MB = 1;