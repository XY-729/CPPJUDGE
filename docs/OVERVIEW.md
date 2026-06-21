# CPPJUDGE 项目大纲

Updated: 2026-06-20

本文从当前状态出发，作为后续开发、审计和合并工作的项目大纲。旧的阶段性概览不再作为事实来源；当前事实以 `PROGRESS.md`、`LAST_TASK_REPORT.md` 和测试输出为准。

## 1. 项目定位

CPPJUDGE 是一个面向 Linux 的轻量级 C++ 判题内核。当前适合：

- 本地学习、课程项目和可信实验室环境；
- 单机 C++17 提交的编译、运行、输出比较和结构化日志；
- 继续演进为学校或实验室内部使用的判题后端。

当前还不能直接称为“公开互联网产品级判题器”。主要原因不是核心判题流程不可用，而是公开不可信代码运行所需的部署、隔离、审计和运维闭环还没有全部完成。

## 2. 当前可用能力

### 判题主流程

当前流程为：

```text
main()
  -> judge(argc, argv)
     -> load_problem_config()
     -> validate_sandbox_for_environment()
     -> compile_cpp_structured()
     -> run_program() for each case
     -> compare_output()
     -> write_log_file()
```

已具备：

- C++17 编译；
- 多测试点运行；
- exact / floating 输出比较；
- AC / WA / CE / RE / TLE / MLE / OLE / SE verdict；
- `CompileInfo` 和 `RunInfo` 结构化错误模型；
- `build/runs/<run_id>/judge_log.json` 和 `build/judge_log.json`；
- portable 测试 profile 通过。

### 沙箱后端

| 后端 | 当前状态 | 用途 |
|------|----------|------|
| `builtin` | 可用 | 本地开发、可信环境、CI portable 回归 |
| `nsjail` | 已有 cgroup v2 + seccomp 接入代码，但依赖部署授权 | 面向产品级沙箱的主路径 |
| `isolate` | 占位，返回 SE | 暂不作为开发重点 |

`builtin` 不是安全沙箱；不能用来运行公开不可信代码。

`nsjail` 已经具备重要基础：per-run sandbox root、编译/运行阶段分离、禁用 procfs、网络 namespace、cgroup v2 生命周期、seccomp policy 接入和 fail-closed preflight。但它的产品可用性取决于运行环境是否正确授予 cgroup delegation 和是否能完整跑通 nsjail/security 测试。

## 3. 当前验证状态

2026-06-20 在 Rocky VM 上确认：

```text
bash scripts/check_nsjail_env.sh
bash scripts/run_all_tests.sh portable
bash scripts/run_all_tests.sh nsjail
bash scripts/run_all_tests.sh security
```

结果摘要：

| 项目 | 状态 | 证据 |
|------|------|------|
| 构建 | PASS | portable profile 构建成功 |
| portable 测试 | PASS | 12/12 passed |
| builtin security 回归 | PASS | security profile 中 `test_regression_security` passed |
| nsjail/security 测试 | NOT_VERIFIED | 当前 SSH 会话无 cgroup write permission，相关测试 skipped |
| cgroup v2 环境 | PARTIAL | cgroup v2 与 memory/pids controllers 存在，但当前会话不能创建子 cgroup |
| seccomp policy | PARTIAL | policy 文件存在并接入 nsjail 参数，但当前策略是 deny-list + default allow |

关键环境事实：

```text
nsjail: found
cgroup v2: detected
nsjail cgroup flags: available
cgroup write permission: no
```

因此，当前代码方向是对的，但产品级安全承诺还不能盖章。

## 4. 产品级判题器还缺什么

### P0：安全与可信判定

1. 在受控 systemd service 中运行完整 nsjail/security profile。
   - 需要 `Delegate=memory pids`。
   - 当前 SSH 普通会话会 skip，不能作为安全通过证据。

2. 固定最小 rootfs。
   - 当前运行阶段只挂载必要动态库文件，编译阶段仍只读挂载宿主 `/usr`、`/lib64`、`/lib`、`/bin`。
   - 产品级应使用版本化 rootfs，并在日志记录 `rootfs_version`。

3. 低权限身份映射。
   - nsjail 参数中还没有明确的稳定 UID/GID 策略。
   - 产品级应使用专用 judge 用户、不可登录用户或 user namespace 映射。

4. seccomp 从 deny-list 走向 allow-list。
   - 当前 policy 禁止一批高危 syscall，但 `DEFAULT ALLOW` 仍偏宽。
   - 产品级应以最小 syscall allow-list 为目标，并为 C++ 基础程序、I/O、异常退出等行为建立兼容测试。

5. 编译阶段安全边界继续收紧。
   - 编译阶段已经通过 nsjail 包裹，但依赖宿主工具链和较宽的只读挂载。
   - 产品级应有独立编译 rootfs、编译输出目录限额、临时目录限额和更清晰的编译资源日志。

### P1：接口和运维

1. CLI 需要从位置参数演进为子命令。
   - 目标示例：`cppjudge judge --problem ... --submission ...`。

2. `cppjudge doctor`。
   - 检查 nsjail、cgroup delegation、seccomp policy、rootfs、低权限用户、构建目录权限。

3. 稳定 `problem.json` 和 `judge_log.json` schema。
   - 当前日志足够调试，但还没有 schema version、兼容策略和字段稳定性承诺。

4. 安装与部署文档。
   - 需要 `INSTALL.md`、`SECURITY.md`、`TROUBLESHOOTING.md` 和 systemd 部署说明。

5. 运行目录生命周期。
   - 需要可配置保留失败现场、定期清理、磁盘上限和异常退出清理策略。

### P2：扩展能力

- 多 worker 队列；
- Special Judge；
- 多语言；
- 编译缓存；
- Web/API 层；
- 分布式评测。

这些不是当前 1.0 安全闭环的前置条件。

## 5. 推荐开发主线

从现在起，建议把项目主线整理为：

```text
0. 当前文档和分支整理
1. 合并 stage3d/rootfs/cgroup/seccomp 现有工作到 master
2. 建立可重复的 delegated systemd 测试入口
3. 跑通 nsjail/security/seccomp 全量测试并保存证据
4. 固定最小 runtime rootfs
5. 收紧 seccomp policy，从 deny-list 迭代到 allow-list
6. 明确低权限 UID/GID 和部署用户模型
7. 稳定 CLI、problem schema、log schema
8. 发布 0.x 版本并补齐安装/排错文档
9. 再进入 worker/API/多语言等扩展阶段
```

## 6. Git 分支现状

GitHub 远端当前分支：

| 分支 | 提交 | 建议 |
|------|------|------|
| `master` | `0330f43` | 当前唯一 GitHub 分支；已包含 Stage 3A/3B/3C 与文档审计更新 |

VM 本地分支：

| 分支 | 提交 | 说明 |
|------|------|------|
| `master` | `0330f43` | VM 当前工作分支；已与 GitHub master 对齐 |

安全建议：

- GitHub 已收敛为单一 `master` 分支；
- 旧分支 `codex/judge-architecture-tests`、`claudeworker`、`stage2-testing` 已删除；
- 后续分支策略建议继续保持短生命周期开发分支，合并后及时清理。

## 7. 当前单一事实源

后续协作优先读取：

- `docs/OVERVIEW.md`：从现在起的项目大纲；
- `PROGRESS.md`：当前阶段和风险快照；
- `LAST_TASK_REPORT.md`：最近一次任务报告；
- `docs/product-readiness-audit.md`：产品级差距审计；
- `docs/TESTING.md`：测试 profile 说明；
- `.claude/`：Claude Code 工作约束。
