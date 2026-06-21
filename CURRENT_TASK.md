# Current Task: Security harness exit-code compatibility

**TASK_ID**: CPPJUDGE_P1_SECURITY_HARNESS_EXIT_CODE_FIX_001

**STATUS**: COMPLETE

**MODE**: SMALL_TEST_HARNESS_FIX_WITH_VERIFICATION

**SOURCE_EDIT_AUTHORIZATION**: TEST_HARNESS_ONLY

**TEST_EDIT_AUTHORIZATION**: ALLOWED

**DOC_EDIT_AUTHORIZATION**: REPORTS_ONLY

**GIT_WRITE_AUTHORIZATION**: NO_COMMIT_NO_PUSH

## Objective

Fix the security regression harness so it is compatible with P1 exit-code semantics:

- Accepted verdict => exit 0
- non-Accepted verdict => exit 1
- CLI argument error / doctor NOT_READY or NOT_VERIFIED => exit 2
- System Error / tool error => exit 3

The fix must not change runner, cgroup, nsjail, seccomp, or CLI semantics.

## Completion State

Completed:

1. Updated `scripts/run_security_tests.sh::run_security_case`.
2. The harness now captures cppjudge exit status explicitly.
3. The harness checks `judge_log.json` final_verdict against each case's allowed verdicts.
4. Exit 1 is accepted only for verified non-Accepted verdicts.
5. Exit 3 is accepted only for verified System Error verdicts.
6. Exit 0 is required for Accepted verdicts.
7. Exit 2 remains a failure for these security judge cases.
8. No runner/cgroup/nsjail/seccomp/CLI implementation files were modified.

## Verification Performed

```bash
cmake -S . -B build
cmake --build build -j2
bash scripts/run_all_tests.sh portable
systemd-run --user --scope -p Delegate=yes bash scripts/run_all_tests.sh nsjail
systemd-run --user --scope -p Delegate=yes bash scripts/run_all_tests.sh security
./build/cppjudge --version
./build/cppjudge doctor
./build/cppjudge judge --problem problems/A+B --submission submissions/solution.cpp
python3 -m json.tool build/judge_log.json >/tmp/cppjudge_log_check.json
./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 exact 5000
```

Confirmed:

- BUILD: PASS
- PORTABLE_TESTS: PASS, 16/16
- DELEGATED_NSJAIL: PASS, 5/5
- DELEGATED_SECURITY: PASS, 4/4
- SECURITY_HARNESS_EXIT_CODE_FIX: PASS
- CLI_SMOKE: PASS
- JSON_VALID: PASS
- NO_RUNNER_CGROUP_SECCOMP_CHANGE: PASS
- EXCLUDED_SANDBOX_RULE_UNTOUCHED: PASS

## Scope Boundaries

This task intentionally did not modify:

- `src/runner.cpp`
- `src/runner.h`
- `src/cgroup_v2.cpp`
- `src/cgroup_v2.h`
- `src/seccomp_config.cpp`
- `src/seccomp_config.h`
- `sandbox/seccomp/`
- `src/cli.cpp`
- `src/judge.cpp`

The existing `.claude/rules/sandbox.md` working-tree change was preserved and not modified.

## Next Recommended Task

`CPPJUDGE_P1_FINAL_COMMIT_REVIEW_001`: final diff review and proposed commit plan, still without git write operations unless explicitly approved.
