# CPPJUDGE

CPPJUDGE 是一个面向 Linux 的单机 C++ 判题器。它会编译提交代码、自动运行题目目录中的测试点、执行资源限制、比较输出，并把完整结果写入 `build/judge_log.json`。

产品级安全路径是 **nsjail + cgroup v2 + seccomp**。`builtin` runner 仅用于学习、实验和可信环境调试，不应作为运行不可信代码的安全边界。

## Quick Start

### 1. 构建

```bash
cmake -S . -B build
cmake --build build -j2
```

### 2. 添加题目

建立如下目录：

```text
problems/<problem-name>/
  problem.json
  input/
    1.in
    2.in
  output/
    1.out
    2.out
```

`input/1.in` 对应 `output/1.out`，`input/2.in` 对应 `output/2.out`。每个 `.in` 文件都必须有同名 `.out`；缺少标准输出属于题目数据错误，评测结果为 `System Error`。

### 3. 编写 problem.json

题目的时间、内存、输出、编译、比较方式和沙箱限制都写在 `problem.json` 中。普通用户无需在命令行重复输入这些限制。

学习/实验环境可以使用 builtin：

```json
{
  "title": "A+B",
  "time_limit_ms": 1000,
  "memory_limit_mb": 128,
  "output_limit_mb": 1,
  "compile_time_limit_ms": 5000,
  "compare_mode": "exact",
  "sandbox_type": "builtin"
}
```

产品安全路径使用 nsjail，并要求宿主机正确配置 cgroup v2 delegation 和 seccomp：

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

支持的字段：

- `title`：题目名称。
- `time_limit_ms`：单测试点时间限制，正整数，单位毫秒。
- `memory_limit_mb`：内存限制，正整数，单位 MB。
- `output_limit_mb`：输出限制，正整数，单位 MB。
- `compile_time_limit_ms`：编译时间限制，正整数，单位毫秒。
- `compare_mode`：`exact` 或 `floating`。
- `sandbox_type`：`builtin`、`nsjail` 或尚未实现的 `isolate`。
- `float_abs_eps`、`float_rel_eps`：浮点比较使用的非负误差，可选。

### 4. 提交代码并判题

普通用户只需提供题目目录和提交文件。仓库默认的 `submissions/solution.cpp` 是 A+B 的 Accepted 示例，因此第一条命令应稳定返回 `Accepted`：

```bash
./build/cppjudge judge --problem problems/A+B --submission submissions/solution.cpp
```

也可以使用简写：

```bash
./build/cppjudge judge submissions/solution.cpp --problem problems/A+B
```

查看命令帮助：

```bash
./build/cppjudge --help
./build/cppjudge judge --help
```

高级用户可以临时覆盖 `problem.json` 中的限制：

```bash
./build/cppjudge judge \
  --problem problems/A+B \
  --submission submissions/solution.cpp \
  --time-limit-ms 2000 \
  --memory-limit-mb 256 \
  --output-limit-mb 4 \
  --compare-mode floating \
  --compile-time-limit-ms 10000
```

还可使用 `--sandbox-type` 临时覆盖沙箱类型。override 只适合调试和高级用途，题目限制的权威来源仍应是 `problem.json`。

### 5. 查看结果

终端会显示最终 verdict。Quick Start 示例应输出 `Final Verdict: Accepted`。完整 JSON 日志位于：

```bash
python3 -m json.tool build/judge_log.json
```

每次评测的独立日志和输出保存在 `build/runs/<run_id>/`。

## Legacy / Developer Override 用法

旧 positional CLI 继续兼容已有脚本和开发流程：

```bash
./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 exact 5000
```

参数顺序依次为 submission、problem、time、memory、output、compare mode 和 compile time。新用户应优先使用 `cppjudge judge`，让限制来自 `problem.json`。

## 当前范围

CPPJUDGE 当前没有真正的 `import`、zip 上传、题目上传服务、Web 前端、数据库或比赛系统。题目通过目录和 `problem.json` 管理，提交通过本地文件传入。


## 诊断、版本和退出码

