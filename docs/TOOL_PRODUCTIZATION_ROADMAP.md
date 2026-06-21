# CPPJUDGE Tool Productization Roadmap

## 1. Project Positioning

CPPJUDGE is a local, CI-embeddable, batch-oriented, default-safe, diagnosable and reproducible Linux C++ CLI judging tool. It is not an Online Judge platform.

Productization must stay focused on Linux, C++, CLI, local judging, CI integration, nsjail, cgroup v2, seccomp, reproducibility and diagnostics.

## 2. Current Code-Based Findings

Audit target: Rocky VM, /home/xiyuan729/cppjudge, branch master, commit bdf90e9.

Verified paths exist and were read: CMakeLists.txt, src/, include/, sandbox/, scripts/, tests/, deploy/, .github/, problems/, submissions/.

Build system:
- CMake builds cppjudge_lib from judge.cpp, compiler.cpp, runner.cpp, comparer.cpp, cgroup_v2.cpp and seccomp_config.cpp.
- CMake builds cppjudge from main.cpp and links cppjudge_lib.
- CTest registers portable, integration, regression, cgroup-delegated, nsjail and security tests.
- cmake -S . -B build && cmake --build build passed on the VM.
- There is no install target, packaging target or version injection yet.

CLI and JSON:
- main.cpp only calls judge(argc, argv).
- Positional CLI is cppjudge <submission> <problem_dir> <time_ms> <mem_mb> <out_mb> <compare> <compile_time_ms>.
- No --help, --version, doctor/check-env subcommand, named flags or stable exit-code contract.
- JSON logs include run_id, run_dir, submission, problem_dir, limits, compare_mode, sandbox_type, per-case results, final_verdict, passed and total.
- JSON lacks schema_version, tool_version, stable machine-readable error codes and full sandbox/cgroup/seccomp diagnostics.

