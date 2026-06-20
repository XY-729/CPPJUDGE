# CPPJUDGE Security Test Matrix

Updated: 2026-06-20

## Current verification rule

Security results must distinguish three states:

- **PASS**: test actually ran and passed.
- **SKIPPED / NOT_VERIFIED**: test did not run because the environment was missing required capability.
- **KNOWN_GAP**: behavior is intentionally documented as not yet product-grade.

Do not treat skipped nsjail/seccomp tests as product security proof.

## 2026-06-20 VM result snapshot

| Area | Status | Evidence |
|------|--------|----------|
| portable build + tests | PASS | `bash scripts/run_all_tests.sh portable` -> 12/12 passed |
| builtin security regression | PASS | `bash scripts/run_all_tests.sh security` -> `test_regression_security` passed |
| nsjail integration | NOT_VERIFIED | nsjail tests skipped because current SSH session lacks cgroup delegation |
| nsjail MUST_BLOCK security | NOT_VERIFIED | skipped |
| nsjail KNOWN_GAP observation | NOT_VERIFIED | skipped |
| seccomp security | NOT_VERIFIED | skipped |

Environment check:

```text
nsjail: found
cgroup v2: detected
nsjail cgroup flags: available
cgroup write permission: no
```

## MUST_BLOCK

These must pass in a delegated nsjail environment before CPPJUDGE can claim product-grade sandbox behavior.

| Attack | Expected defense | Current test | Current status |
|--------|------------------|--------------|----------------|
| Read host canary file | chroot + controlled bind mounts | `tests/security/test_nsjail_must_block.sh` | NOT_VERIFIED |
| Write host canary file | chroot + read-only/absent host paths | `tests/security/test_nsjail_must_block.sh` | NOT_VERIFIED |
| Access procfs | `--disable_proc` | `tests/security/test_nsjail_must_block.sh` | NOT_VERIFIED |
| Connect to host TCP port | network namespace | `tests/security/test_nsjail_must_block.sh` | NOT_VERIFIED |
| Modify solution binary | read-only bind mount | `tests/security/test_nsjail_must_block.sh` | NOT_VERIFIED |
| Write forbidden paths | chroot + mount policy | `tests/security/test_nsjail_must_block.sh` | NOT_VERIFIED |
| Read injected environment | cleared/controlled env | `tests/security/test_nsjail_must_block.sh` | NOT_VERIFIED |
| Read leaked file descriptors | fd closing before exec | `tests/security/test_nsjail_must_block.sh` | NOT_VERIFIED |
| Leave orphan processes | cgroup kill + process group cleanup | `tests/security/test_nsjail_must_block.sh` | NOT_VERIFIED |
| High-risk syscalls | seccomp policy | `tests/security/test_seccomp_security.sh` | NOT_VERIFIED |

## Implemented but environment-gated

| Mechanism | Code status | Verification status |
|-----------|-------------|---------------------|
| cgroup v2 manager | Implemented | unit tests pass; real delegated run NOT_VERIFIED |
| `memory.max` | Implemented in run cgroup setup | NOT_VERIFIED in real nsjail run |
| `pids.max` | Implemented in run cgroup setup | NOT_VERIFIED in real nsjail run |
| `memory.events` / `oom_kill` | Implemented for verdict classification | NOT_VERIFIED in real nsjail run |
| `cgroup.kill` cleanup | Implemented with fallback | NOT_VERIFIED in real nsjail run |
| seccomp policy path validation | Implemented | NOT_VERIFIED in real nsjail run |
| seccomp deny-list policy | Implemented | NOT_VERIFIED in real nsjail run |

## KNOWN_GAP

| Gap | Current state | Product-grade target |
|-----|---------------|----------------------|
| seccomp strategy | deny-list + `DEFAULT ALLOW` | versioned allow-list policy |
| runtime rootfs | per-run root with selected library bind mounts | fixed minimal runtime rootfs |
| compile rootfs | host `/usr`, `/lib64`, `/lib`, `/bin` mounted read-only | fixed compile rootfs |
| low-privilege identity | not yet a stable UID/GID deployment contract | dedicated judge user and nsjail UID/GID model |
| delegated cgroup runner | code exists, current SSH session lacks permission | repeatable systemd delegated test entry |
| schema stability | useful JSON log exists | versioned `judge_log.json` schema |
| operations | scripts exist | `cppjudge doctor`, install, troubleshooting, cleanup |

## Required security gate before product claim

A future release may call the nsjail path product-grade only after:

1. `bash scripts/run_all_tests.sh portable` passes.
2. `bash scripts/run_all_tests.sh nsjail` passes with zero skipped tests.
3. `bash scripts/run_all_tests.sh security` passes with zero skipped nsjail/seccomp tests.
4. The test environment is documented as delegated via systemd or equivalent.
5. Rootfs, seccomp policy, cgroup delegation, low-privilege identity, and log schema are versioned or explicitly documented.
