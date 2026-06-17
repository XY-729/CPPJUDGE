#pragma once

#include <string>

// Stage 3C: seccomp policy configuration for nsjail production mode.
// Policy is enforced via nsjail's --seccomp_policy flag using Kafel format.

struct SeccompConfig {
    // Path to the Kafel seccomp policy file.
    // Default: sandbox/seccomp/cppjudge-runtime.kafel (relative to repo root).
    // Set to empty string to disable (only valid for testing/builtin).
    std::string policy_path;

    // Whether seccomp is enabled for nsjail runs.
    // Production nsjail mode always requires seccomp; this flag is for
    // test/development overrides only.
    bool enabled = true;

    // Validate that the policy file exists, is a regular file, and is readable.
    // Returns false with error description on failure.
    bool validate(std::string& error) const;
};

// Determine the default policy path relative to the source directory.
// This is resolved at runtime relative to the cppjudge binary's location
// or via the CPPJUDGE_SRC_DIR environment variable for testing.
std::string default_seccomp_policy_path();
