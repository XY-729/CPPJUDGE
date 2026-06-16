#include "runner.h"
#include "compiler.h"
#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
    else { printf("  ok: %s\n", msg); } \
} while(0)

int main() {
    printf("=== run_result_to_string ===\n");
    CHECK(run_result_to_string(RunResult::OK) == "OK", "OK -> OK");
    CHECK(run_result_to_string(RunResult::TLE) == "TLE", "TLE -> TLE");
    CHECK(run_result_to_string(RunResult::MLE) == "MLE", "MLE -> MLE");
    CHECK(run_result_to_string(RunResult::OLE) == "OLE", "OLE -> OLE");
    CHECK(run_result_to_string(RunResult::RE) == "RE", "RE -> RE");
    CHECK(run_result_to_string(RunResult::SE) == "SE", "SE -> SE");

    printf("=== CompileResult checks ===\n");
    // CompileResult values: OK=0, CE=1, SE=2
    CHECK(static_cast<int>(CompileResult::OK) == 0, "CompileResult::OK == 0");
    CHECK(static_cast<int>(CompileResult::CE) == 1, "CompileResult::CE == 1");
    CHECK(static_cast<int>(CompileResult::SE) == 2, "CompileResult::SE == 2");

    printf("=== RunResult checks ===\n");
    // RunResult values: OK=0, TLE=1, MLE=2, OLE=3, RE=4, SE=5
    CHECK(static_cast<int>(RunResult::OK) == 0, "RunResult::OK == 0");
    CHECK(static_cast<int>(RunResult::TLE) == 1, "RunResult::TLE == 1");
    CHECK(static_cast<int>(RunResult::MLE) == 2, "RunResult::MLE == 2");
    CHECK(static_cast<int>(RunResult::OLE) == 3, "RunResult::OLE == 3");
    CHECK(static_cast<int>(RunResult::RE) == 4, "RunResult::RE == 4");
    CHECK(static_cast<int>(RunResult::SE) == 5, "RunResult::SE == 5");

    printf("=== CompileInfo defaults ===\n");
    CompileInfo ci;
    CHECK(ci.result == CompileResult::OK, "CompileInfo default result is OK");
    CHECK(ci.system_error == false, "CompileInfo default system_error is false");
    CHECK(ci.error_message.empty(), "CompileInfo default error_message is empty");

    printf("=== RunInfo defaults ===\n");
    RunInfo ri;
    CHECK(ri.result == RunResult::OK, "RunInfo default result is OK");
    CHECK(ri.time_ms == 0, "RunInfo default time_ms is 0");
    CHECK(ri.system_error == false, "RunInfo default system_error is false");
    CHECK(ri.exit_code == -1, "RunInfo default exit_code is -1");
    CHECK(ri.signal == -1, "RunInfo default signal is -1");

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) FAILED\n", failures);
        return 1;
    }
    printf("\nAll verdict/string tests passed.\n");
    return 0;
}
