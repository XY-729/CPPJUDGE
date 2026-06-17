# CPPJUDGE Overview

## 1. 项目定位

CPPJUDGE 是一个面向 Linux 的轻量级 C++ 判题工具，当前目标是在学校或实验室可信环境中作为判题后端使用。核心流程为：

- 读取题目配置（`problem.json`）
- 编译用户提交（C++17，通过 g++ 或 nsjail 封装）
- 在受限环境中运行编译后的程序
- 收集退出状态、信号和资源使用
- 比较程序输出与标准答案
- 生成每个测试点的结果、最终 verdict 和结构化 JSON 日志

当前阶段仍然以教学导向、本地开发优先。内置 runner 适合本地测试和可信环境，但不是产品级安全沙箱。nsjail 后端已有 MVP 实现，但产品级加固仍在进行中。

---

## 2. 判题流程

```text
main.cpp
  └─ judge(argc, argv)
       ├─ load_problem_config()          → ProblemConfig (默认值 + problem.json + CLI 覆盖)
       ├─ is_production_environment()    → 检查 CPPJUDGE_PRODUCTION / CPPJUDGE_ENV
       ├─ validate_sandbox_for_environment() → 生产模式拒绝 builtin/isolate
       ├─ 扫描 input_dir 中的 *.in 文件 (排序)
       ├─ make_run_id()                  → YYYYMMDD_HHMMSS_<pid>
       ├─ compile_cpp_structured()       → CompileInfo {OK, CE, SE}
       │    ├─ CE → 写入 CE 日志，结束
       │    └─ SE → 写入 SE 日志，结束
       └─ 对每个测试点:
            ├─ run_program()             → RunInfo {OK,TLE,MLE,OLE,RE,SE}
            │    ├─ SE → 该测试点 SE，更新最终 verdict
            │    ├─ TLE/MLE/OLE/RE → 更新最终 verdict
            │    └─ OK → compare_output() → AC 或 WA
            └─ 记录 case_json
       └─ write_log_file()               → build/runs/<run_id>/judge_log.json
                                          → build/judge_log.json (快捷路径)
```

所有测试点都会被运行（不会因为单个测试点失败而提前终止），但最终 verdict 在第一个非 AC 结果时确定。

---

## 3. 核心模块

### compiler（`src/compiler.h` / `src/compiler.cpp`）

负责编译用户源码，支持 builtin 和 nsjail 两种编译路径：

- **builtin**: 直接 `fork` + `execvp("g++", ...)`，通过 `setpgid` 隔离进程组，使用 `RLIMIT_AS`、`RLIMIT_FSIZE`、`RLIMIT_CPU`、`RLIMIT_NOFILE`、`RLIMIT_NPROC` 施加编译资源限制。
- **nsjail**: 先将用户源码复制到运行目录，构建 nsjail 编译参数（chroot 到 `compile_sandbox_root`，只读挂载 `/usr`、`/lib64`、`/lib`、`/bin` 等），在沙箱内执行 `/usr/bin/g++`。

两种路径均使用 `pipe2(O_CLOEXEC)` 检测 `exec` 失败：子进程 fork 后关闭读端、保留写端（`O_CLOEXEC`）。若 `exec` 成功，写端自动关闭；若 `exec` 失败，子进程向写端写入一个字节。父进程等待子进程结束后检查管道——读到数据说明 `exec` 失败，应返回 SE。

返回 `CompileInfo` 结构体，包含 `CompileResult`（OK/CE/SE）、`system_error` 标志和 `error_message`。

### runner（`src/runner.h` / `src/runner.cpp`）

负责进程启动、沙箱执行、资源限制和退出状态收集。支持三种沙箱后端：

- **builtin**: 通过 `fork` + `execl` 执行用户程序。父进程使用 `wait4(WNOHANG)` 轮询，每 5ms 检查 `/proc/<pid>/status` 的 `VmSize` 监控内存、检查墙钟超时、检查输出文件大小。使用 `RLIMIT_AS`、`RLIMIT_FSIZE`、`RLIMIT_CORE`、`RLIMIT_NOFILE`、`RLIMIT_NPROC`、`RLIMIT_CPU` 做资源限制。子进程通过 `setpgid(0,0)` 创建独立进程组，超限时使用 `kill(-pid, SIGKILL)` 清理整个进程组。同样使用 `pipe2(O_CLOEXEC)` 检测 `exec` 失败。
- **nsjail**: 构建 per-run sandbox_root、bind-mount 只读挂载 solution 和动态库、读写挂载 user_output 目录。通过 `nsjail -Mo --chroot` 执行，禁用 procfs（`--disable_proc`），施加 rlimit。当前仍通过解析 nsjail stderr 辅助判断 TLE/MLE/OLE（已知限制）。
- **isolate**: 占位实现，直接返回 SE。

