#include "config.h"
#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
    else { printf("  ok: %s\n", msg); } \
} while(0)

int main() {
    printf("=== config constants ===\n");
    CHECK(!PROBLEM_NAME.empty(), "PROBLEM_NAME is not empty");
    CHECK(!BUILD_DIR.empty(), "BUILD_DIR is not empty");
    CHECK(TIME_LIMIT_MS > 0, "TIME_LIMIT_MS > 0");
    CHECK(MEMORY_LIMIT_MB > 0, "MEMORY_LIMIT_MB > 0");
    CHECK(OUTPUT_LIMIT_MB > 0, "OUTPUT_LIMIT_MB > 0");
    CHECK(COMPILE_TIME_LIMIT_MS > 0, "COMPILE_TIME_LIMIT_MS > 0");
    CHECK(FLOAT_ABS_EPS > 0, "FLOAT_ABS_EPS > 0");
    CHECK(FLOAT_REL_EPS > 0, "FLOAT_REL_EPS > 0");

    printf("=== derived paths ===\n");
    std::string executable = BUILD_DIR + "/solution";
    CHECK(!executable.empty(), "EXECUTABLE_FILE builds correctly");
    std::string problem_dir = "problems/" + PROBLEM_NAME;
    CHECK(problem_dir == PROBLEM_DIR, "PROBLEM_DIR derivation is consistent");
    std::string input_dir = PROBLEM_DIR + "/input";
    CHECK(input_dir == INPUT_DIR, "INPUT_DIR derivation is consistent");
    std::string output_dir = PROBLEM_DIR + "/output";
    CHECK(output_dir == OUTPUT_DIR, "OUTPUT_DIR derivation is consistent");

    printf("=== time_limit_ms: %d ===\n", TIME_LIMIT_MS);
    printf("=== memory_limit_mb: %d ===\n", MEMORY_LIMIT_MB);
    printf("=== output_limit_mb: %d ===\n", OUTPUT_LIMIT_MB);
    printf("=== compile_time_limit_ms: %d ===\n", COMPILE_TIME_LIMIT_MS);
    printf("=== float_abs_eps: %g ===\n", FLOAT_ABS_EPS);
    printf("=== float_rel_eps: %g ===\n", FLOAT_REL_EPS);

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) FAILED\n", failures);
        return 1;
    }
    printf("\nAll config tests passed.\n");
    return 0;
}
