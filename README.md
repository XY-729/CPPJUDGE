# CPPJUDGE

CPPJUDGE is a lightweight C++ judge for local learning, course projects, and
trusted development environments. It compiles one C++ submission, runs it
against problem test data, applies basic resource limits, compares outputs, and
writes structured JSON logs for debugging.

This project is currently a teaching-oriented / local-only judge. Its practical
goal is to evolve toward a C++ judge backend suitable for trusted school or lab
use. The built-in runner is useful for development and experiments, but it is
not a product-grade security sandbox.

---

## Project Overview / 项目简介

CPPJUDGE is a small C++ online judge prototype focused on the C++ language. It
supports:

- compiling C++17 submissions
- running submissions against multiple input/output test cases
- time, memory, output, and compile-time limits
- exact and floating-point output comparison
- per-run artifact directories
- detailed `judge_log.json` output

The current goal is to keep the core judge simple while preparing the runner
architecture for future sandbox backends such as nsjail and isolate.

---

## Features / 功能特性

- Compile Error
- Accepted / Wrong Answer
- Time Limit Exceeded
- Memory Limit Exceeded
- Output Limit Exceeded
- Runtime Error
- System Error
- `exact` / `floating` compare mode
- `sandbox_type`: `builtin` / `nsjail` / `isolate`
- JSON judge log
- isolated run directories under `build/runs/<run_id>/`
- latest-log shortcut at `build/judge_log.json`

---

## Build / 构建

```bash
mkdir -p build
cd build
cmake ..
make
```

On Rocky Linux / Fedora, install the basic build tools first if needed:

```bash
sudo dnf install gcc-c++ make cmake
```

---

## Usage / 使用方法

```bash
./build/cppjudge [submission_file] [problem_dir] [time_limit_ms] [memory_limit_mb] [output_limit_mb] [compare_mode] [compile_time_limit_ms]
```

Arguments:

- `submission_file`: C++ source file. Default: `submissions/solution.cpp`
- `problem_dir`: problem directory. Default: `problems/A+B`
- `time_limit_ms`: runtime limit in milliseconds, positive integer
- `memory_limit_mb`: memory limit in MB, positive integer
- `output_limit_mb`: output limit in MB, positive integer
- `compare_mode`: `exact`, `floating`, or `float`
- `compile_time_limit_ms`: compile timeout in milliseconds, positive integer

Example:

```bash
./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 floating 5000
python3 -m json.tool build/judge_log.json
```

Command-line arguments override matching values loaded from `problem.json`.

---

## Problem Format / 题目目录格式

Each problem directory contains a `problem.json` file and matching input/output
files:

```text
problems/A+B/
  problem.json
  input/
    1.in
    2.in
  output/
    1.out
    2.out
```

Input files use the `.in` extension. For each `input/<case>.in`, CPPJUDGE
expects a corresponding `output/<case>.out`. Missing standard output files are
treated as `System Error` because that indicates incomplete problem data.

---

## problem.json Example

```json
{
    "title": "A+B",
    "time_limit_ms": 1000,
    "memory_limit_mb": 128,
    "output_limit_mb": 1,
    "compile_time_limit_ms": 5000,
    "compare_mode": "floating",
    "float_abs_eps": 1e-6,
    "float_rel_eps": 1e-6,
    "sandbox_type": "builtin"
}
```

Supported config values:

- `time_limit_ms`, `memory_limit_mb`, `output_limit_mb`,
  `compile_time_limit_ms`: positive integers
- `compare_mode`: `exact`, `floating`, or `float`
- `float_abs_eps`, `float_rel_eps`: non-negative numbers
- `sandbox_type`: `builtin`, `nsjail`, or `isolate`

Invalid config types or values are reported as `System Error` and written to
the `error` field in `judge_log.json`.

---

## Verdicts / 评测结果

- `Accepted`: the submission passed all test cases.
- `Wrong Answer`: the submission finished normally but output differed from the standard answer.
- `Time Limit Exceeded`: the submission exceeded the runtime limit.
- `Memory Limit Exceeded`: the submission exceeded the memory limit.
- `Output Limit Exceeded`: the submission exceeded the output size limit.
- `Runtime Error`: the user program crashed or exited abnormally.
- `Compile Error`: the submission failed to compile.
- `System Error`: the judge configuration, test data, sandbox backend, or local execution environment failed.

`Runtime Error` is reserved for user-program failures. `System Error` is used
for judge-side problems such as invalid `problem.json`, missing input/output
directories, missing answer files, or unimplemented sandbox backends.

---

## Sandbox / 沙箱说明

CPPJUDGE currently exposes three sandbox backend names:

- `builtin`: implemented, used by default
- `nsjail`: MVP runner implemented; requires the external `nsjail` binary
- `isolate`: reserved backend, not implemented yet

`builtin` is the default and remains the path used by CI. The `nsjail` backend
can execute submissions through `fork` + `execvp("nsjail", ...)`. The current
MVP profile now uses a per-run `sandbox_root` instead of exposing the whole host
root as the jail root. It bind-mounts the compiled solution read-only, the
per-run `user_output` directory read/write, and a small set of dynamic-linker
files needed by ordinary C++ binaries read-only. It also disables procfs inside
the jail.

