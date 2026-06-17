# CPPJUDGE Roadmap

## 1. 总体目标

CPPJUDGE 的长期目标是成为一个适合学校或实验室可信环境使用的 C++ 判题后端。1.0 版本的完成标准是：

- 安全行为可验证
- 判定结果可信
- 安装与排错简单
- 接口和日志稳定

## 2. 状态说明

每个阶段使用以下状态标记：

- **Completed**: 全部验收标准已满足。
- **In Progress**: 部分完成，仍有未完成事项。
- **Planned**: 尚未开始。

---

## 3. 阶段 1：错误模型和判定可靠性

**Status: In Progress**

核心目标：runner 不再把沙箱故障伪装成 RE，judge 不再读取 stderr 并通过字符串猜测 SE，编译和运行返回结构化结果。

### 已完成

- `CompileInfo` 和 `RunInfo` 结构化结果类型，`CompileResult`（OK/CE/SE）和 `RunResult`（OK/TLE/MLE/OLE/RE/SE）枚举。
- `compile_cpp_structured()` 和结构化 runner 变体，返回 `CompileInfo`/`RunInfo`。
- `pipe2(O_CLOEXEC)` exec 失败检测管道：编译器 g++ 启动失败 → SE，nsjail 启动失败 → SE，solution exec 失败 → SE。
- `judge.cpp` 不再依赖 stderr 字符串匹配判断 SE（该工作在 runner/compiler 层完成）。
- `run_id` 唯一性：`YYYYMMDD_HHMMSS_<pid>`。
- 文件系统异常处理：文件打开失败 → SE。
- 系统调用返回值检查：`fork` 失败 → SE，`waitpid` 失败 → SE。
- Verdict 优先级明确：SE > 其他 RunResult > WA，所有测试点都会被运行。
- exit_code 和 signal 记录到日志。

### 验收标准

- 所有错误都有明确来源。
- judge.cpp 不再通过猜字符串区分用户错误和系统错误。


### 尚未完成

- nsjail runner 仍通过解析 stderr 判断 TLE、MLE、OLE：
  `stderr_says_tle` / `stderr_says_mle` / `stderr_says_ole` 直接设置 verdict
  （`src/runner.cpp:938-965`）。
- builtin runner 仍通过检查 error_file 中的 `bad_alloc`、
  `Cannot allocate memory` 等关键词辅助判断 MLE
  （`src/runner.cpp:680-682`）。
- 目标：使用可信内核状态（cgroup `memory.events`、进程信号）替代
  文本匹配，在阶段 3 的 cgroup v2 集成中解决。

**关键 commit**: `ecc46d1`, `43884a7`

---

## 4. 阶段 2：完整自动化测试体系

**Status: Completed**

### 已完成

四层测试已建立：

- **单元测试**: `test_comparer`、`test_sandbox_types`、`test_verdict_strings`、`test_config`（覆盖 comparer、sandbox 类型转换、verdict 字符串、配置验证、浮点比较、exact 比较、时间和内存单位换算）。
- **集成测试**: builtin verdicts（AC/WA/TLE/MLE/OLE/RE/CE/SE）、illegal config、nsjail verdicts（条件启用）。
- **回归测试**: 默认回归、安全回归、结构化错误回归、stderr 独立性回归、MLE 分类回归。
- **安全测试**: nsjail MUST_BLOCK（读取 /etc/passwd、读取 /proc、连接外网、创建 socket、fork bomb、thread storm、执行 /bin/sh、修改 solution、向未允许目录写文件、符号链接逃逸、读取环境变量、继承额外 fd）、nsjail KNOWN_GAP（AF_INET socket、AF_UNIX socket、fork bomb 精确限制、thread storm、/bin/sh 可用性、seccomp 缺失、RLIMIT_AS 近似内存控制、rootfs 暴露）。

CI 集成：GitHub Actions 在 push/PR 时运行 portable profile（11 个测试），测试失败返回非零。

统一测试入口：

```bash
bash scripts/run_all_tests.sh portable    # CI 默认
bash scripts/run_all_tests.sh quick       # 快速检查
bash scripts/run_all_tests.sh full        # 全部测试
```

### 尚未完成

- CI 中默认运行 nsjail 安全测试（需要 CI 环境安装 nsjail）。
- 更多 verdict 边界测试。

### 验收标准

- 一条命令运行全部测试。
- 测试失败返回非零。
- 可用于 CI。
- 每个 verdict 有稳定测试。
- 关键安全能力有攻击测试。

**关键 commit**: `2dae1e3`（默认初始化修复）

---

## 5. 阶段 3：nsjail 产品级加固

**Status: In Progress**

当前 nsjail MVP 已完成基本 chroot、文件系统隔离、网络隔离和 rlimit 覆盖。以下子阶段待完成。

### 5.1 cgroup v2

**Status: Planned**

目标使用 cgroup v2 统一层级：