返回 `RunInfo` 结构体，包含 `RunResult`（OK/TLE/MLE/OLE/RE/SE）、`time_ms`、`memory_mb`、`system_error`、`error_message`、`exit_code`、`signal`。

### comparer（`src/comparer.h` / `src/comparer.cpp`）

负责输出比较。支持两种模式：

- **exact**: 逐行比较，末尾空白字符（换行、回车、空格、制表符）在比较前规范化。
- **floating**: token-by-token 比较。若两边 token 均可解析为有限浮点数，使用 `|a-b| <= max(abs_eps, rel_eps * max(1, |a|, |b|))` 判断；否则做精确字符串比较。

### judge（`src/judge.h` / `src/judge.cpp`）

负责编排编译、运行、比较、测试点汇总和最终 verdict。定义了 `FinalVerdict` 枚举（AC/WA/TLE/MLE/OLE/RE/SE/CE）和 `ProblemConfig` 结构体。关键函数：

- `load_problem_config()`: 从 `problem.json` 加载配置，验证类型和范围，CLI 参数可覆盖。
- `validate_sandbox_for_environment()`: 生产模式下检查沙箱可用性。
- `make_run_id()`: 格式 `YYYYMMDD_HHMMSS_<pid>`，保证单机唯一。
- `write_log_file()`: 同时写入 `build/runs/<run_id>/judge_log.json` 和 `build/judge_log.json`。

### main / CLI（`src/main.cpp`）

最小入口：调用 `judge(argc, argv)` 后返回 0。CLI 解析在 `judge.cpp` 中通过位置参数完成。

```bash
./build/cppjudge [submission] [problem_dir] [time_limit_ms] [memory_limit_mb] [output_limit_mb] [compare_mode] [compile_time_limit_ms]
```

---

## 4. 结构化错误模型

### CompileInfo

```cpp
enum class CompileResult { OK, CE, SE };

struct CompileInfo {
    CompileResult result = CompileResult::OK;
    bool system_error = false;
    std::string error_message;
};
```

- **OK**: 编译成功，生成可执行文件。
- **CE**: 用户源码编译失败（g++ 非零退出或编译超时）。
- **SE**: 编译基础设施错误（fork 失败、exec 失败、文件系统错误、nsjail 无法启动、沙箱类型未实现）。

### RunInfo

```cpp
enum class RunResult { OK, TLE, MLE, OLE, RE, SE };

struct RunInfo {
    RunResult result = RunResult::OK;
    int time_ms = 0;
    int memory_mb = 0;
    bool system_error = false;
    std::string error_message;
    int exit_code = -1;
    int signal = -1;
};
```

- **OK**: 正常退出，退出码 0。
- **TLE**: 超时（墙钟超限、SIGXCPU、或 SIGKILL + 超时判定）。
- **MLE**: 内存超限（`/proc` VmSize 超限、SIGKILL + 内存超限判定、stderr bad_alloc）。
- **OLE**: 输出超限（SIGXFSZ 或输出文件大小达到限制）。
- **RE**: 用户程序非零退出或异常信号终止。
- **SE**: 判题基础设施错误（文件打开失败、fork/exec/wait 失败、nsjail 启动失败）。

### 错误责任边界

| 场景 | Verdict |
|------|---------|
| 用户源码编译失败 | CE |
| 用户程序非零退出 | RE |
| 用户程序被崩溃信号终止 | RE |
| 执行超时 | TLE |
| 内存超限 | MLE |
| 输出超限 | OLE |
| 编译器进程无法启动 | SE |
| nsjail 无法启动 | SE |
| 沙箱目录准备失败 | SE |
| 文件系统或系统调用异常 | SE |
| 题目数据缺失（无输入文件、缺失标准答案） | SE |
| problem.json 格式或类型错误 | SE |

### 设计原则

- stderr 是用户可控文本，不能作为可信系统状态来源。
- judge.cpp 层不通过字符串匹配区分 RE/SE——该工作在 runner/compiler 层通过结构化结果完成。
- 错误由最接近错误来源的底层模块进行结构化分类。
- `pipe2(O_CLOEXEC)` 是 exec 失败检测的核心机制：子进程 exec 成功后写端自动关闭，父进程读到数据即表示 exec 失败。

**已知限制**: nsjail runner 当前仍通过解析 stderr 辅助判断 TLE/MLE/OLE（`stderr_says_tle`、`stderr_says_mle`、`stderr_says_ole`）。builtin runner 也通过检查 stderr 中的 `bad_alloc` 辅助判断 MLE。这些是后续 cgroup v2 集成前需要解决的过渡方案。

---

## 5. Verdict 模型

### 当前支持的 verdict