The current nsjail dynamic-library mounts are minimal and C++ oriented. They are
enough for the current regression programs, but they are not a general dependency
solution for every possible C++ binary or future multi-language support.
Dependency detection or a managed minimal rootfs should be improved later.

The nsjail backend also applies basic rlimits for address space, output file
size, core dumps, CPU time, open files, and process count. Its memory behavior is
currently based on `RLIMIT_AS` with a small loader headroom. Stronger and more
precise MLE enforcement still needs cgroup v2.

This is still not a final product-grade sandbox profile: it does not yet provide
a complete minimal rootfs, seccomp policy, cgroup memory enforcement, or explicit
low-privilege user mapping. `isolate` is still a placeholder and returns
`System Error`.

The `builtin` runner provides a basic local execution wrapper:

- child-process execution
- stdin/stdout/stderr redirection
- process-group cleanup with `SIGKILL`
- `RLIMIT_AS` virtual address-space limit
- `RLIMIT_FSIZE` output file-size limit
- `RLIMIT_CORE = 0` to disable core dumps
- `RLIMIT_NOFILE = 64` open-file limit
- `RLIMIT_NPROC = 16` process-count guard where supported
- `RLIMIT_CPU` fallback for CPU timeouts
- `/proc/<pid>/status` memory monitoring

These checks are useful for local testing but are not equivalent to a full
Linux sandbox. The builtin runner currently has the more detailed MLE behavior
through `RLIMIT_AS`, `/proc` VmSize monitoring, and `bad_alloc` detection. The
nsjail path now has execution, filesystem, network-isolation, and basic
`RLIMIT_AS` memory coverage, but precise nsjail MLE still needs cgroup v2.

---

## Security Notice / 安全性说明

CPPJUDGE is currently a teaching-oriented / local-development judge. The
`builtin` runner is not a product-grade security sandbox.

The current builtin runner does not provide:

- Linux namespace isolation
- seccomp syscall filtering
- cgroup v2 resource isolation
- chroot / pivot_root filesystem isolation
- network namespace isolation
- dedicated low-privilege user isolation
- full container isolation

Do not expose the current version directly to the public internet. Do not use
it to run arbitrary untrusted submissions from unknown users in production.

Future sandbox work is expected to integrate mature backends such as nsjail or
isolate, plus cgroup/seccomp/network/filesystem isolation.

---

## Run Tests / 运行测试

Default regression tests:

```bash
bash scripts/run_tests.sh
```

This script builds the project and checks the main verdict paths:

- AC / WA / TLE / MLE / OLE / RE / CE
- selected `System Error` cases
- latest `build/judge_log.json`
- per-run `run_id` and `run_dir`
- required debug fields in case results

Manual builtin runner security tests:

```bash
bash scripts/run_security_tests.sh
```

Manual nsjail MVP test:

```bash
bash scripts/run_nsjail_tests.sh
```

If `nsjail` is not installed, the nsjail script prints a skip message and exits
successfully. When `nsjail` is available, it temporarily copies `problems/A+B`,
sets `sandbox_type` to `nsjail`, and checks the current MVP paths for Accepted,
Runtime Error, Time Limit Exceeded, Memory Limit Exceeded, Output Limit Exceeded,
stderr capture, basic filesystem isolation, and network namespace isolation
(`clone_newnet:true`).

The security test script exercises open-file limits, process-count behavior,
core-dump blocking, and stderr capture. It includes fork-related behavior and
is intentionally not part of the default GitHub Actions workflow. Run it only
in a controlled local environment. The nsjail test is also manual-only and is
not run by GitHub Actions.

---

## nsjail / cgroup Environment Check

Before wiring cgroup v2 options into the nsjail runner, inspect the target
machine:

```bash
bash scripts/check_nsjail_env.sh
```

The script reports:

- whether `nsjail` is installed
- whether this `nsjail` build exposes cgroup-related flags
- whether the host appears to use cgroup v2
- the current process cgroup path
- whether the current user can create a child cgroup under `/sys/fs/cgroup`

This script is diagnostic only. It does not modify judge behavior, it is not a
required CI check, and cgroup failures do not make normal judging fail. If cgroup
v2 is unavailable, the nsjail runner keeps using the current rlimit fallback.
Future cgroup memory and pids enforcement should only be enabled after the
machine passes the relevant precheck items.

To preview the nsjail cgroup arguments that a future runner integration would
use, run:

```bash
bash scripts/preview_nsjail_cgroup_args.sh 128 16 0
```

This dry-run script only prints arguments such as `--use_cgroupv2`,
`--cgroup_mem_max`, and `--cgroup_pids_max`. It does not execute nsjail and does
not change the current runner behavior.

---

## Continuous Integration / 持续集成

GitHub Actions runs the default regression test script on every `push` and
`pull_request`:

```bash
bash scripts/run_tests.sh
```

The manual security and nsjail MVP test scripts are not run by CI.

---

## Roadmap / 路线图

- isolated run directories
- configurable sandbox backend
- nsjail integration
- isolate integration
- cgroup v2
- seccomp whitelist
- network isolation
- filesystem isolation
- web API / worker queue

---

## Author

XY-729

## nsjail Design Note

The nsjail backend now has an MVP runner. It is still not a final product-grade
sandbox profile. See docs/nsjail-plan.md for the hardening plan.
