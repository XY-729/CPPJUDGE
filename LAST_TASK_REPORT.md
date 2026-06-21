TASK_ID:
CPPJUDGE_P1_SECURITY_HARNESS_EXIT_CODE_FIX_001

MODE:
SMALL_TEST_HARNESS_FIX_WITH_VERIFICATION

GIT_COMMIT:
10d2bdd (working tree changes not committed)

FILES_READ:
scripts/run_security_tests.sh; tests/security/test_nsjail_known_gaps.sh;
tests/security/test_nsjail_must_block.sh; tests/security/test_seccomp_security.sh;
tests/support/security_test_helpers.sh; tests/support/skip_unless_cgroup_delegated.sh;
tests/support/test_helpers.sh; scripts/run_all_tests.sh; LAST_TASK_REPORT.md

FILES_MODIFIED:
scripts/run_security_tests.sh; LAST_TASK_REPORT.md; PROGRESS.md; CURRENT_TASK.md

COMMANDS_RUN:
pwd; git status --short; git branch --show-current; git rev-parse --short HEAD;
git diff --stat; git diff -- .claude/rules/sandbox.md;
sed -n '1,260p' scripts/run_security_tests.sh;
find tests/security -type f -maxdepth 2 -print -exec sed -n '1,220p' {} \;;
find tests/support -type f -maxdepth 2 -print -exec sed -n '1,220p' {} \;;
sed -n '1,260p' scripts/run_all_tests.sh;
sed -n '1,220p' LAST_TASK_REPORT.md;
git diff --check -- scripts/run_security_tests.sh tests/security tests/support LAST_TASK_REPORT.md PROGRESS.md CURRENT_TASK.md;
cmake -S . -B build; cmake --build build -j2;
bash scripts/run_all_tests.sh portable;
systemd-run --user --scope -p Delegate=yes bash scripts/run_all_tests.sh nsjail;
systemd-run --user --scope -p Delegate=yes bash scripts/run_all_tests.sh security;
./build/cppjudge --version;
./build/cppjudge doctor;
./build/cppjudge judge --problem problems/A+B --submission submissions/solution.cpp;
python3 -m json.tool build/judge_log.json >/tmp/cppjudge_log_check.json;
./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 exact 5000

BUILD_RESULT:
PASS. cmake -S . -B build && cmake --build build -j2 completed successfully.

PORTABLE_TEST_RESULT:
PASS. bash scripts/run_all_tests.sh portable completed successfully: 16/16 tests passed.

DELEGATED_NSJAIL_RESULT:
PASS. systemd-run --user --scope -p Delegate=yes bash scripts/run_all_tests.sh nsjail:
5/5 tests passed, 0 skipped.

DELEGATED_SECURITY_RESULT:
PASS. systemd-run --user --scope -p Delegate=yes bash scripts/run_all_tests.sh security:
4/4 tests passed, including test_regression_security.

SECURITY_HARNESS_FIX_RESULT:
PASS. scripts/run_security_tests.sh now treats cppjudge exit code as judge status metadata,
not as the sole pass/fail signal. It accepts exit 1 only after verifying judge_log.json
contains an allowed non-Accepted verdict, accepts exit 3 only for System Error verdicts,
requires exit 0 for Accepted, and still rejects timeout, CLI exit 2, and unexpected statuses.

CLI_SMOKE_RESULT:
PASS. --version exits 0; doctor runs and reports NOT_VERIFIED/exit 2 in plain SSH;
new user CLI returns Accepted/exit 0; old positional CLI returns Accepted/exit 0.

JSON_VALIDATION_RESULT:
PASS. python3 -m json.tool build/judge_log.json succeeded.

KEY_DIFFS:
- Updated scripts/run_security_tests.sh::run_security_case to explicitly capture cppjudge exit status.
- The harness now validates judge_log.json final_verdict against per-case allowed verdicts.
- Exit code 1 is accepted only for verified non-Accepted verdicts.
- Exit code 3 is accepted only for verified System Error verdicts.
- Exit code 2 remains a harness failure for these security judge cases.
- No runner/cgroup/nsjail/seccomp/CLI implementation changes were made.

RISKS:
- Working tree still contains the full uncommitted P1 series and the unrelated .claude/rules/sandbox.md diff.
- The security harness now depends on the P1 exit-code contract remaining stable.
- Plain SSH doctor still reports NOT_VERIFIED because delegated cgroup write is unavailable there; delegated profiles were verified with systemd-run.

NEXT_TASK_RECOMMENDATION:
CPPJUDGE_P1_FINAL_COMMIT_REVIEW_001 — rerun final diff review, confirm .claude/rules/sandbox.md exclusion,
then ask for explicit approval before any git add/commit/push.

STATUS:
COMPLETED
