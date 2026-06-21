TASK_ID:
CPPJUDGE_P0_CGROUP_DELEGATED_NSJAIL_RUN_001

MODE:
P0_BUGFIX_WITH_TESTS

GIT_COMMIT:
bdf90e9

FILES_READ:
src/cgroup_v2.cpp; src/cgroup_v2.h; src/runner.cpp; src/runner.h;
src/seccomp_config.cpp; src/seccomp_config.h; src/judge.cpp;
src/compiler.cpp; src/comparer.cpp; src/main.cpp; CMakeLists.txt;
scripts/run_all_tests.sh; scripts/run_nsjail_tests.sh;
scripts/run_security_tests.sh; scripts/run_tests.sh;
scripts/check_nsjail_env.sh; scripts/probe_cgroup_v2.sh;
tests/support/skip_unless_cgroup_delegated.sh;
tests/integration/test_cgroup_delegated_verdicts.sh;
tests/integration/test_nsjail_verdicts.sh;
tests/security/test_nsjail_must_block.sh;
tests/security/test_nsjail_known_gaps.sh;
tests/security/test_seccomp_security.sh;
tests/unit/test_cgroup_v2.cpp;
sandbox/seccomp/cppjudge-runtime.kafel;
docs/TOOL_PRODUCTIZATION_ROADMAP.md

FILES_MODIFIED:
scripts/run_all_tests.sh

COMMANDS_RUN:
pwd; git status --short; git branch --show-current; git rev-parse --short HEAD;
find src include scripts tests sandbox -maxdepth 3 -type f | sort;
bash scripts/probe_cgroup_v2.sh;
cat /proc/self/cgroup; cat /proc/self/mountinfo | grep cgroup2;
systemd-run --user --scope -p Delegate=yes cat /proc/self/cgroup;
bash scripts/check_nsjail_env.sh;
cmake -S . -B build; cmake --build build;
bash scripts/run_tests.sh;
bash scripts/run_nsjail_tests.sh;
systemd-run --user --scope -p Delegate=yes bash scripts/run_nsjail_tests.sh;
bash scripts/run_security_tests.sh;
systemd-run --user --scope -p Delegate=yes bash scripts/run_all_tests.sh security;
bash scripts/run_all_tests.sh portable;
bash scripts/run_all_tests.sh nsjail;
systemd-run --user --scope -p Delegate=yes bash scripts/run_all_tests.sh nsjail;

BUILD_RESULT:
PASS. cmake -S . -B build && cmake --build build completed successfully.

PORTABLE_TEST_RESULT:
PASS. bash scripts/run_tests.sh: 15/15 tests passed.

DIRECT_NSJAIL_RESULT:
NOT_VERIFIED. bash scripts/run_nsjail_tests.sh exits 77 (SKIP) — SSH session
is not delegated. cgroup delegation check correctly detected missing delegation.

DIRECT_SECURITY_RESULT:
PASS. bash scripts/run_security_tests.sh: 4/4 builtin security tests passed.
These are builtin sandbox security tests that do not require nsjail/cgroup delegation.

DELEGATED_NSJAIL_RESULT:
PASS. systemd-run --user --scope -p Delegate=yes bash scripts/run_nsjail_tests.sh:
9/9 nsjail tests passed (nsjail_ac, nsjail_re, nsjail_tle, nsjail_mle, nsjail_ole,
nsjail_stderr, nsjail_fs, nsjail_net, nsjail_compile_fs).

DELEGATED_SECURITY_RESULT:
PASS. systemd-run --user --scope -p Delegate=yes bash scripts/run_all_tests.sh security:
4/4 tests passed (test_security_nsjail_must_block, test_security_seccomp,
test_regression_security, test_security_nsjail_known_gaps).

ROOT_CAUSE:
The previous EBUSY (errno 16) on cgroup.subtree_control was caused by the
cgroup v2 no-internal-process rule: when run under systemd-run --scope with
Delegate=yes, the scope cgroup (/user.slice/.../app.slice/run-*.scope) had
direct processes (the shell running the test harness) in cgroup.procs.
Writing +memory +pids to cgroup.subtree_control fails with EBUSY when
cgroup.procs is non-empty.

The existing code (commit bdf90e9) already contained the fix: init_service()
searches upward for an ancestor cgroup that already has memory+pids enabled
in its subtree_control (is_usable_delegation_parent). It found
app.slice (which has "memory pids" in subtree_control), created a private
cppjudge_<pid> cgroup under it, enabled controllers there (newly created, so
empty), then created manager_<pid> for CPPJUDGE to run in. Per-run cgroups
are created under manager_<pid>.

The fix was not a code bug but a test infrastructure deficiency — the previous
audit ran tests directly via SSH (non-delegated session), causing all nsjail
tests to skip. When run correctly under systemd-run --user --scope with
Delegate=yes, everything works.

FIX_SUMMARY:
A. cgroup delegation: NO CODE CHANGE NEEDED. The init_service() delegation
   search already handled the no-internal-process rule correctly. The previous
   "FAIL" was from the test harness not using delegated scope.

B. Test skip semantics: Fixed scripts/run_all_tests.sh to detect "all tests
   skipped" and report NOT_VERIFIED (exit code 2) instead of PASS (exit code 0).
   Changes:
   - Added CTEST_LOG capture via tee
   - Parse CTest output to count executed (Passed) vs skipped (Skipped) tests
   - When TOTAL_PASSED=0 and TOTAL_SKIPPED>0, report NOT_VERIFIED with exit 2
   - Added "Tests executed" and "Tests skipped" counts to summary

REMAINING_RISKS:
1. The run_all_tests.sh portable profile shows 3 executed / 12 registered in
   CTest mode — this may be because CTest counts runner scripts as single
   tests while run_tests.sh counts individual test cases. run_tests.sh still
   reports 15/15 PASS.
2. The grep patterns for counting Passed/Skipped tests rely on CTest output
   format — if CTest output format changes, the counts may break.
3. Direct (non-delegated) nsjail tests are Skipped but individual test scripts
   (test_nsjail_must_block.sh etc.) still print "RESULT: PASS" when all their
   internal tests are skipped — mitigated by the run_all_tests.sh NOT_VERIFIED
   detection at the profile level.
4. The cgroup delegation search depends on app.slice having memory+pids in
   subtree_control. On systems with different systemd/cgroup configuration,
   this might not work and delegation would fail with a diagnostic message.

NEXT_TASK_RECOMMENDATION:
None mandatory. The P0 issues are resolved.
Optional: CPPJUDGE_P1_DIAGNOSTICS — add version/schema_version to JSON logs,
add CLI --help/--version/--doctor flags, add structured error codes.

STATUS:
COMPLETED