| Verdict | 含义 |
|---------|------|
| **Accepted (AC)** | 所有测试点通过 |
| **Wrong Answer (WA)** | 程序正常结束，但输出与标准答案不同 |
| **Compile Error (CE)** | 提交源码编译失败 |
| **Runtime Error (RE)** | 程序崩溃或非零退出 |
| **Time Limit Exceeded (TLE)** | 超过运行时间限制 |
| **Memory Limit Exceeded (MLE)** | 超过内存限制 |
| **Output Limit Exceeded (OLE)** | 超过输出大小限制 |
| **System Error (SE)** | 判题配置、题目数据、沙箱后端或本地执行环境出错 |

### Verdict 优先级

代码遍历全部测试点，最终 verdict 取遍历中出现的第一个非 AC 状态（顺序：SE > TLE/MLE/OLE/RE > WA）。所有测试点都会被运行，不会在中途停止。

每个测试点内部的优先级：SE > 其他 RunResult（TLE/MLE/OLE/RE）> WA。

---

## 6. Sandbox 模型

### builtin

- 已完整实现。
- 通过 `fork` + `setpgid` + `execl` 执行，使用 rlimit 做资源限制。
- 通过 `/proc/<pid>/status` 轮询监控内存。
- 超限时 `kill(-pid, SIGKILL)` 清理整个进程组。
- 关闭额外 fd、清空环境变量。
- **不适合运行公开不可信代码**：没有 namespace 隔离、seccomp、chroot、cgroup 或网络隔离。

### nsjail

- MVP 实现已完成。
- 编译和运行两个阶段均通过 nsjail 执行。
- Per-run sandbox_root，只读挂载 solution 和最小动态库集合，读写挂载 user_output。
- 禁用 procfs（`--disable_proc`）。
- 保留 nsjail 默认网络 namespace 隔离。
- 基础 rlimit 覆盖（AS、FSIZE、CORE、CPU、NOFILE、NPROC）。
- **当前限制**：无 cgroup v2 精确内存控制、无 seccomp、无固定 rootfs、无低权限 UID/GID 映射、部分分类仍依赖 stderr 解析。

### isolate

- 占位后端，返回 System Error。
- 代码中预留了接口，但无实现。

### 生产模式

设置 `CPPJUDGE_PRODUCTION=1` 或 `CPPJUDGE_ENV=production` 启用：
- 拒绝 `builtin`（不安全）。
- 拒绝 `isolate`（未实现）。
- 要求 nsjail 在 PATH 中可用。
- sandbox preflight check 失败时 fail closed。

---

## 7. 测试体系

### 层次

| 层次 | 测试内容 | 位置 |
|------|---------|------|
| **unit** | comparer、sandbox_types、verdict_strings、config | `tests/unit/` |
| **integration** | builtin verdicts、illegal config、nsjail verdicts | `tests/integration/` |
| **regression** | 默认回归、安全、结构化错误、stderr 独立性、MLE 分类 | `tests/regression/`、`scripts/run_tests.sh` 等 |
| **security** | MUST_BLOCK（nsjail 必须阻止的攻击）、KNOWN_GAP（已知缺口） | `tests/security/` |

### Profiles

| Profile | 内容 | 命令 |
|---------|------|------|
| **quick** | unit + integration（不含 regression/security/nsjail） | `run_all_tests.sh quick` |
| **portable** | 所有 portable 标签测试（不含 nsjail） | `run_all_tests.sh portable` |
| **full** | 所有已注册测试 | `run_all_tests.sh full` |
| **nsjail** | 仅 nsjail 标签测试 | `run_all_tests.sh nsjail` |
| **security** | 仅 security 标签测试（需 nsjail） | `run_all_tests.sh security` |

### 回归测试原则

每修复一个缺陷，保留能够复现该问题的永久测试。当前典型回归：

- 用户伪造 nsjail stderr 不得导致误判 SE（`test_regression_stderr_independence`）
- 编译器 exec 失败必须为 SE（`test_regression_structured_results`）
- 用户程序 SIGSEGV 必须保持 RE（`test_regression_mle_classification` 及其关联测试）
- MLE 不得误判为 RE（`test_regression_mle_classification`）

### CI

GitHub Actions 在 push/PR 时运行 portable profile（11 个测试，不含 nsjail）。安全测试和 nsjail 测试默认不在 CI 中运行。

---

## 8. 题目和日志接口

### problem.json

当前支持的字段（均为可选，有默认值）：

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

CLI 参数可覆盖 `problem.json` 中的对应配置项。类型和范围校验在 `load_problem_config()` 中完成：整数字段必须为正、eps 必须非负、compare_mode 和 sandbox_type 必须为已知值。

### 题目目录结构

```text
problems/<name>/
  problem.json
  input/
    1.in
    2.in
  output/
    1.out
    2.out
```

