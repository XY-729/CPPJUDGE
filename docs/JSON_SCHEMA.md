# CPPJUDGE judge_log.json schema v1 draft

This document describes the current `build/judge_log.json` top-level shape. It is a v1 draft, not a frozen public API contract yet.

## Required top-level identity fields

```json
{
  "schema_version": 1,
  "tool": "cppjudge",
  "cppjudge_version": "0.1.0-dev",
  "git_commit": "10d2bdd"
}
```

Fields:

- `schema_version`: integer schema version. Current value is `1`.
- `tool`: always `"cppjudge"`.
- `cppjudge_version`: tool version injected by CMake, currently `0.1.0-dev`.
- `git_commit`: short git commit injected by CMake, or `"unknown"` if unavailable.

## Run identity fields

```json
{
  "cli_mode": "judge",
  "run_id": "20260621_153000_12345",
  "run_dir": "build/runs/<run_id>",
  "problem_dir": "problems/A+B",
  "submission": "submissions/solution.cpp",
  "submission_file": "submissions/solution.cpp"
}
```

Fields:

- `cli_mode`: `"judge"` for the user-facing subcommand, `"legacy"` for positional CLI.
- `run_id`: unique timestamp/PID run identifier.
- `run_dir`: per-run directory containing compile logs and user output.
- `problem_dir`: problem directory used for the run.
- `submission` / `submission_file`: source file path. `submission` is the legacy field; `submission_file` is the clearer v1 alias.

## Problem configuration fields

```json
{
  "time_limit_ms": 1000,
  "memory_limit_mb": 128,
  "output_limit_mb": 1,
  "compile_time_limit_ms": 5000,
  "compare_mode": "exact",
  "sandbox_type": "builtin",
  "float_abs_eps": 1e-6,
  "float_rel_eps": 1e-6
}
```

These are the effective values after reading `problem.json` and applying optional CLI overrides.

## Final verdict fields

```json
{
  "final_verdict": "Accepted",
  "passed": 5,
  "total": 5
}
```

Current final verdict strings include:

- `Accepted`
- `Wrong Answer`
- `Time Limit Exceeded`
- `Memory Limit Exceeded`
- `Output Limit Exceeded`
- `Runtime Error`
- `Compile Error`
- `System Error`

## Compile and output paths

```json
{
  "executable_file": "build/runs/<run_id>/solution",
  "compile_error_file": "build/runs/<run_id>/compile_error.txt",
  "user_output_dir": "build/runs/<run_id>/user_output"
}
```

`compile_error` may appear when compilation fails.

## Case result entries

`results` is an array. Each entry describes one input case:

```json
{
  "case": "1",
  "input_file": "problems/A+B/input/1.in",
  "standard_output_file": "problems/A+B/output/1.out",
  "user_output_file": "build/runs/<run_id>/user_output/1.out",
  "user_error_file": "build/runs/<run_id>/user_output/1.out.err",
  "run_result": "OK",
  "verdict": "AC",
  "time_ms": 3,
  "memory_mb": 2
}
```

Optional per-case fields:

- `message`: diagnostic message for non-AC cases.
- `exit_code`: user process exit code when available.
- `signal`: terminating signal when available.
- `system_error`: boolean marker for judge/system-side failures.

## Exit code relationship

CLI exit codes are intentionally separate from `final_verdict`:

| Exit code | Meaning |
|-----------|---------|
| 0 | command succeeded and judge verdict is Accepted, or doctor READY |
| 1 | judge completed and verdict is not Accepted |
| 2 | CLI argument error, or doctor NOT_READY / NOT_VERIFIED |
| 3 | System Error or tool-level error |

Consumers should read `judge_log.json` for detailed verdicts and diagnostics.


## Compatibility policy

`schema_version` is currently `1`.

For schema v1:

- Evolution should be additive whenever possible.
- Existing fields should not be deleted or renamed casually.
- New fields should be optional for older consumers unless explicitly documented otherwise.
- External tools should check `schema_version` before assuming field semantics.
- Consumers should treat unknown fields as allowed.
- Verdict strings and exit codes are related but not identical:
  - `final_verdict` is the detailed judge result in JSON.
  - CLI exit code `0` means Accepted or successful non-judge command.
  - CLI exit code `1` means judge completed but verdict is not Accepted.
  - CLI exit code `2` means CLI argument error or doctor NOT_READY / NOT_VERIFIED.
  - CLI exit code `3` means System Error or tool-level error.
- `System Error` means the judge, problem data, sandbox backend, or host environment failed. It is not a user Runtime Error.

A future schema v2 may tighten or restructure fields, but v1 consumers should be able to rely on additive evolution during the v1 line.
