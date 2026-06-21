# CPPJUDGE Troubleshooting

## Build failed

Run the build commands from the repository root:

```bash
cmake -S . -B build
cmake --build build -j2
```

If CMake cannot find a compiler, install a C++17-capable compiler and CMake. On Rocky/Fedora systems this usually means `gcc-c++`, `make`, and `cmake`.

## `cppjudge: command not found`

Use the built binary path:

```bash
./build/cppjudge --help
```

If `build/cppjudge` does not exist, rebuild first.

## `nsjail not found`

`nsjail` is only required for the product sandbox path. The builtin runner and portable tests can still run without nsjail, but product-grade untrusted-code execution requires nsjail + cgroup v2 + seccomp.

Check:

```bash
./build/cppjudge doctor
```

## Doctor returns `NOT_VERIFIED`

`NOT_VERIFIED` means doctor ran successfully and found the main dependencies, but the current session cannot prove the full product sandbox path. A normal SSH shell often lacks delegated cgroup write permission, so doctor may be unable to create a child cgroup.

This is not the same as a failed install. Use a delegated systemd scope/service to verify the product nsjail path.

## Doctor returns `NOT_READY`

`NOT_READY` means a key dependency is missing or unreadable, such as:

- nsjail is not in `PATH`
- cgroup v2 is not detected
- memory/pids controllers are not visible
- seccomp policy is missing or unreadable

Fix the missing dependency and rerun doctor.

## Why nsjail tests are skipped in SSH

The nsjail product path needs delegated cgroup permissions. Plain SSH sessions often cannot write the required cgroup subtree, so nsjail/security tests may skip or report NOT_VERIFIED. This protects the test signal from pretending product security was verified when it was not.

## Common `System Error` causes

`System Error` is a judge-side or environment-side failure, not a user program runtime error. Common causes:

- invalid `problem.json`
- missing `input/` or `output/` directory
- missing matching `.out` file
- selected sandbox backend unavailable
- nsjail/cgroup/seccomp setup failure
- internal compile/run setup failure

## Missing `.out` file

Every `input/<case>.in` must have a matching `output/<case>.out`. Missing expected output is invalid problem data and results in `System Error`.

## Judge returns exit code 1

Exit code 1 means judging completed, but the final verdict was not Accepted. Check:

```bash
python3 -m json.tool build/judge_log.json
```

Look at `final_verdict` and `results`.

## Judge returns exit code 2

Exit code 2 means a CLI argument error, or doctor completed but reported `NOT_READY` / `NOT_VERIFIED`.

Examples:

```bash
./build/cppjudge judge --submission submissions/solution.cpp
./build/cppjudge doctor
```

## Judge returns exit code 3

Exit code 3 means `System Error` or a tool-level error. Inspect `build/judge_log.json` for the `error` field.

## Where is `build/judge_log.json`?

The latest judge log is:

```bash
build/judge_log.json
```

Pretty-print it with:

```bash
python3 -m json.tool build/judge_log.json
```

## Run-specific logs

Every run also writes a dedicated log under:

```text
build/runs/<run_id>/judge_log.json
```

The latest `build/judge_log.json` contains `run_id` and `run_dir` so you can find the per-run files.