### judge_log.json

当前日志结构（以实际输出为准）：

```json
{
    "submission": "...",
    "run_id": "YYYYMMDD_HHMMSS_<pid>",
    "run_dir": "build/runs/<run_id>/",
    "problem_dir": "...",
    "executable_file": "...",
    "compile_error_file": "...",
    "user_output_dir": "...",
    "time_limit_ms": 1000,
    "memory_limit_mb": 128,
    "output_limit_mb": 1,
    "compile_time_limit_ms": 5000,
    "compare_mode": "floating",
    "sandbox_type": "builtin",
    "float_abs_eps": 1e-6,
    "float_rel_eps": 1e-6,
    "results": [
        {
            "case": "1",
            "input_file": "...",
            "standard_output_file": "...",
            "user_output_file": "...",
            "user_error_file": "...",
            "run_result": "OK",
            "time_ms": 5,
            "memory_mb": 3,
            "exit_code": 0,
            "verdict": "AC"
        }
    ],
    "final_verdict": "Accepted",
    "passed": 2,
    "total": 2
}
```

稳定 schema（`schema_version`）、`rootfs_version`、`case_results` 完整字段等属于后续阶段的计划，当前尚未实现。详见 [ROADMAP.md](ROADMAP.md)。

---

## 9. 安全边界

### 当前安全能力

| 模式 | 适用场景 | 已验证的隔离 |
|------|---------|-------------|
| builtin | 本地可信代码、开发调试 | rlimit（AS/FSIZE/CORE/CPU/NOFILE/NPROC）、进程组清理、环境变量清空、fd 关闭、`/proc` 内存监控 |
| nsjail | 计划用于公开不可信代码 | chroot、bind-mount 文件系统隔离、procfs 禁用、网络 namespace、rlimit、per-run sandbox_root、编译沙箱分离 |

### 已知安全缺口

nsjail 后端尚未完成：

- cgroup v2 精确内存和进程数控制（当前使用 RLIMIT_AS 近似）
- seccomp 系统调用过滤
- 固定最小 rootfs（当前动态 bind-mount 宿主机路径）
- 低权限 UID/GID 映射和 user namespace
- 独立编译 rootfs（当前与运行 rootfs 共享部分路径）
- 编译阶段仍只读挂载宿主机 `/usr`（允许读取系统头文件）

builtin runner 不提供任何形式的 namespace、chroot、seccomp 或网络隔离。

详细安全测试矩阵见 [nsjail 安全测试矩阵](security-test-matrix.md)。nsjail 加固计划见 [nsjail-plan.md](nsjail-plan.md)。

---

## 10. 仓库结构

```text
src/                    # 源码
  main.cpp              # 入口
  config.h              # 默认常量
  judge.h / judge.cpp   # 判题编排
  compiler.h / compiler.cpp  # 编译模块
  runner.h / runner.cpp      # 运行模块
  comparer.h / comparer.cpp  # 输出比较
include/
  json.hpp              # nlohmann/json v3.12.0
tests/
  unit/                 # 单元测试
  integration/          # 集成测试脚本
  regression/           # 回归测试
  security/             # 安全测试（nsjail MUST_BLOCK + KNOWN_GAP）
  fixtures/             # 测试 fixture
  support/              # 测试辅助
scripts/                # 测试运行和环境诊断脚本
problems/               # 示例题目
submissions/            # 测试用提交（ac/wa/tle/mle/ole/re/ce/security）
docs/                   # 项目文档
.claude/                # Claude Code 控制面
.github/workflows/      # CI 配置
CMakeLists.txt          # 构建和 CTest 定义
```

---

## 11. 当前状态

### 已完成

- 结构化错误模型：`CompileInfo` / `RunInfo` 完整实现，judge.cpp 不通过猜字符串区分 RE/SE。
- 自动化测试框架：4 层测试（unit/integration/regression/security），一条命令运行（`run_all_tests.sh`），支持 CI。
- nsjail MVP：编译和运行两个阶段的 nsjail 封装，per-run sandbox_root，基础文件系统和网络隔离。
- 回归测试覆盖：stderr 独立性、结构化错误、MLE 分类等关键回归。
- 生产模式 fail closed：`CPPJUDGE_PRODUCTION` 环境变量保护。

### 进行中

- 产品级 nsjail 加固：cgroup v2、seccomp、固定 rootfs、低权限身份映射。

### 尚未开始

- CLI 子命令（judge/doctor/validate/version/init-problem）。
- 正式 schema（problem.json schema_version、稳定 judge_log.json API）。
- 安装、发布、文档完善。
- 多 worker、Special Judge、多语言等扩展功能。

完整产品化计划见 [ROADMAP.md](ROADMAP.md)。
