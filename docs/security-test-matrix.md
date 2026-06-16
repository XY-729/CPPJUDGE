# CPPJUDGE NSJail Security Test Matrix

Updated: 2026-06-16

## MUST_BLOCK

Tests that must pass for the nsjail sandbox to be considered functional.

| Attack | Mechanism | Category | Test Method | Pass Criterion |
|--------|-----------|----------|-------------|----------------|
| Read host canary file | chroot + bind mount isolation | MUST_BLOCK | Submission attempts open/read of host canary; stderr marker check | Canary content not leaked; host file unchanged |
| Write host canary file | chroot + bind mount isolation | MUST_BLOCK | Submission attempts open/write to host canary path | Host file content unchanged after run |
| Access /proc | nsjail --disable_proc | MUST_BLOCK | Submission reads /proc/self/status, /proc/1/status, /proc/mounts | All reads fail; PROC_BLOCKED in stderr |
| Connect to host TCP port | nsjail network namespace (clone_newnet) | MUST_BLOCK | Host runs continuous TCP server; host probe confirmed; submission tries connect() with unique sandbox token | connect() fails; server receives host probe but not sandbox token; server alive after host probe |
| Modify solution binary | bind mount read-only | MUST_BLOCK | Submission attempts O_WRONLY open, truncate, unlink, rename, chmod on /sandbox/solution | All 5 operations fail; SOLUTION_BLOCKED in stderr; five independent forbidden markers (SOLUTION_WRITABLE, SOLUTION_TRUNCATED, SOLUTION_UNLINKED, SOLUTION_RENAMED, SOLUTION_CHMODDED) each checked separately |
| Write to forbidden paths | chroot isolation | MUST_BLOCK | Submission attempts write to /etc, /bin, / paths | All writes fail; no leaked files on host |
| Read injected env vars | nsjail env isolation | MUST_BLOCK | Host exports CPPJUDGE_SECRET_CANARY before judge; submission calls getenv() | Canary not found in environment |
| Read extra file descriptors | fd closing before exec | MUST_BLOCK | Host opens fd 9 to canary file; submission reads fds 3-64 | Canary not found via any leaked fd |
| Leave orphan child processes | process group SIGKILL cleanup | MUST_BLOCK | Submission creates 4 long-lived children without PDEATHSIG, each with unique PR_SET_NAME token; parent does not wait and exits normally; after judge returns, pgrep -x for token verifies zero residual processes | Zero processes matching unique token remain; host sanity check confirms fixture produces orphans without self-cleaning |

## KNOWN_GAP

Known limitations pending cgroup v2, seccomp, and rootfs hardening.

| Attack | Current Mechanism | Category | Test Method | Current Behavior |
|--------|-------------------|----------|-------------|------------------|
| AF_INET socket() | RLIMIT_NOFILE only | KNOWN_GAP | Submission creates AF_INET SOCK_STREAM socket | socket() likely succeeds; connect() blocked by net ns |
| AF_UNIX socket() | RLIMIT_NOFILE only | KNOWN_GAP | Submission creates AF_UNIX SOCK_STREAM socket | socket() likely succeeds |
| Fork bomb | RLIMIT_NPROC | KNOWN_GAP | Submission attempts 64 forks | Limited by RLIMIT_NPROC but no precise pids cgroup |
| Thread storm | No specific limit | KNOWN_GAP | Submission creates up to 64 pthreads | No thread limit enforced; all may succeed |
| Execute /bin/sh | Rootfs not hardened | KNOWN_GAP | Submission tries execl(/bin/sh) and system() | Depends on rootfs contents; not blocked by seccomp |
| Strict syscall whitelist | Not implemented | KNOWN_GAP | seccomp not yet integrated | All non-privileged syscalls available |
| Precise memory OOM | RLIMIT_AS only | KNOWN_GAP | Large allocation | RLIMIT_AS approximate; cgroup memory.max needed |
| Minimal rootfs | Dynamic library bind mounts | KNOWN_GAP | Filesystem enumeration | Many system paths visible via bind mounts |

## BASELINE_ONLY

Recorded for documentation; not security guarantees.

| Behavior | Current Status |
|----------|---------------|
| /bin/sh presence | Depends on rootfs bind mounts |
| system() success | Depends on /bin/sh availability |
| AF_UNIX socket creation | Not restricted by seccomp |
| Thread creation limit | No limit enforced |
| Process creation limit | RLIMIT_NPROC approximate |
