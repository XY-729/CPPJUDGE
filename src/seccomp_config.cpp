#include "seccomp_config.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

bool SeccompConfig::validate(std::string& error) const {
    if (!enabled) return true;

    if (policy_path.empty()) {
        error = "seccomp policy path is empty but seccomp is enabled";
        return false;
    }

    // Path containment: reject traversal patterns
    if (policy_path.find("..") != std::string::npos) {
        error = "seccomp policy path contains unsafe traversal: " + policy_path;
        return false;
    }

    std::error_code ec;
    if (!fs::exists(policy_path, ec)) {
        error = "seccomp policy file not found: " + policy_path;
        return false;
    }

    if (!fs::is_regular_file(policy_path, ec)) {
        error = "seccomp policy path is not a regular file: " + policy_path;
        return false;
    }

    // Try to open for reading
    std::ifstream test(policy_path);
    if (!test.is_open()) {
        error = "seccomp policy file not readable: " + policy_path;
        return false;
    }
    // Read a small amount to confirm it's not empty
    std::string first_line;
    std::getline(test, first_line);
    if (first_line.empty()) {
        error = "seccomp policy file appears empty: " + policy_path;
        return false;
    }

    return true;
}

std::string default_seccomp_policy_path() {
    // Use CPPJUDGE_SRC_DIR env var for testing, otherwise resolve relative to cwd
    const char* env = std::getenv("CPPJUDGE_SRC_DIR");
    if (env && env[0] != '\0') {
        return std::string(env) + "/sandbox/seccomp/cppjudge-runtime.kafel";
    }
    // Production: resolve relative to the repository root
    // We use a heuristic: cppjudge binary is at <repo>/build-*/cppjudge
    // CPPJUDGE_SRC_DIR must be set in production (see deploy docs)
    return "sandbox/seccomp/cppjudge-runtime.kafel";
}
