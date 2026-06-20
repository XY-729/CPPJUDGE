# CPPJUDGE 产品级差距审计

Updated: 2026-06-20

## 结论

CPPJUDGE 已经有一个可运行、可测试、结构相对清楚的 C++ 判题内核。作为本地课程项目、实验室内部原型或受信任环境工具，它已经相当可用。

作为“产品级判题器”，当前还差一个关键转折：从“代码里有安全机制”变成“部署中可重复证明安全机制真的生效”。这需要 delegated cgroup 环境、完整 nsjail/security 测试证据、固定 rootfs、低权限用户、严格 seccomp policy 和可诊断运维入口。

## 已经做得不错的部分

### 1. 判题核心

- 编译、运行、比较、日志输出链路完整。
- 支持 AC / WA / CE / RE / TLE / MLE / OLE / SE。
- 每次运行有独立 `run_id` 和 `build/runs/<run_id>/`。
- `judge_log.json` 能记录测试点、时间、内存、退出码、信号、错误信息等调试所需字段。

### 2. 错误模型

- `CompileInfo` 区分 OK / CE / SE。
- `RunInfo` 区分 OK / TLE / MLE / OLE / RE / SE。
- 编译器、runner、judge 的错误责任边界已经比早期清楚。
- 用户 stderr 伪造不再能被 judge 层当作系统错误来源。

### 3. 测试体系

- 已有 unit / integration / regression / security 测试分层。
- `scripts/run_all_tests.sh portable` 在 Rocky VM 通过，12/12 passed。
- builtin security regression 在当前 security profile 中通过。
- nsjail / seccomp 测试已经存在，但当前环境没有完整运行。

### 4. nsjail 产品化骨架

- 运行阶段有 per-run sandbox root。
- 编译阶段和运行阶段分开处理。
- 运行阶段禁用 procfs。
- 网络 namespace 隔离已有测试设计。
- cgroup v2 manager 已实现服务初始化、run cgroup、memory/pids limit、stats、kill 和 cleanup。
- seccomp policy 可通过 nsjail 参数接入。
- 生产模式会拒绝 builtin/isolate，并对 nsjail 做 fail-closed preflight。

## 距离产品级的主要不足

### P0-1：缺少 delegated cgroup 的可重复验证

当前环境检查结果：

```text
cgroup v2: detected
controllers: cpuset cpu io memory hugetlb pids rdma misc
subtree_control: memory pids
cgroup write permission: no
```

影响：

- nsjail/security/seccomp 测试在当前 SSH 会话中 skipped。
- 无法证明 `memory.max`、`pids.max`、`memory.events`、`cgroup.kill` 在真实运行中全部生效。
- 当前不能把 nsjail profile 的 PASS 解读为安全通过；它只是“测试集合没有失败”，其中关键项未运行。

建议：

- 使用 systemd user/service 方式提供 `Delegate=memory pids`。
- 增加一个标准脚本，例如 `scripts/run_delegated_nsjail_tests.sh`，统一启动 delegated service 并运行全量 profile。
- 把 skipped 和 passed 在报告中强区分，避免“绿色但没测”的错觉。

### P0-2：seccomp policy 仍偏宽

当前 policy：

```text
DENY { mount, pivot_root, chroot, reboot, ... }
DEFAULT ALLOW
```

影响：

- 能挡住一批明显高危 syscall。
- 但默认允许未知 syscall，不是产品级最小权限模型。
- 对 C++ 程序实际需要 syscall 的基线还没有冻结。

建议：

- 先保留 deny-list 作为过渡。
- 增加 syscall 观察/记录流程。
- 逐步形成 allow-list policy。
- 为基础程序、异常退出、文件 I/O、stdout/stderr、大输出、MLE/TLE 等场景建立 seccomp 兼容测试。

### P0-3：rootfs 未版本化

当前状态：

- 运行阶段挂载少量动态库文件。
- 编译阶段只读挂载宿主 `/usr`、`/lib64`、`/lib`、`/bin`、`/etc/alternatives`。

影响：

- 行为依赖宿主系统版本。
- 安全边界难审计。
- 不同机器间结果可能漂移。
- 编译阶段暴露面仍较大。

建议：

- 创建版本化 runtime rootfs。
- 创建版本化 compile rootfs。
- 在日志中写入 `rootfs_version`。
- 为 rootfs 内容维护 manifest 和完整性校验。

### P0-4：低权限用户模型未定型

当前没有形成稳定的 UID/GID 策略。产品级部署应明确：

- judge 主进程用户；
- nsjail 内用户；
- 编译阶段用户；
- 运行阶段用户；
- 文件属主与权限；
- systemd service sandboxing 边界。

建议：

- 不使用 root 运行常规 judge 服务。
- 使用专用不可登录用户。
- 明确 nsjail UID/GID 映射或 user namespace 策略。
- 写入 `SECURITY.md` 和 systemd 示例。

### P1-1：CLI 仍是开发型接口

当前 CLI 是位置参数：

```bash
./build/cppjudge [submission] [problem_dir] [time_limit_ms] [memory_limit_mb] [output_limit_mb] [compare_mode] [compile_time_limit_ms]
```

这适合开发，但不适合产品使用。建议演进为：

```bash
cppjudge judge --problem problems/A+B --submission submissions/solution.cpp
cppjudge doctor
cppjudge validate-problem problems/A+B
```

### P1-2：日志 schema 尚未稳定

当前日志已经能调试，但产品级需要：

- `schema_version`；
- `cppjudge_version`；
- `sandbox.version`；
- `rootfs_version`；
- `policy_version`；
- 每个 case 的稳定字段说明；
- 向后兼容策略。

### P1-3：安装、升级、排错文档不足

建议新增：

- `INSTALL.md`
- `SECURITY.md`
- `PROBLEM_FORMAT.md`
- `LOG_SCHEMA.md`
- `TROUBLESHOOTING.md`
- `CHANGELOG.md`

## Git / 仓库状态审计

GitHub 远端分支：

| 分支 | 状态 |
|------|------|
| `master` | 落后 VM 当前开发状态 |
| `stage2-testing` | GitHub 上停在 `4106bbb`，已被 VM `stage3d-rootfs` 包含 |
| `claudeworker` | 已被 VM `stage3d-rootfs` 包含 |
| `codex/judge-architecture-tests` | 已被 GitHub `master` 包含 |

建议清理顺序：

1. 把 VM `stage3d-rootfs` 合入 `master`。
2. 确认 CI / portable 测试通过。
3. 删除 `codex/judge-architecture-tests`。
4. 删除 `claudeworker`。
5. 删除 `stage2-testing`。

注意：删除远端分支属于破坏性仓库操作，应在明确确认后执行。

## 1.0 前置完成标准

CPPJUDGE 可以开始考虑 1.0 前，建议至少满足：

- portable profile PASS；
- delegated nsjail profile PASS，且无 skipped；
- security profile PASS，且 nsjail/seccomp 项无 skipped；
- 固定 runtime rootfs；
- 编译阶段 rootfs/资源限制可审计；
- seccomp policy 至少有版本化 deny-list，并有 allow-list 迁移计划；
- 明确低权限用户部署；
- `cppjudge doctor` 可诊断运行环境；
- `problem.json` 和 `judge_log.json` schema 文档稳定；
- 安装、部署、安全、排错文档齐备。

