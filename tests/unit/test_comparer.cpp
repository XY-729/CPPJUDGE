#include "comparer.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
    else { printf("  ok: %s\n", msg); } \
} while(0)

static std::string write_temp(const std::string& base, const std::string& content) {
    std::string path = "/tmp/cppjudge_test_" + base;
    FILE* f = fopen(path.c_str(), "w");
    if (f) { fprintf(f, "%s", content.c_str()); fclose(f); }
    return path;
}

static void test_compare_mode_conversion() {
    CHECK(is_valid_compare_mode("exact"), "exact is valid");
    CHECK(is_valid_compare_mode("floating"), "floating is valid");
    CHECK(is_valid_compare_mode("float"), "float is valid (alias)");
    CHECK(!is_valid_compare_mode(""), "empty is invalid");
    CHECK(!is_valid_compare_mode("unknown"), "unknown is invalid");
    CHECK(is_valid_compare_mode("EXACT"), "EXACT (upper) is valid (case insensitive)");

    CHECK(compare_mode_from_string("exact") == CompareMode::EXACT, "exact -> EXACT");
    CHECK(compare_mode_from_string("floating") == CompareMode::FLOATING, "floating -> FLOATING");
    CHECK(compare_mode_from_string("float") == CompareMode::FLOATING, "float -> FLOATING (alias)");
    CHECK(compare_mode_from_string("unknown") == CompareMode::EXACT, "unknown -> EXACT (default)");
    CHECK(compare_mode_from_string("") == CompareMode::EXACT, "empty -> EXACT (default)");

    CHECK(compare_mode_to_string(CompareMode::EXACT) == "exact", "EXACT -> exact");
    CHECK(compare_mode_to_string(CompareMode::FLOATING) == "floating", "FLOATING -> floating");
}

// ── exact compare tests ──────────────────────────────────

