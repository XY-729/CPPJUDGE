# nsjail Integration Plan

This document describes the nsjail backend plan and current implementation
status for CPPJUDGE. The project now has an nsjail MVP runner, but the profile is
still being hardened and is not yet a final product-grade sandbox.

---

## Current nsjail Status

Current implementation status:

- nsjail MVP runner is implemented behind `sandbox_type = "nsjail"`.
- The builtin runner remains the default path.
- Each run uses a per-run `sandbox_root` under `build/runs/<run_id>/`.
- The compiled `solution` is bind-mounted read-only into the jail.
- The per-run `user_output` directory is bind-mounted read/write.
- A small C++-oriented set of dynamic-linker files is bind-mounted read-only.
- `procfs` is disabled inside the jail with `--disable_proc`.
- nsjail's default network namespace isolation is preserved and tested.
- Basic nsjail rlimits are applied:
  - `RLIMIT_AS`
  - `RLIMIT_FSIZE`
  - `RLIMIT_CORE`
  - `RLIMIT_CPU`
  - `RLIMIT_NOFILE`
  - `RLIMIT_NPROC`
- Manual nsjail tests cover:
  - AC
  - RE
  - TLE
  - MLE
  - OLE
  - stderr capture
  - filesystem isolation
  - network isolation

Known limitations:

- The dynamic library list is still minimal and C++ oriented.
- The nsjail memory limit is currently an `RLIMIT_AS` fallback, not precise
  cgroup memory accounting.
- There is no seccomp whitelist yet.
- There is no managed minimal rootfs yet.
- Low-privilege user mapping and production cgroup policy still need design and
  deployment work.

---

## cgroup v2 Precheck

cgroup v2 is deployment-specific. CPPJUDGE should not blindly add cgroup
arguments to `run_program_nsjail(...)` until the target machine has been checked.
Before enabling cgroup v2 memory or pids limits, verify:

- nsjail is installed and executable.
- nsjail supports the required cgroup flags:
  - `--use_cgroupv2`
  - `--cgroupv2_mount`
  - `--cgroup_mem_max`
  - `--cgroup_pids_max`
  - `--cgroup_cpu_ms_per_sec`
- The system has a cgroup v2 unified hierarchy mounted.
- `/sys/fs/cgroup/cgroup.controllers` is present and contains useful
  controllers.
- The judge runtime user can create or be delegated a child cgroup.
- Production deployment uses systemd or a dedicated judge user/service to grant
  cgroup permissions safely.

Current strategy:

- Keep the existing rlimit fallback working everywhere.
- Do not make cgroup availability a requirement for normal local judging.
- Use `scripts/check_nsjail_env.sh` to inspect a machine before enabling cgroup
  integration.
- Add `--use_cgroupv2`, `--cgroup_mem_max`, and `--cgroup_pids_max` only after
  the environment is confirmed.

## cgroup v2 Dry-run Argument Preview

Before changing `build_nsjail_args(...)`, CPPJUDGE provides a dry-run helper:

```bash
bash scripts/preview_nsjail_cgroup_args.sh [memory_limit_mb] [pids_limit] [cpu_ms_per_sec]
```

The script prints the nsjail cgroup arguments that a future runner integration
would add, for example:

```text
--use_cgroupv2
--cgroupv2_mount /sys/fs/cgroup
--cgroup_mem_max <memory_limit_bytes>
--cgroup_pids_max <pids_limit>
--cgroup_cpu_ms_per_sec <cpu_ms_per_sec>
```

This is intentionally a preview only:

- it does not execute nsjail;
- it does not modify `runner.cpp`;
- it does not require cgroup write access to succeed;
- it keeps the current rlimit fallback as the actual runner behavior.

The dry-run step is useful for reviewing parameter shape and deployment
assumptions before cgroup v2 is wired into `run_program_nsjail(...)`.

## 1. Background / 背景说明

CPPJUDGE currently uses the `builtin` runner as its default runner backend. The
builtin runner is a basic local sandbox wrapper. It already supports:

- time limits
- memory limits
- output limits
- stdin/stdout/stderr redirection
- process-group cleanup with `SIGKILL`
- selected `rlimit` protections

However, the builtin runner is not a product-grade security sandbox. It does
not provide:

- Linux namespace isolation
- cgroup isolation
- seccomp syscall filtering
- chroot / pivot_root filesystem isolation
- network namespace isolation
- dedicated low-privilege user isolation

nsjail is planned as a future backend to improve process, filesystem, network,
and resource isolation for running untrusted submissions.

---

## 2. nsjail Runner Goals / nsjail runner 目标

The future `run_program_nsjail(...)` should preserve the public runner contract
and eventually provide the following behavior:

- run the compiled user program from `executable_file`
- read stdin from `input_file`
- write stdout to `output_file`
- write stderr to `output_file + .err`
- apply `time_limit_ms`
- apply `memory_limit_mb`
- apply `output_limit_mb`
- disable networking by default unless explicitly enabled
- limit the number of user-created processes
- restrict the visible filesystem to required paths only
- return `RunInfo` with:
  - `RunResult`
  - `time_ms`
  - `memory_mb`

The judge layer should continue to decide final verdicts such as AC, WA, RE,
TLE, MLE, OLE, and System Error.

---

## 3. Directory Model / 目录模型

CPPJUDGE already creates an isolated run directory for each judge run:

```text
build/runs/<run_id>/
  solution
  compile_error.txt
  judge_log.json
  user_output/
```

