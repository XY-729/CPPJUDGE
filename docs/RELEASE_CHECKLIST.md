# CPPJUDGE Release Checklist

Run these commands from the repository root before publishing or handing off a release candidate.

## Required portable checks

```bash
cmake -S . -B build
cmake --build build -j2
bash scripts/run_all_tests.sh portable
./build/cppjudge --version
./build/cppjudge doctor || true
./build/cppjudge judge --problem problems/A+B --submission submissions/solution.cpp
python3 -m json.tool build/judge_log.json
```

Expected baseline:

- build succeeds
- portable profile passes
- `--version` prints `cppjudge <version> (git: <commit-or-unknown>)`
- doctor returns `READY` or, in a plain SSH session, may return `NOT_VERIFIED` with exit code 2
- Quick Start judge command returns `Accepted` and exit code 0
- `build/judge_log.json` is valid JSON and includes schema v1 fields

## Delegated product-sandbox checks

Plain SSH `NOT_VERIFIED` is not a release failure by itself. Product-grade nsjail verification requires delegated cgroup permissions, normally through a systemd scope/service.

When the VM supports delegated systemd-run, also run:

```bash
systemd-run --user --scope -p Delegate=yes bash scripts/run_all_tests.sh nsjail
systemd-run --user --scope -p Delegate=yes bash scripts/run_all_tests.sh security
```

Expected:

- delegated nsjail profile passes
- delegated security profile passes
- skipped tests are not counted as product security verification

## Do not claim product security from builtin

`builtin` is for learning, experiments, local debugging, and portable regression tests. Product security claims require nsjail + cgroup v2 + seccomp in a delegated environment.

## Log/schema sanity

After the Quick Start run:

```bash
python3 -m json.tool build/judge_log.json
```

Confirm the top-level fields include:

- `schema_version`
- `tool`
- `cppjudge_version`
- `git_commit`
- `cli_mode`
- `problem_dir`
- `submission_file`
- `final_verdict`
- `results`
