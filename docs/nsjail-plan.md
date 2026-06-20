# CPPJUDGE nsjail / cgroup / seccomp Plan

Updated: 2026-06-20

## Current status

CPPJUDGE now has an nsjail backend with cgroup v2 and seccomp integration code, but the full product-grade path is still environment-gated.

Implemented in code:

- `sandbox_type = "nsjail"` compile and run paths.
- Per-run `sandbox_root`.
- Read-only solution bind mount.
- Writable `user_output` bind mount.
- Selected dynamic library bind mounts.
- `--disable_proc`.
- Network namespace behavior through nsjail defaults.
- cgroup v2 service/run lifecycle.
- `memory.max`, `memory.swap.max`, `pids.max`.
- `memory.events` / `oom_kill` based MLE classification.
- `memory.peak` collection.
- `cgroup.kill` cleanup with fallback.
- seccomp policy path validation and `--seccomp_policy` argument injection.
- Production preflight that fails closed when nsjail, delegated cgroup, or seccomp policy is unavailable.

Current 2026-06-20 VM verification:

| Check | Status |
|-------|--------|
| nsjail executable | PASS |
| nsjail cgroup flags | PASS |
| cgroup v2 detected | PASS |
| current SSH session can create child cgroup | FAIL |
| portable profile | PASS |
| delegated nsjail tests | NOT_VERIFIED / skipped |
| seccomp security tests | NOT_VERIFIED / skipped |

## Product-grade interpretation

The nsjail path is not yet product-grade merely because the code exists. It becomes product-grade only when the target deployment can repeatedly run the nsjail/security/seccomp tests without skipped cases.

The most important missing proof is delegated cgroup execution:

```text
Delegate=memory pids
```

Without this, `memory.max`, `pids.max`, `memory.events`, and `cgroup.kill` cannot be proven in the actual sandbox path.

## Current architecture

### Compile phase

The nsjail compile path:

- copies the submission into the run directory as `submission.cpp`;
- creates a compile sandbox root;
- mounts the run directory at `/work`;
- mounts compiler/toolchain paths such as `/usr`, `/lib64`, `/lib`, `/bin`;
- runs `/usr/bin/g++` inside nsjail.

This is better than running the compiler directly on user-controlled code, but it is not the final product-grade compile sandbox because it still depends on broad host read-only mounts.

### Runtime phase

The nsjail runtime path:

- creates a per-run sandbox root;
- bind-mounts `solution` read-only as `/sandbox/solution`;
- bind-mounts `user_output` writable as `/sandbox/user_output`;
- mounts selected dynamic-linker files;
- disables procfs;
- redirects stdin/stdout/stderr through pre-opened file descriptors;
- waits on a parent barrier until the nsjail process joins the run cgroup;
- classifies TLE/OLE/MLE via parent timing/output checks and cgroup events instead of user stderr.

### cgroup v2 lifecycle

The intended lifecycle is:

```text
discover cgroup v2 mount and current cgroup
create manager_<pid>
move judge process to manager cgroup
enable memory+pids in subtree_control
create run_<pid>_<counter>
write memory.max / memory.swap.max / pids.max
fork nsjail process
join nsjail process to run cgroup
release child barrier
monitor wall clock and output file size
read memory.events and memory.peak
classify verdict
kill and remove run cgroup
```

## Known gaps

### 1. Delegated environment

Current ordinary SSH sessions do not have cgroup write permission. The project needs a repeatable delegated runner entry.

Recommended next artifact:

```bash
scripts/run_delegated_nsjail_tests.sh
```

It should launch tests under systemd with `Delegate=memory pids`, then run:

```bash
bash scripts/run_all_tests.sh nsjail
bash scripts/run_all_tests.sh security
```

### 2. Fixed rootfs

Current runtime root is constructed per run and relies on host dynamic library paths. Current compile sandbox mounts broad host toolchain paths.

Product target:

- versioned runtime rootfs;
- versioned compile rootfs;
- rootfs manifest;
- `rootfs_version` in `judge_log.json`;
- integrity check in `cppjudge doctor`.

### 3. Seccomp policy

Current policy is a deny-list with `DEFAULT ALLOW`.

Product target:

- observed syscall baseline for normal C++ submissions;
- versioned allow-list policy;
- compatibility tests for C++ I/O, exceptions, signals, TLE, MLE, OLE, CE;
- clear policy version in logs.

### 4. Low-privilege identity

Current plan still needs a stable UID/GID model.

Product target:

- dedicated judge service user;
- explicit nsjail UID/GID mapping;
- no accidental root execution for untrusted code;
- file ownership and permissions documented.

### 5. Operational diagnostics

Current scripts are useful but scattered.

Product target:

```bash
cppjudge doctor
cppjudge doctor --verbose
```

Doctor should check:

- nsjail presence and version;
- cgroup v2 mount and controllers;
- delegated write permission;
- seccomp policy file and version;
- rootfs manifest;
- writable build/run directories;
- low-privilege user model.

## Next development order

1. Commit current documentation refresh.
2. Confirm merge strategy for `stage3d-rootfs`.
3. Create a delegated nsjail test entry.
4. Run nsjail/security/seccomp tests without skipped cases.
5. Add fixed runtime rootfs.
6. Tighten seccomp from deny-list toward allow-list.
7. Define low-privilege identity and install docs.
8. Add doctor and stable schemas.
