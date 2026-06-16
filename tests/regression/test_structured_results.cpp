#include "runner.h"
#include "compiler.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

static int failures = 0;
static std::string g_temp_dir;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s\n", msg); \
        failures++; \
    } else { \
        printf("  ok: %s\n", msg); \
    } \
} while(0)

static std::string write_file(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
    f.close();
    return path;
}

static void setup_temp_dir() {
    char tmpl[] = "/tmp/cppjudge_struct_test.XXXXXX";
    const char* dir = mkdtemp(tmpl);
    if (!dir) {
        fprintf(stderr, "FATAL: cannot create temp dir\n");
        exit(1);
    }
    g_temp_dir = dir;
}

static void cleanup_temp_dir() {
    if (!g_temp_dir.empty()) {
        fs::remove_all(g_temp_dir);
    }
}

// ── 4.1 Runner infrastructure errors ────────────────────

static void test_nonexistent_executable() {
    printf("\n=== Runner: non-existent executable -> SE ===\n");
    std::string out = g_temp_dir + "/nonex_out.txt";
    RunInfo info = run_program(
        g_temp_dir + "/does_not_exist_xyz",
        "/dev/null",
        out,
        1000, 128, 1,
        SandboxType::BUILTIN
    );
    CHECK(info.result == RunResult::SE, "non-existent exec result is SE");
    CHECK(info.system_error, "non-existent exec system_error == true");
    CHECK(!info.error_message.empty(), "non-existent exec error_message non-empty");
    CHECK(info.result != RunResult::RE, "non-existent exec is NOT RE");
}

static void test_executable_no_permission() {
    printf("\n=== Runner: executable without +x -> SE ===\n");
    std::string exe = g_temp_dir + "/noperm_exe";
    write_file(exe, "#!/bin/sh\necho hello\n");
    chmod(exe.c_str(), 0644);

    std::string out = g_temp_dir + "/noperm_out.txt";
    RunInfo info = run_program(
        exe,
        "/dev/null",
        out,
        1000, 128, 1,
        SandboxType::BUILTIN
    );
    CHECK(info.result == RunResult::SE, "no-exec-perm result is SE");
    CHECK(info.system_error, "no-exec-perm system_error == true");
    CHECK(!info.error_message.empty(), "no-exec-perm error_message non-empty");
}

static void test_input_file_missing() {
    printf("\n=== Runner: missing input file -> SE ===\n");
    std::string ok_exe = g_temp_dir + "/ok_prog";
    write_file(g_temp_dir + "/ok_prog_src.cpp",
        "int main() { return 0; }\n");
    std::string compile_err = g_temp_dir + "/ok_compile_err.txt";
    CompileInfo ci = compile_cpp_structured(
        g_temp_dir + "/ok_prog_src.cpp",
        ok_exe,
        compile_err,
        5000,
        SandboxType::BUILTIN
    );
    CHECK(ci.result == CompileResult::OK, "compile ok_prog for input test");

    if (ci.result == CompileResult::OK) {
        std::string out = g_temp_dir + "/input_miss_out.txt";
        RunInfo info = run_program(
            ok_exe,
            g_temp_dir + "/no_such_input_file.xyz",
            out,
            1000, 128, 1,
            SandboxType::BUILTIN
        );
        CHECK(info.result == RunResult::SE, "missing input result is SE");
        CHECK(info.system_error, "missing input system_error == true");
    }
}

static void test_output_parent_missing() {
    printf("\n=== Runner: output parent dir missing -> SE ===\n");
    std::string ok_exe = g_temp_dir + "/ok_prog2";
    write_file(g_temp_dir + "/ok_prog2_src.cpp",
        "int main() { return 0; }\n");
    std::string compile_err = g_temp_dir + "/ok_compile_err2.txt";
    CompileInfo ci = compile_cpp_structured(
        g_temp_dir + "/ok_prog2_src.cpp",
        ok_exe,
        compile_err,
        5000,
        SandboxType::BUILTIN
    );
    CHECK(ci.result == CompileResult::OK, "compile ok_prog2 for output test");

    if (ci.result == CompileResult::OK) {
        std::string out = g_temp_dir + "/nonexistent_dir/output.txt";
        RunInfo info = run_program(
            ok_exe,
            "/dev/null",
            out,
            1000, 128, 1,
            SandboxType::BUILTIN
        );
        CHECK(info.result == RunResult::SE, "bad output parent result is SE");
        CHECK(info.system_error, "bad output parent system_error == true");
    }
}