Problem schema:
- problem.json fields observed: title, time_limit_ms, memory_limit_mb, output_limit_mb, compile_time_limit_ms, sandbox_type, compare_mode, float_abs_eps, float_rel_eps.
- There is no schema version.
- Cases are discovered by scanning input/*.in and mapping to output/<stem>.out.
- std::filesystem::path sorting is lexicographic, so 10.in can sort before 2.in.
- No explicit case names, groups, per-case limits, sample/hidden flags or special judge extension point.

Compiler:
- Builtin compile runs g++ with -std=c++17 -O2, redirects stdout/stderr to compile_error.txt, kills process group on timeout and maps timeout/non-zero to CE.
- Builtin compile does not apply compile memory/file/process limits beyond timeout.
- nsjail compile is implemented with nsjail -Mo, chroot, cwd /work, disable_proc, rlimit_as/fsize/core/cpu/nofile/nproc and bind mounts.
- nsjail compile still depends on broad host paths such as /usr, /lib64, /lib, /bin and /etc/alternatives.

Runner and sandbox:
- Builtin is the default sandbox. It applies rlimits, clears environment, closes extra fds, redirects stdio and polls /proc/<pid>/status VmSize.
- Production mode rejects builtin when CPPJUDGE_PRODUCTION is enabled or CPPJUDGE_ENV starts with prod.
- nsjail runtime is implemented and called when sandbox_type=nsjail.
- nsjail runtime preflights cgroup v2 and seccomp, creates per-run cgroups, uses a parent/child barrier, and passes --seccomp_policy.
- Runtime nsjail args include chroot, cwd /sandbox, disable_proc, time/cpu/fsize/nofile/nproc limits, read-only solution bind, writable output bind and runtime library binds.
- Network and uid/gid boundaries are not explicit in the observed nsjail argument list.

cgroup v2:
- cgroup_v2.cpp discovers cgroup2 mount and /proc/self/cgroup, validates path containment, creates manager_<pid>, moves self, enables +memory +pids, creates per-run cgroups, writes memory.max, memory.swap.max and pids.max, reads memory.events(.local), reads memory.peak, kills and cleans run cgroups.
- scripts/probe_cgroup_v2.sh --verbose passed on the VM and found user@1000.service delegated with memory and pids, writable cgroup.procs/subtree_control, memory.events.local, cgroup.kill and memory.oom.group.
- scripts/check_nsjail_env.sh also ran, but checks root /sys/fs/cgroup write permission and reported no write permission, which conflicts with the delegated-root probe.

seccomp:
- SeccompConfig validates enabled policy path fail-closed for empty, traversal, missing, non-regular, unreadable or empty policy.
- Runtime nsjail adds --seccomp_policy when seccomp preflight succeeds.
- sandbox/seccomp/cppjudge-runtime.kafel is a deny-list with DEFAULT ALLOW, not a tight product-grade allow-list.

Verdicts and comparer:
- Runtime results: OK, TLE, MLE, OLE, RE, SE.
- nsjail classification priority: existing SE, parent TLE, parent OLE, cgroup oom_kill -> MLE, SIGXFSZ -> OLE, SIGXCPU -> TLE, exit 0 -> OK, non-zero/signal -> RE.
- cgroup OOM is intended to map to MLE.
- Exact compare trims trailing whitespace.
- Floating compare tokenizes whitespace, requires equal token count, rejects NaN/inf and nonnumeric tokens, and uses abs/rel eps.
- WA diagnostics are printed but not stored as structured bounded JSON details.
- No special judge extension exists.

Test execution:
- Portable profile passed: 12/12.
- Direct nsjail profile from SSH returned PASS but skipped all 5 nsjail-labelled tests.
- Direct security profile returned PASS but skipped nsjail security tests; only builtin security ran.
- Running tests inside systemd-run --user --scope -p Delegate=yes made nsjail tests execute, but nsjail profile failed: integration_nsjail, regression_nsjail and security_nsjail_must_block failed.
- Manual nsjail AC reproduction under delegated scope returned System Error:
  cgroup v2 error phase service_init, code=LIMIT_WRITE_FAILED, enable_controllers write failed on cgroup.subtree_control, errno=16 Device or resource busy.
- This means nsjail/cgroup/seccomp path is implemented but not product-ready.

## 3. Product-Level Gaps

P0 gaps (updated 2026-06-21 — see LAST_TASK_REPORT.md CPPJUDGE_P0_CGROUP_DELEGATED_NSJAIL_RUN_001):
- ~~nsjail production path fails in delegated scope with cgroup.subtree_control EBUSY.~~ RESOLVED. Delegation search in init_service() handles this correctly when run under systemd-run --user --scope -p Delegate=yes.
- ~~nsjail/security profiles can report PASS while all nsjail tests are skipped.~~ RESOLVED. run_all_tests.sh now detects all-skipped and reports NOT_VERIFIED (exit 2).
- builtin remains the default sandbox outside production mode. (REMAINS — P1)
- cgroup preflight errors are human-readable but not stable enough for automation. (REMAINS — P1)

P1 gaps:
- No --help, --version, doctor/check-env, named flags or exit-code contract.
- JSON lacks schema_version, tool_version and stable error taxonomy.
- check_nsjail_env.sh and probe_cgroup_v2.sh disagree because they check different cgroup roots.
- No install/release/version metadata.

P2 gaps:
- Runtime rootfs is generated per run and depends on host library binds.
- Seccomp is deny-list DEFAULT ALLOW.
- nsjail network and uid/gid boundaries are not explicit.
- Compile sandbox uses broad host binds and hardcoded limits.

P3 gaps:
- Comparer diagnostics are not structured/bounded in JSON.
- Verdict aggregation is first-failure style and not a documented severity policy.
- Cleanup failure is not surfaced strongly enough.

P4 gaps:
- problem.json is unversioned and case discovery is implicit.
- No case groups, per-case limits, sample/hidden flags or special judge hook.

P5 gaps:
- CI only verifies portable behavior on GitHub-hosted runners.
- No release artifact layout or release smoke test.

## 4. P0 Critical Fixes

### 4.1 Fix delegated cgroup initialization — RESOLVED (bdf90e9)

Root cause: systemd-run --user --scope -p Delegate=yes creates a scope cgroup
under app.slice. The scope has direct processes (shell harness), so enabling
domain controllers in the scope subtree_control fails with EBUSY.

Fix (already present in code): init_service() uses is_usable_delegation_parent()
to search upward for an ancestor cgroup that already has memory+pids enabled in
subtree_control. It finds app.slice, creates a private cppjudge_<pid> child
(empty, safe to enable controllers), then creates manager_<pid> and moves
cppjudge into it. Per-run cgroups live under manager_<pid>.

Verification:
- systemd-run --user --scope -p Delegate=yes bash scripts/run_nsjail_tests.sh: 9/9 PASS
- systemd-run --user --scope -p Delegate=yes bash scripts/run_all_tests.sh security: 4/4 PASS

### 4.2 Make all-skipped profiles NOT_VERIFIED — RESOLVED

run_all_tests.sh now:
- Captures CTest output via tee
- Counts Passed (executed) and Skipped tests from CTest status lines
- Reports NOT_VERIFIED (exit 2) when TOTAL_PASSED=0 and TOTAL_SKIPPED>0
- Reports PASS only when at least one test executed and none failed

### 4.3 Make builtin explicitly unsafe/dev-only — DEFERRED to P1

Production mode already rejects builtin when CPPJUDGE_PRODUCTION is set.
Making this the default for untrusted submissions is a P1 task.

### 4.4 Stabilize SE diagnostics — DEFERRED to P1

cgroup_v2.cpp already has cgroup_diag() with cgroup.type, cgroup.controllers,
cgroup.subtree_control, and cgroup.procs_count. Structured JSON diagnostics
remain a P1 task.

## 5. P1 Stabilization Tasks

- Add cppjudge --help, --version and doctor.
- Convert existing check scripts into doctor-backed wrappers.
- Align check_nsjail_env.sh with delegated cgroup probing.
- Add JSON schema_version and tool_version.
- Add sandbox, limits, environment and errors[] objects to JSON.
- Define exit codes for AC, judged failure, CE, SE, invalid CLI and internal failure.
- Add CMake install target and version metadata.
- Add CI job or documented self-hosted workflow for nsjail/cgroup/seccomp verification.

## 6. P2 Sandbox Hardening Tasks

- Build or ship a fixed audited rootfs instead of per-run placeholder rootfs.
- Reduce broad host bind mounts.
- Make network namespace policy explicit.
- Make uid/gid mapping explicit.
- Replace deny-list seccomp with allow-list policy.
- Add positive and negative seccomp tests.
- Decide whether compile gets cgroup limits or nsjail rlimits only.
- Promote cleanup failures into structured diagnostics.

## 7. P3 Diagnostics and Judge Correctness

- Add structured comparer diagnostics to JSON.
- Bound diff/snippet length.
- Add line/token location for WA.
- Define cross-case final verdict priority.
- Add boundary tests for OLE.
- Add MLE tests for cgroup OOM, RLIMIT_AS and caught allocation failure.
- Add TLE-vs-MLE priority tests.
- Verify no leftover processes or cgroups after TLE/OLE/MLE.

## 8. P4 Problem Schema Evolution

- Add problem.json schema_version.
- Add optional explicit cases[] manifest.
- Define natural sort or require explicit order.
- Add case name, input, output, group, limits, sample and hidden fields.
- Reserve special judge extension fields without turning CPPJUDGE into an OJ platform.
- Define unknown-field policy by schema version.

## 9. P5 Release and Packaging

- Add install target for binary, seccomp policy, scripts and docs.
- Generate version from git tag/commit.
- Define release artifact layout.
- Add release smoke tests for doctor, builtin trusted sample, delegated nsjail sample and JSON schema.
- Document Linux requirements: kernel, cgroup v2, systemd delegation, nsjail and Kafel/seccomp support.

## 10. Explicit Non-Goals

Do not build:
- Web frontend
- User system
- Login or registration
- Contest system
- Ranking board
- Database platform
- Distributed OJ
- Multi-tenant platform
- Online submission platform

## 11. Recommended Next Implementation Task

TASK_ID: CPPJUDGE_P1_CLI_JSON_STABILIZATION

P0 blocking items for delegated cgroup/nsjail/seccomp are resolved.
Next priority is P1 stabilization:

Scope:
- Add cppjudge --help, --version and doctor subcommands.
- Add JSON schema_version, tool_version and structured error codes.
- Add CMake install target.
- Align check_nsjail_env.sh with delegated cgroup probing.
- Define stable exit-code contract.

Previous P0 task (CPPJUDGE_P0_CGROUP_DELEGATED_NSJAIL_RUN_001): COMPLETED.