- `memory.max` — 硬内存上限
- `memory.peak` — 峰值内存记录
- `memory.events` — OOM 事件检测
- `pids.max` — 进程数上限
- `cgroup.kill` — 超限时清理

判定映射：

- `oom_kill` → MLE
- 墙钟超时 → TLE
- 输出达到限制 → OLE
- 非零退出或信号崩溃 → RE

不得依赖 stderr 判断 MLE。

环境预检查和 dry-run 脚本已就绪：
- `scripts/check_nsjail_env.sh` — 诊断 cgroup v2 可用性
- `scripts/preview_nsjail_cgroup_args.sh` — 预览 nsjail cgroup 参数

### 5.2 seccomp

**Status: Planned**

计划限制高风险系统调用：

- `mount`、`umount`、`pivot_root`
- `ptrace`
- `bpf`
- `keyctl`
- `perf_event_open`
- `reboot`、`kexec_load`
- 模块加载（`init_module`、`finit_module`、`delete_module`）
- namespace 再创建（`unshare`、`clone` 含 CLONE_NEW* 标志）
- 危险 `clone` 行为
- 网络调用（`socket`、`connect`、`bind`、`listen`、`accept` 等）

策略从可靠的限制规则逐步走向更严格的允许列表。

### 5.3 低权限身份

**Status: Planned**

- 专用 UID/GID 映射（非 root）。
- `user namespace` 映射。
- `no_new_privs` 防止提权。
- Capabilities 清空。
- 目录所有权：sandbox_root 和 user_output 对 jailed 用户可读写。

### 5.4 固定最小运行 rootfs

**Status: Planned**

- 版本化运行 rootfs，固定动态链接器和标准 C/C++ 库版本。
- 日志记录 `rootfs_version`。
- rootfs 升级有对应回归测试。
- 不再动态 bind-mount 宿主机 `/usr`、`/lib64` 等路径。

### 5.5 独立编译沙箱

**Status: Planned**

编译 rootfs 只提供必要组件：

- `g++`、`cc1plus`、`as`、`ld`
- 标准库头文件
- 必要动态库
- `/tmp`、`/work`

避免编译阶段只读挂载宿主机整个 `/usr`。

### 阶段 3 验收标准

- 公开不可信代码在受控 UID、rootfs、cgroup、seccomp 和网络隔离中运行。
- 关键安全能力缺失时 fail closed。

---

## 6. 阶段 4：CLI 和用户体验

**Status: Planned**

### 计划子命令

```text
cppjudge judge        # 执行判题
cppjudge doctor       # 环境诊断
cppjudge validate     # 题目配置校验
cppjudge version      # 版本信息
cppjudge init-problem # 生成题目模板
```

### 计划用法

```bash
cppjudge judge \
  --problem ./problems/A+B \
  --submission ./solution.cpp \
  --sandbox nsjail
```

### 计划参数

- `--json` — JSON 输出
- `--verbose` — 详细输出
- `--keep-run-dir` — 保留运行目录
- `--log-file` — 指定日志路径
- `--time-limit` / `--memory-limit` / `--output-limit` — CLI 覆盖限制

### doctor 计划检查

- 系统版本
- nsjail 可执行文件和版本
- namespace 可用性
- cgroup v2 状态
- memory/pids controller
- rootfs 完整性
- 编译器可用性
- 最小运行程序测试
- 网络隔离验证

### validate 计划检查

- `problem.json` 格式和字段
- input/output 文件对应关系
- 文件权限
- 限制值合法性
- 比较模式
- 空测试数据
- 重复测试点

### init-problem 计划生成

```text
<name>/
├── problem.json
├── input/
└── output/
```

### 验收标准

- 新用户阅读 README 后十分钟内可以安装、运行示例并排查环境问题。

---

## 7. 阶段 5：题目格式和日志格式

**Status: Planned**

### 计划中的 problem.json

```json
{
  "schema_version": 1,
  "time_limit_ms": 1000,
  "memory_limit_mb": 128,
  "output_limit_mb": 16,
  "compile_time_limit_ms": 10000,
  "compare_mode": "exact",
  "float_abs_eps": 0.000001,
  "float_rel_eps": 0.000001,
  "sandbox_type": "nsjail"
}
```

需定义：

- 必填字段和可选字段
- 默认值
- 数值范围
- 未知字段行为（拒绝 vs 忽略）
- schema 版本语义

### 计划中的 judge_log.json

```json
{
  "cppjudge_version": "1.0.0",
  "schema_version": 1,
  "run_id": "...",
  "start_time": "...",
  "problem": { "title": "...", "path": "..." },
  "submission": { "file": "...", "language": "c++17" },
  "sandbox": { "type": "nsjail", "rootfs_version": "..." },
  "compile_result": { "verdict": "OK", "time_ms": 1200, "memory_mb": 45 },
  "case_results": [
    {
      "case": "1",
      "verdict": "AC",
      "time_ms": 5,
      "memory_peak_mb": 3,
      "output_bytes": 12
    }
  ],
  "final_verdict": "Accepted",
  "system_error": null,
  "passed": 2,
  "total": 2
}
```