static void test_exact_identical() {
    std::string f1 = write_temp("exact1.txt", "hello world\n");
    std::string f2 = write_temp("exact2.txt", "hello world\n");
    CHECK(compare_output_exact(f1, f2), "exact: identical files match");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_exact_trailing_newline() {
    std::string f1 = write_temp("exact_a.txt", "hello\n");
    std::string f2 = write_temp("exact_b.txt", "hello");
    CHECK(compare_output_exact(f1, f2), "exact: trailing newline treated equal");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_exact_trailing_spaces() {
    std::string f1 = write_temp("exact_c.txt", "hello  \n");
    std::string f2 = write_temp("exact_d.txt", "hello");
    CHECK(compare_output_exact(f1, f2), "exact: trailing spaces stripped");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_exact_middle_spaces_differ() {
    std::string f1 = write_temp("exact_e.txt", "hello world\n");
    std::string f2 = write_temp("exact_f.txt", "hello  world\n");
    CHECK(!compare_output_exact(f1, f2), "exact: middle spaces cause mismatch");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_exact_multiline() {
    std::string f1 = write_temp("exact_g.txt", "line1\nline2\nline3\n");
    std::string f2 = write_temp("exact_h.txt", "line1\nline2\nline3\n");
    CHECK(compare_output_exact(f1, f2), "exact: multiline match");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_exact_empty() {
    std::string f1 = write_temp("exact_i.txt", "");
    std::string f2 = write_temp("exact_j.txt", "");
    CHECK(compare_output_exact(f1, f2), "exact: empty files match");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_exact_case_different() {
    std::string f1 = write_temp("exact_k.txt", "Hello\n");
    std::string f2 = write_temp("exact_l.txt", "hello\n");
    CHECK(!compare_output_exact(f1, f2), "exact: case difference causes mismatch");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_exact_extra_token() {
    std::string f1 = write_temp("exact_m.txt", "1 2\n");
    std::string f2 = write_temp("exact_n.txt", "1 2 3\n");
    CHECK(!compare_output_exact(f1, f2), "exact: extra token causes mismatch");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_exact_nonexistent_file() {
    CHECK(!compare_output_exact("/tmp/nonexistent12345_a.txt", "/tmp/nonexistent12345_b.txt"),
          "exact: nonexistent files return false");
}

// ── floating compare tests ───────────────────────────────

static void test_floating_exact_equal() {
    std::string f1 = write_temp("flt_a.txt", "3.14\n");
    std::string f2 = write_temp("flt_b.txt", "3.14\n");
    CHECK(compare_output_floating(f1, f2), "floating: exact equal match");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_floating_within_abs_eps() {
    std::string f1 = write_temp("flt_c.txt", "1.0000001\n");
    std::string f2 = write_temp("flt_d.txt", "1.0000002\n");
    CHECK(compare_output_floating(f1, f2, 0.001, 1e-6), "floating: within abs eps");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_floating_within_rel_eps() {
    std::string f1 = write_temp("flt_e.txt", "1000.5\n");
    std::string f2 = write_temp("flt_f.txt", "1000.0\n");
    CHECK(compare_output_floating(f1, f2, 1e-6, 0.01), "floating: within rel eps (0.05% < 1%)");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_floating_exceeds_eps() {
    std::string f1 = write_temp("flt_g.txt", "1.0\n");
    std::string f2 = write_temp("flt_h.txt", "1.1\n");
    CHECK(!compare_output_floating(f1, f2, 0.01, 1e-9), "floating: exceeds both eps");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_floating_negative() {
    std::string f1 = write_temp("flt_i.txt", "-5.0\n");
    std::string f2 = write_temp("flt_j.txt", "-5.000001\n");
    CHECK(compare_output_floating(f1, f2, 0.001, 1e-6), "floating: negative numbers match");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_floating_near_zero() {
    std::string f1 = write_temp("flt_k.txt", "0.0\n");
    std::string f2 = write_temp("flt_l.txt", "0.0000001\n");
    CHECK(compare_output_floating(f1, f2, 0.001, 1e-6), "floating: near-zero within abs eps");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_floating_scientific() {
    std::string f1 = write_temp("flt_m.txt", "1.5e3\n");
    std::string f2 = write_temp("flt_n.txt", "1500.0\n");
    CHECK(compare_output_floating(f1, f2), "floating: scientific notation vs decimal");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_floating_non_numeric_tokens() {
    std::string f1 = write_temp("flt_o.txt", "hello world\n");
    std::string f2 = write_temp("flt_p.txt", "hello world\n");
    CHECK(compare_output_floating(f1, f2), "floating: non-numeric tokens match as strings");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_floating_token_count_mismatch() {
    std::string f1 = write_temp("flt_q.txt", "1.0 2.0\n");
    std::string f2 = write_temp("flt_r.txt", "1.0\n");
    CHECK(!compare_output_floating(f1, f2), "floating: token count mismatch");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_floating_nan() {
    std::string f1 = write_temp("flt_s.txt", "NaN\n");
    std::string f2 = write_temp("flt_t.txt", "NaN\n");
    CHECK(compare_output_floating(f1, f2), "floating: NaN treated as string, matches");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_floating_inf() {
    std::string f1 = write_temp("flt_u.txt", "inf\n");
    std::string f2 = write_temp("flt_v.txt", "inf\n");
    CHECK(compare_output_floating(f1, f2), "floating: inf treated as string, matches");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_floating_nonexistent_file() {
    CHECK(!compare_output_floating("/tmp/nonex123_a.txt", "/tmp/nonex123_b.txt"),
          "floating: nonexistent files return false");
}

static void test_floating_default_eps() {
    std::string f1 = write_temp("flt_w.txt", "3.141592\n");
    std::string f2 = write_temp("flt_x.txt", "3.141593\n");
    CHECK(compare_output_floating(f1, f2), "floating: default eps (1e-6) works for close values");
    unlink(f1.c_str()); unlink(f2.c_str());
}

// ── dispatch tests ──────────────────────────────────────

static void test_dispatch_exact() {
    std::string f1 = write_temp("dsp_a.txt", "hello\n");
    std::string f2 = write_temp("dsp_b.txt", "hello");
    CHECK(compare_output(f1, f2, CompareMode::EXACT, 1e-6, 1e-6), "dispatch: explicit EXACT");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_dispatch_floating() {
    std::string f1 = write_temp("dsp_c.txt", "3.14\n");
    std::string f2 = write_temp("dsp_d.txt", "3.14\n");
    CHECK(compare_output(f1, f2, CompareMode::FLOATING, 1e-6, 1e-6), "dispatch: explicit FLOATING");
    unlink(f1.c_str()); unlink(f2.c_str());
}

static void test_dispatch_default_mode() {
    std::string f1 = write_temp("dsp_e.txt", "x\n");
    std::string f2 = write_temp("dsp_f.txt", "x\n");
    CHECK(compare_output(f1, f2), "dispatch: default (2-arg) uses exact");
    unlink(f1.c_str()); unlink(f2.c_str());
}

int main() {
    printf("=== compare_mode conversion ===\n");
    test_compare_mode_conversion();

    printf("=== exact comparison ===\n");
    test_exact_identical();
    test_exact_trailing_newline();
    test_exact_trailing_spaces();
    test_exact_middle_spaces_differ();
    test_exact_multiline();
    test_exact_empty();
    test_exact_case_different();
    test_exact_extra_token();
    test_exact_nonexistent_file();

    printf("=== floating comparison ===\n");
    test_floating_exact_equal();
    test_floating_within_abs_eps();
    test_floating_within_rel_eps();
    test_floating_exceeds_eps();
    test_floating_negative();
    test_floating_near_zero();
    test_floating_scientific();
    test_floating_non_numeric_tokens();
    test_floating_token_count_mismatch();
    test_floating_nan();
    test_floating_inf();
    test_floating_nonexistent_file();
    test_floating_default_eps();

    printf("=== dispatch ===\n");
    test_dispatch_exact();
    test_dispatch_floating();
    test_dispatch_default_mode();

    if (failures > 0) {
        fprintf(stderr, "\n%d test(s) FAILED\n", failures);
        return 1;
    }
    printf("\nAll comparer tests passed.\n");
    return 0;
}
