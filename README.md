# cppjudge

`cppjudge` is a small C++ online judge prototype. It compiles one C++17 submission, runs it against problem test data, compares outputs, records resource usage, and writes a JSON judge log.

---

## Features

- C++17 compile and run flow
- `exact` and `floating` output comparison modes
- Per-case time and memory reporting
- JSON log at `build/judge_log.json`
- User stdout files under `build/user_output/`
- User stderr files next to stdout as `.out.err`
- Compile time limit support
- CMake-based Linux development workflow

---

## Project Layout

| Path | Description |
| --- | --- |
| `src/` | Source files for judge, runner, compiler, and comparer |
| `include/` | Third-party headers, such as `json.hpp` |
| `build/` | Build output, judge logs, and user outputs |
| `problems/` | Problem data, including `input/`, `output/`, and `problem.json` |
| `submissions/` | User submissions and verdict test programs |
| `README.md` | Project documentation |
| `CMakeLists.txt` | CMake build configuration |
| `.gitignore` | Ignored build and editor files |

---

## Build

On Rocky Linux / Fedora:

```bash
sudo dnf install gcc-c++ make cmake
mkdir -p build
cd build
cmake ..
make
```

---

## Usage

```bash
./build/cppjudge [submission_file] [problem_dir] [time_limit_ms] [memory_limit_mb] [output_limit_mb] [compare_mode] [compile_time_limit_ms]
```

Arguments:

- `submission_file`: C++ source file. Default: `submissions/solution.cpp`
- `problem_dir`: problem directory. Default: `problems/A+B`
- `time_limit_ms`: run time limit in milliseconds. Must be a positive integer.
- `memory_limit_mb`: memory limit in MB. Must be a positive integer.
- `output_limit_mb`: output limit in MB. Must be a positive integer.
- `compare_mode`: output comparison mode. Supported values: `exact`, `floating`, `float`.
- `compile_time_limit_ms`: compile time limit in milliseconds. Must be a positive integer.

Example:

```bash
./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 floating 5000
```

---

## problem.json

Each problem may provide a `problem.json` file:

```json
{
    "title": "A+B",
    "time_limit_ms": 1000,
    "memory_limit_mb": 128,
    "output_limit_mb": 1,
    "compile_time_limit_ms": 5000,
    "compare_mode": "floating",
    "float_abs_eps": 1e-6,
    "float_rel_eps": 1e-6
}
```

Command-line arguments override values loaded from `problem.json`.

---

## Output Files

- Judge log: `build/judge_log.json`
- User stdout: `build/user_output/*.out`
- User stderr: `build/user_output/*.out.err`
- Compile errors: `build/compile_error.txt`

Each case in the JSON log includes the stdout and stderr paths:

```json
{
    "case": "1",
    "input_file": "problems/A+B/input/1.in",
    "standard_output_file": "problems/A+B/output/1.out",
    "user_output_file": "build/user_output/1.out",
    "user_error_file": "build/user_output/1.out.err",
    "time_ms": 15,
    "memory_mb": 12,
    "verdict": "AC"
}
```

---

## Verdict Test Submissions

Keep these files for regression checks:

```text
submissions/tests/ac.cpp
submissions/tests/wa.cpp
submissions/tests/ce.cpp
submissions/tests/tle.cpp
submissions/tests/mle.cpp
submissions/tests/ole.cpp
submissions/tests/re.cpp
```

Example:

```bash
cp submissions/tests/mle.cpp submissions/solution.cpp
./build/cppjudge submissions/solution.cpp problems/A+B 1000 16
python3 -m json.tool build/judge_log.json
```

---

## Future Work

- Multiple language support
- Web submission UI
- Special Judge support
- Stronger sandboxing

## Author

XY-729