// ── 4.2 Normal user program errors ──────────────────────

static void test_exit_7() {
    printf("\n=== Runner: exit(7) -> RE ===\n");
    std::string src = g_temp_dir + "/exit7.cpp";
    write_file(src, "int main() { return 7; }\n");
    std::string exe = g_temp_dir + "/exit7_exe";
    std::string err = g_temp_dir + "/exit7_err.txt";
    CompileInfo ci = compile_cpp_structured(src, exe, err, 5000, SandboxType::BUILTIN);
    CHECK(ci.result == CompileResult::OK, "compile exit7");

    if (ci.result == CompileResult::OK) {
        std::string out = g_temp_dir + "/exit7_out.txt";
        RunInfo info = run_program(exe, "/dev/null", out, 1000, 128, 1, SandboxType::BUILTIN);
        CHECK(info.result == RunResult::RE, "exit(7) result is RE");
        CHECK(!info.system_error, "exit(7) system_error == false");
        CHECK(info.exit_code == 7, "exit(7) exit_code == 7");
        CHECK(info.result != RunResult::SE, "exit(7) is NOT SE");
    }
}

static void test_sigsegv() {
    printf("\n=== Runner: SIGSEGV -> RE with signal ===\n");
    std::string src = g_temp_dir + "/sigsegv.cpp";
    write_file(src, "#include <csignal>\nint main() { raise(SIGSEGV); return 0; }\n");
    std::string exe = g_temp_dir + "/sigsegv_exe";
    std::string err = g_temp_dir + "/sigsegv_err.txt";
    CompileInfo ci = compile_cpp_structured(src, exe, err, 5000, SandboxType::BUILTIN);
    CHECK(ci.result == CompileResult::OK, "compile sigsegv");

    if (ci.result == CompileResult::OK) {
        std::string out = g_temp_dir + "/sigsegv_out.txt";
        RunInfo info = run_program(exe, "/dev/null", out, 1000, 128, 1, SandboxType::BUILTIN);
        CHECK(info.result == RunResult::RE, "SIGSEGV result is RE");
        CHECK(!info.system_error, "SIGSEGV system_error == false");
        CHECK(info.signal >= 0, "SIGSEGV signal >= 0");
    }
}

static void test_timeout_tle() {
    printf("\n=== Runner: infinite loop -> TLE ===\n");
    std::string src = g_temp_dir + "/infloop.cpp";
    write_file(src, "int main() { for(;;) {} }\n");
    std::string exe = g_temp_dir + "/infloop_exe";
    std::string err = g_temp_dir + "/infloop_err.txt";
    CompileInfo ci = compile_cpp_structured(src, exe, err, 5000, SandboxType::BUILTIN);
    CHECK(ci.result == CompileResult::OK, "compile infloop");

    if (ci.result == CompileResult::OK) {
        std::string out = g_temp_dir + "/infloop_out.txt";
        RunInfo info = run_program(exe, "/dev/null", out, 200, 128, 1, SandboxType::BUILTIN);
        CHECK(info.result == RunResult::TLE, "infloop result is TLE");
        CHECK(!info.system_error, "infloop system_error == false");
        CHECK(info.result != RunResult::RE, "infloop is NOT RE");
    }
}

static void test_output_limit_ole() {
    printf("\n=== Runner: excessive output -> OLE ===\n");
    std::string src = g_temp_dir + "/bigout.cpp";
    write_file(src,
        "#include <cstdio>\n"
        "int main() { while(1) { fputs(\"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\\n\", stdout); } return 0; }\n");
    std::string exe = g_temp_dir + "/bigout_exe";
    std::string err = g_temp_dir + "/bigout_err.txt";
    CompileInfo ci = compile_cpp_structured(src, exe, err, 5000, SandboxType::BUILTIN);
    CHECK(ci.result == CompileResult::OK, "compile bigout");

    if (ci.result == CompileResult::OK) {
        std::string out = g_temp_dir + "/bigout_out.txt";
        RunInfo info = run_program(exe, "/dev/null", out, 1000, 128, 1, SandboxType::BUILTIN);
        CHECK(info.result == RunResult::OLE, "bigout result is OLE");
        CHECK(!info.system_error, "bigout system_error == false");
        CHECK(info.result != RunResult::RE, "bigout is NOT RE");
    }
}

// ── 4.3 Compiler structured errors ──────────────────────

