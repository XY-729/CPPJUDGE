# CPPJUDGE Problem Format

A problem is a directory containing `problem.json` and paired input/output cases:

```text
problems/<problem-name>/
  problem.json
  input/
    1.in
  output/
    1.out
```

Every `input/<case>.in` requires a matching `output/<case>.out`. Missing output is problem-data corruption and produces `System Error`.

`problem.json` is the source of resource limits and judge behavior:

```json
{
  "title": "A+B",
  "time_limit_ms": 1000,
  "memory_limit_mb": 128,
  "output_limit_mb": 1,
  "compile_time_limit_ms": 5000,
  "compare_mode": "exact",
  "sandbox_type": "nsjail"
}
```

Use positive integers for all limit fields. `compare_mode` accepts `exact` or `floating`. `sandbox_type=nsjail` is the product security path when cgroup v2 delegation and seccomp are configured; `builtin` is for learning and trusted experiments only.

Run a submission with:

```bash
./build/cppjudge judge --problem problems/A+B --submission submissions/solution.cpp
```


## Quick Start sample

The repository default `submissions/solution.cpp` is an Accepted A+B sample for `problems/A+B`:

```bash
./build/cppjudge judge --problem problems/A+B --submission submissions/solution.cpp
```

This command should return exit code 0 and write an Accepted `build/judge_log.json`.