查看版本：

```bash
./build/cppjudge --version
```

也可以使用：

```bash
./build/cppjudge version
```

检查当前机器的产品安全路径就绪情况：

```bash
./build/cppjudge doctor
```

`doctor` 只做环境诊断，不执行判题。它会报告 cppjudge version、git commit、platform、当前工作目录、nsjail、cgroup v2、当前 cgroup path、memory/pids controller、seccomp policy，以及产品沙箱就绪状态。

状态含义：

- `READY`：nsjail 可用，cgroup v2 和 memory/pids controller 可见，seccomp policy 可读，并且当前 cgroup 看起来可写入 delegated child cgroup。
- `NOT_READY`：关键依赖缺失，例如 nsjail 不存在、cgroup v2 不存在或 seccomp policy 不可读。
- `NOT_VERIFIED`：依赖存在，但当前会话无法证明产品路径完整可用；普通 SSH session 常见这种状态，因为缺少 delegated cgroup 写权限。

基础 exit code 语义：

| Exit code | 含义 |
|-----------|------|
| 0 | 命令成功执行；judge verdict 为 Accepted；或 doctor 为 READY |
| 1 | 判题完成但 verdict 不是 Accepted，例如 WA / CE / RE / TLE / MLE / OLE |
| 2 | CLI 参数错误；或 doctor 运行成功但产品路径为 NOT_READY / NOT_VERIFIED |
| 3 | System Error 或工具自身错误 |

## judge_log.json schema v1

每次判题都会写出 `build/judge_log.json`。顶层字段包含：

```json
{
  "schema_version": 1,
  "tool": "cppjudge",
  "cppjudge_version": "0.1.0-dev",
  "git_commit": "10d2bdd",
  "cli_mode": "judge",
  "problem_dir": "problems/A+B",
  "submission_file": "submissions/solution.cpp",
  "final_verdict": "Accepted",
  "results": []
}
```

旧字段不会删除；schema v1 只是给现有日志补充稳定识别字段。更完整的草案见 `docs/JSON_SCHEMA.md`。题目目录格式见 `docs/PROBLEM_FORMAT.md`，排错指南见 `docs/TROUBLESHOOTING.md`，发布前检查见 `docs/RELEASE_CHECKLIST.md`。


## 评测结果

- `Accepted`: 所有测试点通过。
- `Wrong Answer`: 用户程序正常结束，但输出与标准答案不同。
- `Time Limit Exceeded`: 用户程序超过运行时间限制。
- `Memory Limit Exceeded`: 用户程序超过内存限制。
- `Output Limit Exceeded`: 用户程序超过输出大小限制。
- `Runtime Error`: 用户程序崩溃或异常退出。
- `Compile Error`: 提交源码编译失败。
- `System Error`: 判题配置、题目数据、沙箱后端或本地执行环境出错。

`Runtime Error` 只用于用户程序本身的运行错误。`System Error` 用于判题侧问题，例如非法 `problem.json`、缺失输入/输出目录、缺失标准答案文件、沙箱不可用或后端未实现。

---

## 沙箱说明

CPPJUDGE 当前暴露三个沙箱后端名称：

- `builtin`: 已实现，默认用于本地开发和 CI 回归测试
 - `nsjail`: 已实现编译/运行路径，并接入 cgroup v2 与 seccomp；需要系统安装外部 `nsjail`，且产品路径需要 delegated cgroup 环境
- `isolate`: 预留后端，尚未实现

`builtin` 是默认路径，也仍然是 GitHub Actions 默认回归测试使用的路径。它适合本地开发、课程演示和可信环境调试，但不能作为产品级安全沙箱。

`nsjail` 后端现在覆盖两个阶段：

- 编译阶段：把用户源码复制到每次运行目录中的 `submission.cpp`，通过 `nsjail` 调用 `/usr/bin/g++` 编译出 `solution`。
- 运行阶段：把编译出的 `solution` 只读挂载进运行沙箱，再按测试点运行。

