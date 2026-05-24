# CPPJUDGE

CPPJUDGE 是一个轻量级 C++ 判题内核，适合本地学习、课程项目和可信实验环境使用。它可以编译一份 C++ 提交，把程序运行在题目测试数据上，施加基础资源限制，比较输出结果，并写出结构化的 `judge_log.json` 方便调试。

当前项目仍然是教学导向、本地优先的判题原型。它的实际目标是逐步演进成适合学校或实验室可信环境使用的 C++ 判题后端。内置 runner 适合开发和实验，但不是产品级安全沙箱。

---

## 项目简介

CPPJUDGE 是一个聚焦 C++ 语言的小型在线判题原型。目前支持：

- 编译 C++17 提交
- 对多个输入/输出测试点运行提交程序
- 运行时间、内存、输出大小和编译时间限制
- 精确比较和浮点误差比较
- 每次评测独立的运行目录
- 详细的 `judge_log.json` 日志

当前设计目标是在保持核心判题流程简单的同时，为后续接入 `nsjail`、`isolate` 等沙箱后端做好结构准备。

---

## 功能特性

- Compile Error
- Accepted / Wrong Answer
- Time Limit Exceeded
- Memory Limit Exceeded
- Output Limit Exceeded
- Runtime Error
- System Error
- `exact` / `floating` 比较模式
- `sandbox_type`: `builtin` (自己写的残废版)/ `nsjail` (接了一半)/ `isolate`(还没搞)
- JSON 判题日志
- `build/runs/<run_id>/` 下的独立运行目录
- `build/judge_log.json` 最新日志快捷路径

---

## 构建

```bash
mkdir -p build
cd build
cmake ..
make
```

Rocky Linux / Fedora 上如果缺少基础构建工具，可以先安装：

```bash
sudo dnf install gcc-c++ make cmake
```

---

## 使用方法

```bash
./build/cppjudge [submission_file] [problem_dir] [time_limit_ms] [memory_limit_mb] [output_limit_mb] [compare_mode] [compile_time_limit_ms]
```

参数说明：

- `submission_file`: C++ 源码文件，默认 `submissions/solution.cpp`
- `problem_dir`: 题目目录，默认 `problems/A+B`
- `time_limit_ms`: 运行时间限制，单位毫秒，必须是正整数
- `memory_limit_mb`: 内存限制，单位 MB，必须是正整数
- `output_limit_mb`: 输出大小限制，单位 MB，必须是正整数
- `compare_mode`: `exact`、`floating` 或 `float`
- `compile_time_limit_ms`: 编译超时时间，单位毫秒，必须是正整数

示例：

```bash
./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 floating 5000
python3 -m json.tool build/judge_log.json
```

命令行参数会覆盖 `problem.json` 中对应的配置项。

---

## 题目目录格式

每个题目目录包含一个 `problem.json`，以及成对的输入/输出文件：

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

输入文件使用 `.in` 后缀。对于每个 `input/<case>.in`，CPPJUDGE 期望存在对应的 `output/<case>.out`。如果标准输出文件缺失，会判为 `System Error`，因为这表示题目数据不完整。

---

## problem.json 示例

```json
{
    title: A+B,
    time_limit_ms: 1000,
    memory_limit_mb: 128,
    output_limit_mb: 1,
    compile_time_limit_ms: 5000,
    compare_mode: floating,
    float_abs_eps: 1e-6,
    float_rel_eps: 1e-6,
    sandbox_type: builtin
}
```

支持的配置项：

- `time_limit_ms`、`memory_limit_mb`、`output_limit_mb`、`compile_time_limit_ms`: 正整数
- `compare_mode`: `exact`、`floating` 或 `float`
- `float_abs_eps`、`float_rel_eps`: 非负数字
- `sandbox_type`: `builtin`、`nsjail` 或 `isolate`

如果配置类型或取值不合法，会判为 `System Error`，并把错误信息写入 `judge_log.json` 的 `error` 字段。

