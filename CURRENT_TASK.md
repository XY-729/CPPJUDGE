# Current Task: Repository Audit, Documentation Refresh, and Branch Cleanup Plan

**TASK_ID**: REPO-AUDIT-DOC-REFRESH-2026-06-20

**STATUS**: IN_PROGRESS

**SOURCE_EDIT_AUTHORIZATION**: DOCS_ONLY

**TEST_EDIT_AUTHORIZATION**: READ_ONLY

**DOC_EDIT_AUTHORIZATION**: ALLOWED

**GIT_WRITE_AUTHORIZATION**: CONFIRM_BEFORE_BRANCH_DELETE_OR_MERGE

## Objective

Audit the current CPPJUDGE project state, identify gaps between the current implementation and a product-grade judge, refresh stale project progress documentation, replace `docs/OVERVIEW.md` with a forward-looking project outline, inspect Claude/prompt control files, and prepare a safe branch cleanup / merge plan.

## Current Findings

- Current VM branch: `stage3d-rootfs`.
- Current VM HEAD: `7e69963 feat: enforce seccomp policy in nsjail sandbox`.
- GitHub `master` is behind the VM branch.
- GitHub `codex/judge-architecture-tests` is already contained in GitHub `master`.
- GitHub `claudeworker` and `stage2-testing` are contained in VM `stage3d-rootfs`, but not yet in GitHub `master`.
- `stage3d-rootfs` exists on the VM/local clone but is not present as a GitHub branch.

## Verification Performed

```bash
bash scripts/check_nsjail_env.sh
bash scripts/run_all_tests.sh portable
bash scripts/run_all_tests.sh nsjail
bash scripts/run_all_tests.sh security
```

Confirmed:

- portable profile: PASS, 12/12 tests passed.
- builtin security regression: PASS.
- nsjail/security/seccomp tests: NOT_VERIFIED in current SSH session because cgroup delegation is unavailable and those tests were skipped.
- cgroup v2 and nsjail flags exist, but current user cannot create child cgroups under `/sys/fs/cgroup`.

## Scope Boundaries

Allowed now:

- Update docs and progress reports.
- Add product readiness audit documentation.
- Inspect local/remote branches and compute ancestry.
- Prepare exact branch deletion and merge recommendations.

Requires confirmation before execution:

- Delete GitHub branches.
- Push to GitHub.
- Merge current development branch into `master`.
- Remove local VM branches.
- Delete ignored build artifacts from the VM.

## Files to Keep Current

- `docs/OVERVIEW.md`
- `docs/product-readiness-audit.md`
- `PROGRESS.md`
- `LAST_TASK_REPORT.md`
- `CLAUDE.md`
- `.claude/rules/documentation.md`

## Recommended Next Decision

Confirm one of the following merge paths:

1. Directly merge `stage3d-rootfs` into `master` and push `master`.
2. Push a temporary GitHub branch from `stage3d-rootfs`, open/inspect a PR, then merge.

After the merge target is confirmed, obsolete branches can be deleted in this order:

1. `codex/judge-architecture-tests`
2. `claudeworker`
3. `stage2-testing`