运行阶段的 nsjail 路径使用每次运行独立的 `sandbox_root`，而不是把宿主机根目录暴露给 jail。它只读挂载编译后的 `solution`、读写挂载本次运行的 `user_output` 目录，并只读挂载普通 C++ 动态链接所需的一小组库文件。同时，运行沙箱内禁用了 procfs。

当前 nsjail 的动态库挂载仍然是面向 C++ 的最小集合，足够覆盖现有回归测试，但还不是通用依赖方案。后续如果支持更多语言或更复杂的 C++ 依赖，需要做依赖扫描，或者维护一个受控的最小 rootfs。

nsjail 后端目前施加基础 rlimit，并已接入 cgroup v2 生命周期：运行前创建 run cgroup，写入 `memory.max`、`pids.max`，通过 `memory.events` 辅助可信 MLE 分类，并在超限或清理时使用 `cgroup.kill`。这些能力需要运行环境授予 cgroup delegation；普通 SSH 会话可能只能跳过相关测试。

当前 nsjail 仍然不是最终产品级沙箱配置：还缺完整最小 rootfs、seccomp allow-list、可重复的 delegated 测试入口，以及明确的低权限用户映射。`isolate` 仍然只是占位后端，会返回 `System Error`。

`builtin` runner 提供基础本地执行包装：

- 子进程执行
- stdin/stdout/stderr 重定向
- 进程组清理和 `SIGKILL`
- `RLIMIT_AS` 虚拟地址空间限制
- `RLIMIT_FSIZE` 输出文件大小限制
- `RLIMIT_CORE = 0` 禁止 core dump
- `RLIMIT_NOFILE = 64` 打开文件数限制
- 支持时使用 `RLIMIT_NPROC = 16` 做进程数保护
- `RLIMIT_CPU` 作为 CPU 时间兜底限制
- 通过 `/proc/<pid>/status` 监控内存

这些检查对本地测试有帮助，但不等价于完整 Linux 沙箱。builtin runner 没有命名空间隔离、文件系统隔离、网络隔离、seccomp 或 cgroup 资源隔离。

---

## 安全性说明

CPPJUDGE 当前仍然是教学导向、本地开发优先的判题系统。`builtin` runner 不是产品级安全沙箱。

当前 builtin runner 不提供：

- Linux namespace 隔离
- seccomp 系统调用过滤
- cgroup v2 资源隔离
- chroot / pivot_root 文件系统隔离
- 网络 namespace 隔离
- 专用低权限用户隔离
- 完整容器隔离

不要把当前版本直接暴露到公网。不要用 builtin runner 在生产环境运行来自未知用户的任意提交。

编译阶段也需要沙箱。虽然编译阶段没有运行学生程序，但编译器会用判题机权限处理学生可控源码。恶意源码可能通过 `#include` 尝试读取宿主机文件，或者通过模板、宏、临时文件和链接阶段消耗资源。仅有编译时间限制不能防止文件读取、磁盘消耗、内存消耗或编译器漏洞风险。因此，生产路径应该把编译和运行都视为不可信阶段。

未来安全工作应继续围绕成熟沙箱后端、cgroup/seccomp、网络隔离、文件系统隔离和最小权限部署推进。

---

## 运行测试

默认回归测试：

```bash
bash scripts/run_tests.sh
```

该脚本会构建项目并检查主要判题路径：

- AC / WA / TLE / MLE / OLE / RE / CE
- 部分 `System Error` 场景
- 最新 `build/judge_log.json`
- 每次运行的 `run_id` 和 `run_dir`
- 测试点结果中的必要调试字段

手动 builtin 安全测试：

```bash
bash scripts/run_security_tests.sh
```

手动 nsjail MVP 测试：

```bash
bash scripts/run_nsjail_tests.sh
```

如果没有安装 `nsjail`，nsjail 测试脚本会打印跳过信息并成功退出。如果 `nsjail` 可用，脚本会临时复制 `problems/A+B`，把复制后的题目配置改为 `sandbox_type=nsjail`，然后检查当前 MVP 路径中的 Accepted、Runtime Error、Time Limit Exceeded、Memory Limit Exceeded、Output Limit Exceeded、stderr 捕获、基础文件系统隔离、网络 namespace 隔离，以及编译期文件系统隔离。