For nsjail execution, only the minimum required paths should be mounted into the
jail:

- the directory containing `executable_file`
- the directory containing `input_file`, mounted read-only
- the per-run `user_output` directory, mounted writable
- required system library directories, mounted read-only

The goal is for the user program to see only what it needs to execute and write
its declared outputs.

---

## 4. stdin/stdout/stderr Design

The runner should preserve the current I/O behavior:

- stdin reads from `input_file`
- stdout writes to `output_file`
- stderr writes to `output_file + .err`

stdout is used by the comparer for AC/WA decisions. stderr is not compared
against the answer; it is kept for debugging RE/MLE/OLE/System Error cases.

The implementation can either redirect file descriptors before `execvp(nsjail,
...)` or configure nsjail to handle the same redirection. The final behavior
must match the builtin runner.

---

## 5. Filesystem Isolation Design

Future nsjail support will likely need global judge-level configuration such as:

- `sandbox_root`
- `sandbox_work_dir`
- `readonly_paths`
- `writable_paths`

These values are environment and deployment dependent, so they fit better in a
global judge configuration file than in `problem.json`.

`problem.json` should stay problem-focused. It is appropriate for:

- time and memory limits
- output limit
- compare mode
- floating-point epsilon
- `sandbox_type`

It should not directly own host mount policies or global filesystem isolation
rules.

---

## 6. Network Isolation Design

Networking should be disabled by default:

```text
enable_network = false
```

Online judge submissions should not be able to access the network unless a
future trusted/internal use case explicitly enables it. The normal public OJ
case should keep network access off.

---

## 7. Resource Limit Design

Resource limits should map from CPPJUDGE config to nsjail/cgroup/rlimit behavior:

- `time_limit_ms`: mapped to nsjail wall-time or time-limit parameters, with the
  judge still recording elapsed time.
- `memory_limit_mb`: eventually enforced by nsjail with cgroup support or an
  equivalent memory limit mechanism.
- `output_limit_mb`: can continue to use `RLIMIT_FSIZE`, an outer file-size
  check, or both.
- `max_processes`: should limit fork-heavy programs and fork-bomb attempts.

The builtin runner millisecond monitoring and `/proc` memory checks are useful
for local testing. The nsjail backend should eventually prefer stronger kernel
isolation primitives where available.

---

## 8. Error Classification / 错误分类

The nsjail backend should preserve CPPJUDGE verdict semantics:

- User program exits normally but output differs: WA, handled by the comparer.
- User program exits non-zero or crashes: RE.
- User program exceeds time limit: TLE.
- User program exceeds memory limit: MLE.
- User program writes too much output: OLE.
- nsjail fails to start: System Error.
- nsjail config is invalid: System Error.
- nsjail is missing or not executable: System Error.
- input/output mount setup fails: System Error.

The key distinction is user failure versus judge/sandbox/environment failure.
User-program failures should become Runtime Error where appropriate. Sandbox
startup, configuration, and mount failures should become System Error.

---

## 9. Future run_program_nsjail(...) Flow

Pseudo-code for the future implementation:

```text
RunInfo run_program_nsjail(...) {
    args = build_nsjail_args(...)

    start timer
    pid = fork()

    child:
        prepare stdin/stdout/stderr redirection if needed
        execvp(nsjail, args)
        write Failed to execute nsjail to stderr file if exec fails
        _exit(1)

    parent:
        wait for nsjail process
        collect exit status
        read/parse nsjail logs if needed
        inspect output size if needed
        classify result as OK / RE / TLE / MLE / OLE / SE
        return RunInfo(result, time_ms, memory_mb)
}
```

The initial implementation should be conservative. If nsjail itself cannot be
started or configured, the judge should return System Error rather than Runtime
Error.

---

## 10. build_nsjail_args(...) Design

A first implementation step should be a pure argument builder:

```cpp
std::vector<std::string> build_nsjail_args(...);
```

Phase 1 of code work should generate nsjail arguments without executing nsjail.
That makes it possible to test command generation with normal unit/dry-run
tests before any real sandbox execution is attempted.

The argument builder should describe:

- executable path inside the jail
- working directory
- read-only mounts
- writable mounts
- stdin/stdout/stderr handling
- time limit
- memory limit strategy
- process limit
- network enabled/disabled mode

This keeps the future `run_program_nsjail(...)` smaller and easier to review.

---

## 11. Test Plan / 测试计划

Future nsjail tests should include:

- normal AC submission
- stderr output capture
- access `/etc/passwd`, expected to fail or be isolated
- write to an illegal path, expected to fail
- network access, expected to fail by default
- fork bomb, expected to be limited
- TLE
- MLE
- OLE

These tests should initially live outside the default CI path until the nsjail
runtime dependency is available and stable in CI.

---

## 12. Roadmap

- Phase 1: `docs/nsjail-plan.md` initial design. Done.
- Phase 2: `build_nsjail_args(...)` MVP argument builder. Done.
- Phase 3: real `run_program_nsjail(...)` MVP execution. Done.
- Phase 4: per-run filesystem isolation, network isolation tests, and basic
  rlimit coverage. Done.
- Phase 5: cgroup v2 environment precheck. Done.
- Phase 6: cgroup v2 dry-run argument design and tests. Done.
- Phase 7: real cgroup v2 memory and pids integration where the deployment
  environment supports it.
- Phase 8: seccomp whitelist, managed minimal rootfs, and production deployment
  hardening.