### 验收标准

- 外部程序只依赖 JSON，不需要解析终端输出。

---

## 8. 阶段 6：安装、发布和可移植性

**Status: Planned**

### 目标安装流程

```bash
cmake -S . -B build
cmake --build build
sudo cmake --install build
```

安装后：

```bash
cppjudge --version
cppjudge doctor
```

### 计划发布方式

- GitHub Release 二进制包
- Rocky Linux RPM
- 安装脚本
- 容器化开发环境

### 优先支持平台

- Rocky Linux 9 x86_64（1.0 主要目标）

其他系统标为实验支持。

### 计划版本

| 版本 | 内容 |
|------|------|
| 0.1.0 | MVP |
| 0.2.0 | 结构化错误 |
| 0.3.0 | 测试体系 |
| 0.4.0 | cgroup v2 |
| 0.5.0 | seccomp / rootfs |
| 0.9.0 | 发布候选 |
| 1.0.0 | 稳定 CLI 和日志 schema |

当前版本尚未正式发布。

### 计划文档

- `README.md`（已有）
- `INSTALL.md`
- `QUICKSTART.md`
- `PROBLEM_FORMAT.md`
- `SECURITY.md`
- `ARCHITECTURE.md`
- `TROUBLESHOOTING.md`
- `CHANGELOG.md`

---

## 9. 阶段 7：可靠性和性能

**Status: Planned**

### 并发模型

当前每份提交内部串行运行测试点是合理设计：

- 行为简单
- 资源统计清晰
- 日志顺序稳定
- 机器负载可控

需要并发时优先：

- 多个独立 worker 进程
- 每个 worker 处理一份提交
- 单份提交内部测试点继续串行

不优先在会频繁调用 `fork()` 的当前进程中加入线程池。

### 可靠性目标

- 运行目录自动清理（可配置保留失败现场）
- 磁盘使用上限
- 中断后清理 cgroup 和残留进程
- SIGTERM/SIGINT 优雅退出
- 临时文件原子写入
- 日志写入错误处理（磁盘满、权限变更）
- 并发 run_id 不冲突

---

## 10. 优先级

### P0（决定可靠性）

1. [x] 建立真实构建和测试基线
2. [x] 重构 RunInfo / CompileInfo，结构化区分 RE 和 SE
3. [x] 建立完整 verdict 回归测试
4. [ ] 接入 cgroup v2 内存和进程控制
5. [ ] 增加 seccomp
6. [ ] 明确低权限用户映射
7. [ ] 固定运行和编译 rootfs
8. [ ] 确保生产模式永不回退 builtin

### P1（决定易用性）

9. [ ] cppjudge doctor
10. [ ] 清晰的子命令和参数
11. [ ] 稳定的 problem.json 规范
12. [ ] 稳定的 judge_log.json 规范
13. [ ] 友好的错误信息
14. [x] 一条命令构建和测试
15. [ ] Quick Start 和示例题
16. [ ] 正式安装流程

### P2（产品成熟后）

17. [ ] 多 worker 并发
18. [ ] Special Judge
19. [ ] 多语言支持
20. [ ] 静态编译支持
21. [ ] 编译缓存
22. [ ] 远程任务队列
23. [ ] Web / API
24. [ ] 分布式评测

---

## 11. 推荐开发顺序

1. 运行全部测试，建立真实基线 **→ 已完成**
2. 结构化错误模型，理清 CE/RE/SE **→ 已完成**
3. 补齐测试与回归测试 **→ 已完成**
4. 接入 cgroup v2
5. 加入 seccomp 与低权限身份
6. 固定 rootfs，分别加固编译和运行
7. 实现 doctor 与 validate
8. 稳定 CLI、日志 schema 和题目格式
9. 完成安装、文档、版本和发布
10. 最后考虑并发和扩展功能

---

## 12. CPPJUDGE 1.0 完成标准

目标体验：

```bash
git clone ...
cmake -S . -B build
cmake --build build
sudo cmake --install build

cppjudge doctor

cppjudge judge \
  --problem examples/A+B \
  --submission examples/solution.cpp
```

成功输出目标：

```text
Accepted
Passed: 2/2
Time: 5 ms
Memory: 3 MB
Log: /.../judge_log.json
```

环境错误目标：

```text
System Error: cgroup v2 memory controller is unavailable
Run `cppjudge doctor --verbose` for details.
```

而不是只有：

```text
Runtime Error
```

或直接显示难以理解的原始 nsjail stderr。

最终目标：

- 安全行为可验证
- 判定结果可信
- 安装与排错简单
- 接口和日志稳定