builtin 安全测试会覆盖打开文件数限制、进程数行为、core dump 禁止和 stderr 捕获。它包含 fork 相关行为，因此没有放进默认 GitHub Actions 流程。请只在受控本地环境运行。nsjail 测试同样是手动测试，不由 GitHub Actions 默认运行。

---

## nsjail / cgroup 环境检查

在使用 nsjail/cgroup 产品路径之前，应先检查目标机器：

```bash
bash scripts/check_nsjail_env.sh
```

该脚本会报告：

- 是否安装了 `nsjail`
- 当前 `nsjail` 构建是否支持 cgroup 相关参数
- 宿主机是否看起来使用 cgroup v2
- 当前进程所在的 cgroup 路径
- 当前用户是否能在 `/sys/fs/cgroup` 下创建子 cgroup

这个脚本只用于诊断。它不会改变判题行为，也不是必需 CI 检查。当前 nsjail 生产路径会 fail closed：如果缺少 delegated cgroup 或 seccomp policy，nsjail 判题应返回 `System Error`，而不是静默退回不安全模式。

预览 nsjail cgroup 参数可以运行：

```bash
bash scripts/preview_nsjail_cgroup_args.sh 128 16 0
```

该 dry-run 脚本只打印类似 `--use_cgroupv2`、`--cgroup_mem_max`、`--cgroup_pids_max` 的参数，不会执行 nsjail，也不会改变当前 runner 行为。真实验证应使用 delegated systemd/service 环境运行 nsjail 与 security profile。

---

## 生产模式

设置 `CPPJUDGE_ENV=production` 或 `CPPJUDGE_PRODUCTION=1` 可启用生产模式保护。生产模式下，如果选中的沙箱不安全或不可用，CPPJUDGE 会在编译或运行前直接 fail closed。

生产模式规则：

- 拒绝 `builtin`，因为它不是真正的安全沙箱。
- 拒绝 `isolate`，直到该后端真正实现。
- 使用 `nsjail` 前必须能在 `PATH` 中找到 nsjail。
- nsjail 生产路径需要 cgroup v2 memory/pids delegation。
- nsjail 生产路径需要可读的 seccomp policy。
- 当 `sandbox_type=nsjail` 时，编译阶段也会通过 nsjail 执行，源码会先复制到本次运行目录。

生产模式本身不会把当前 nsjail 路径变成产品级沙箱；它的作用是防止生产路径意外退回不安全或缺失的沙箱后端。是否达到产品级，还取决于 delegated nsjail/security/seccomp 测试是否真实运行并通过。

---

## 持续集成

GitHub Actions 会在每次 `push` 和 `pull_request` 时运行默认回归测试：

```bash
bash scripts/run_tests.sh
```

手动安全测试和 nsjail MVP 测试默认不在 CI 中运行。

---

## 路线图

- 独立运行目录
- 可配置沙箱后端
- nsjail 集成
- isolate 集成
- cgroup v2
- seccomp 白名单
- 网络隔离
- 文件系统隔离
- Web API / worker 队列

---

## 文档

- [项目总览](docs/OVERVIEW.md)
- [开发路线图](docs/ROADMAP.md)
- [工作区策略](docs/WORKSPACE.md)
- [测试约定](docs/TESTING.md)
- [题目格式](docs/PROBLEM_FORMAT.md)
- [JSON schema v1 草案](docs/JSON_SCHEMA.md)
- [故障排查](docs/TROUBLESHOOTING.md)
- [发布检查清单](docs/RELEASE_CHECKLIST.md)
- [nsjail 加固计划](docs/nsjail-plan.md)
- [nsjail 安全测试矩阵](docs/security-test-matrix.md)

## 作者

XY-729

## nsjail 设计说明

nsjail 后端现在已经有 MVP runner，但还不是最终产品级沙箱配置。更详细的加固计划见 `docs/nsjail-plan.md`。