---

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
- `nsjail`: MVP 版本已实现，需要系统安装外部 `nsjail` 可执行文件
- `isolate`: 预留后端，尚未实现

`builtin` 是默认路径，也仍然是 GitHub Actions 默认回归测试使用的路径。它适合本地开发、课程演示和可信环境调试，但不能作为产品级安全沙箱。

`nsjail` 后端现在覆盖两个阶段：

- 编译阶段：把用户源码复制到每次运行目录中的 `submission.cpp`，通过 `nsjail` 调用 `/usr/bin/g++` 编译出 `solution`。
- 运行阶段：把编译出的 `solution` 只读挂载进运行沙箱，再按测试点运行。

运行阶段的 nsjail MVP 使用每次运行独立的 `sandbox_root`，而不是把宿主机根目录暴露给 jail。它只读挂载编译后的 `solution`、读写挂载本次运行的 `user_output` 目录，并只读挂载普通 C++ 动态链接所需的一小组库文件。同时，运行沙箱内禁用了 procfs。

当前 nsjail 的动态库挂载仍然是面向 C++ 的最小集合，足够覆盖现有回归测试，但还不是通用依赖方案。后续如果支持更多语言或更复杂的 C++ 依赖，需要做依赖扫描，或者维护一个受控的最小 rootfs。

nsjail 后端目前施加基础 rlimit，包括地址空间、输出文件大小、core dump、CPU 时间、打开文件数和进程数。内存限制主要依赖 `RLIMIT_AS`，带有少量动态链接器启动余量。更精确的 MLE 仍然需要接入 cgroup v2。

当前 nsjail 仍然不是最终产品级沙箱配置：还缺完整最小 rootfs、seccomp 策略、cgroup 内存强约束，以及明确的低权限用户映射。`isolate` 仍然只是占位后端，会返回 `System Error`。

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

在把 cgroup v2 参数真正接入 nsjail runner 之前，应先检查目标机器：

```bash
bash scripts/check_nsjail_env.sh
```

该脚本会报告：

- 是否安装了 `nsjail`
- 当前 `nsjail` 构建是否支持 cgroup 相关参数
- 宿主机是否看起来使用 cgroup v2
- 当前进程所在的 cgroup 路径
- 当前用户是否能在 `/sys/fs/cgroup` 下创建子 cgroup

这个脚本只用于诊断。它不会改变判题行为，也不是必需 CI 检查。cgroup 检查失败不会让普通判题失败。如果 cgroup v2 不可用，nsjail runner 会继续使用当前 rlimit 兜底方案。未来只有在目标机器通过相关预检查后，才应启用 cgroup 内存和进程数强约束。

预览未来 nsjail cgroup 参数可以运行：

```bash
bash scripts/preview_nsjail_cgroup_args.sh 128 16 0
```

该 dry-run 脚本只打印类似 `--use_cgroupv2`、`--cgroup_mem_max`、`--cgroup_pids_max` 的参数，不会执行 nsjail，也不会改变当前 runner 行为。

---

## 生产模式

设置 `CPPJUDGE_ENV=production` 或 `CPPJUDGE_PRODUCTION=1` 可启用生产模式保护。生产模式下，如果选中的沙箱不安全或不可用，CPPJUDGE 会在编译或运行前直接 fail closed。

生产模式规则：

- 拒绝 `builtin`，因为它不是真正的安全沙箱。
- 拒绝 `isolate`，直到该后端真正实现。
- 使用 `nsjail` 前必须能在 `PATH` 中找到 nsjail。
- 当 `sandbox_type=nsjail` 时，编译阶段也会通过 nsjail 执行，源码会先复制到本次运行目录。

生产模式本身不会把当前 nsjail MVP 变成产品级沙箱；它的作用是防止生产路径意外退回不安全或缺失的沙箱后端。

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

## 作者

XY-729

## nsjail 设计说明

nsjail 后端现在已经有 MVP runner，但还不是最终产品级沙箱配置。更详细的加固计划见 `docs/nsjail-plan.md`。