static void test_source_not_found() {
    printf("\n=== Compiler: source not found -> CE (g++ reports error) ===\n");
    CompileInfo ci = compile_cpp_structured(
        g_temp_dir + "/no_such_source.cpp",
        g_temp_dir + "/dummy_exe",
        g_temp_dir + "/dummy_err.txt",
        5000,
        SandboxType::BUILTIN
    );
    // Current implementation: g++ receives non-existent source, reports error,
    // exits non-zero -> CE (not SE). Source validation is not done upfront.
    CHECK(ci.result == CompileResult::CE, "source not found result is CE (g++ error)");
    CHECK(!ci.system_error, "source not found system_error == false (g++ user error)");
    CHECK(ci.result != CompileResult::SE, "source not found is NOT SE");
}

static void test_error_file_unwritable() {
    printf("\n=== Compiler: error file blocked by regular file -> SE ===\n");
    // Create a regular file where a directory component would go
    std::string blocker = g_temp_dir + "/err_blocker";
    write_file(blocker, "block\n");
    std::string bad_err = blocker + "/subdir/err.txt";

    std::string src = g_temp_dir + "/ok_src2.cpp";
    write_file(src, "int main() { return 0; }\n");
    CompileInfo ci = compile_cpp_structured(
        src,
        g_temp_dir + "/ok_exe2",
        bad_err,
        5000,
        SandboxType::BUILTIN
    );
    CHECK(ci.result == CompileResult::SE, "blocked error file result is SE");
    CHECK(ci.system_error, "blocked error file system_error == true");
}

static void test_syntax_error_ce() {
    printf("\n=== Compiler: syntax error -> CE ===\n");
    std::string src = g_temp_dir + "/syntax_err.cpp";
    write_file(src, "this is not valid c++ at all {{{{{\n");
    CompileInfo ci = compile_cpp_structured(
        src,
        g_temp_dir + "/ce_exe",
        g_temp_dir + "/ce_err.txt",
        5000,
        SandboxType::BUILTIN
    );
    CHECK(ci.result == CompileResult::CE, "syntax error result is CE");
    CHECK(!ci.system_error, "syntax error system_error == false");
    CHECK(ci.result != CompileResult::SE, "syntax error is NOT SE");
}

static void test_normal_compile_ok() {
    printf("\n=== Compiler: normal code -> OK ===\n");
    std::string src = g_temp_dir + "/normal.cpp";
    write_file(src, "int main() { return 0; }\n");
    CompileInfo ci = compile_cpp_structured(
        src,
        g_temp_dir + "/normal_exe",
        g_temp_dir + "/normal_err.txt",
        5000,
        SandboxType::BUILTIN
    );
    CHECK(ci.result == CompileResult::OK, "normal compile result is OK");
    CHECK(!ci.system_error, "normal compile system_error == false");
}

// ── 4.4 Sandbox dispatch ────────────────────────────────

static void test_isolate_sandbox() {
    printf("\n=== Sandbox: isolate preflight + compile-phase rejection ===\n");
    // Preflight: ISOLATE passes infrastructure check (no binary needed)
    std::string preflight_error;
    bool preflight_ok = sandbox_preflight_check(SandboxType::ISOLATE, preflight_error);
    CHECK(preflight_ok, "isolate preflight returns true (infra available)");

    // Compile phase: ISOLATE returns SE with "not implemented"
    std::string src = g_temp_dir + "/isolate_test.cpp";
    write_file(src, "int main() { return 0; }\n");
    CompileInfo ci = compile_cpp_structured(
        src,
        g_temp_dir + "/isolate_exe",
        g_temp_dir + "/isolate_err.txt",
        5000,
        SandboxType::ISOLATE
    );
    CHECK(ci.result == CompileResult::SE, "isolate compile result is SE");
    CHECK(ci.system_error, "isolate compile system_error == true");
    CHECK(!ci.error_message.empty(), "isolate compile error_message non-empty");
}

int main() {
    setup_temp_dir();
    printf("=== Structured Results Regression Tests ===\n");

    test_nonexistent_executable();
    test_executable_no_permission();
    test_input_file_missing();
    test_output_parent_missing();

    test_exit_7();
    test_sigsegv();
    test_timeout_tle();
    test_output_limit_ole();

    test_source_not_found();
    test_error_file_unwritable();
    test_syntax_error_ce();
    test_normal_compile_ok();

    test_isolate_sandbox();

    cleanup_temp_dir();

    printf("\n========================================\n");
    if (failures > 0) {
        fprintf(stderr, "FAILED: %d failure(s)\n", failures);
        return 1;
    }
    printf("All structured result tests passed.\n");
    return 0;
}
