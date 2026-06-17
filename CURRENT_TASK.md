# Current Task: Stage 3A — cgroup v2 Memory and Process Control

**TASK_ID**: STAGE-3A-CGROUP-V2
**STATUS**: PLANNED
**SOURCE_EDIT_AUTHORIZATION**: ALLOWED
**TEST_EDIT_AUTHORIZATION**: ALLOWED
**DOC_EDIT_AUTHORIZATION**: ALLOWED
**GIT_WRITE_AUTHORIZATION**: ALLOWED

**PREVIOUS_STATE**: AWAITING_STAGE_3_TASK
**NEXT_STATE**: (defined by task outcome)

## Summary

Design and implement per-run cgroup v2 lifecycle in the nsjail runner,
replacing current stderr-based MLE/TLE/OLE classification with trusted
kernel metrics.

## Goals

1. Investigate current nsjail and builtin resource limiting and process cleanup paths.
2. Design a per-run cgroup v2 lifecycle (create before run, cleanup after).
3. Use `memory.max` to enforce hard memory limit.
4. Use `memory.peak` to record peak memory usage.
5. Use `memory.events` (`oom` / `oom_kill` counters) to reliably detect MLE.
6. Use `pids.max` to limit total process and thread count.
7. Use `cgroup.kill` to clean up residual processes on timeout/limit.
8. Fail closed with structured SE when required controllers or permissions are absent.
9. Remove stderr-based MLE classification from nsjail runner.
10. Preserve existing test baselines; add cgroup regression and attack tests.

## Boundaries

- Prioritize design, environment probing, and a minimal closed loop.
- Do NOT simultaneously work on seccomp, fixed rootfs, CLI refactoring, or multi-worker.
- Do NOT use `system()`.
- Do NOT introduce unsafe fork patterns in multi-threaded processes.
- Do NOT silently fall back to unsafe modes when cgroup is unavailable.
- Environment checks that fail should return structured SE, not degrade silently.

## Suggested Files to Review

```
src/runner.cpp
src/judge.cpp
src/config.h
include/
tests/
scripts/run_nsjail_tests.sh
scripts/run_security_tests.sh
docs/nsjail-plan.md
docs/security-test-matrix.md
```

## Baseline Tests

```bash
bash scripts/run_all_tests.sh portable
bash scripts/run_all_tests.sh nsjail
bash scripts/run_all_tests.sh security
```

Actual commands are defined in `docs/TESTING.md` and the test scripts themselves.
