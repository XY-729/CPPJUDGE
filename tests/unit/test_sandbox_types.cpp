#include "runner.h"
#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
    else { printf("  ok: %s\n", msg); } \
} while(0)

int main() {
    printf("=== sandbox_type_from_string ===\n");
    CHECK(sandbox_type_from_string("builtin") == SandboxType::BUILTIN, "builtin -> BUILTIN");
    CHECK(sandbox_type_from_string("nsjail") == SandboxType::NSJAIL, "nsjail -> NSJAIL");
    CHECK(sandbox_type_from_string("isolate") == SandboxType::ISOLATE, "isolate -> ISOLATE");
    CHECK(sandbox_type_from_string("unknown") == SandboxType::BUILTIN, "unknown -> BUILTIN (default)");
    CHECK(sandbox_type_from_string("") == SandboxType::BUILTIN, "empty -> BUILTIN (default)");
    CHECK(sandbox_type_from_string("NSJAIL") == SandboxType::NSJAIL, "NSJAIL (upper) -> NSJAIL");
    CHECK(sandbox_type_from_string("Builtin") == SandboxType::BUILTIN, "Builtin (mixed) -> BUILTIN");

    printf("=== sandbox_type_to_string ===\n");
    CHECK(sandbox_type_to_string(SandboxType::BUILTIN) == "builtin", "BUILTIN -> builtin");
    CHECK(sandbox_type_to_string(SandboxType::NSJAIL) == "nsjail", "NSJAIL -> nsjail");
    CHECK(sandbox_type_to_string(SandboxType::ISOLATE) == "isolate", "ISOLATE -> isolate");

    printf("=== is_valid_sandbox_type ===\n");
    CHECK(is_valid_sandbox_type("builtin"), "builtin is valid");
    CHECK(is_valid_sandbox_type("nsjail"), "nsjail is valid");
    CHECK(is_valid_sandbox_type("isolate"), "isolate is valid");
    CHECK(is_valid_sandbox_type("BUILTIN"), "BUILTIN (upper) is valid");
    CHECK(!is_valid_sandbox_type(""), "empty is invalid");
    CHECK(!is_valid_sandbox_type("unknown"), "unknown is invalid");
    CHECK(!is_valid_sandbox_type("NSjail "), "NSjail with space is invalid");

    printf("=== sandbox_preflight_check ===\n");
    std::string error;
    CHECK(sandbox_preflight_check(SandboxType::BUILTIN, error), "builtin preflight always ok");
    CHECK(error.empty(), "builtin preflight produces no error message");

    // nsjail preflight depends on whether nsjail is in PATH
    bool nsjail_ok = sandbox_preflight_check(SandboxType::NSJAIL, error);
    printf("  nsjail preflight: %s (error: %s)\n",
           nsjail_ok ? "ok" : "fail", error.c_str());

    CHECK(sandbox_preflight_check(SandboxType::ISOLATE, error), "isolate preflight always ok (placeholder)");

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) FAILED\n", failures);
        return 1;
    }
    printf("\nAll sandbox type tests passed.\n");
    return 0;
}
