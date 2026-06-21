# CPPJUDGE 项目进度

Updated: 2026-06-20

## 当前结论

CPPJUDGE 已经从“教学原型”进入“具备产品化骨架的单机判题内核”阶段：核心判题流程、结构化错误模型、portable 测试、cgroup v2 管理代码和 seccomp 接入代码都已存在。

但它距离公开不可信代码的产品级判题器仍有明显差距。当前最大缺口不是普通 AC/WA/CE/RE 判题，而是可重复验证的安全部署闭环：cgroup delegation、nsjail 安全 profile、seccomp 策略强度、固定 rootfs、低权限身份和运维诊断。

## 阶段完成状态

| 阶段 | 状态 | 关键 Commit / 证据 | 备注 |
|------|------|--------------------|------|
| 阶段 1：结构化错误模型 | Completed | `ecc46d1`, `43884a7`, `d772239` | CE/RE/SE 结构化边界已建立；runner 不再依赖用户 stderr 伪造系统错误 |
| 阶段 2：测试基础设施 | Completed | `ac04ac7` → `4106bbb` | unit/integration/regression/security profile 已建立；portable profile 当前 12/12 PASS |
| 阶段 3A：cgroup v2 生命周期 | Implemented / Environment-gated | `0c5eb50`, `04de3c7`, `d772239` | 代码已实现；当前 SSH 会话缺 cgroup write permission，nsjail 安全测试未验证 |
| 阶段 3B：可信 nsjail verdict | Implemented / Needs delegated verification | `d772239` | 通过 cgroup events、wall clock、output size 和 exit status 分类；需在 delegated service 中跑全量 |
| 阶段 3C：seccomp 接入 | Implemented / Policy needs hardening | `7e69963` | 已接入 Kafel policy；当前策略为 deny-list + `DEFAULT ALLOW`，不是最终产品级 allow-list |
| 阶段 4：CLI / doctor / UX | Planned | — | 仍是位置参数 CLI；缺 `cppjudge doctor` |
| 阶段 5：schema / 题目格式 / 日志格式 | Planned | — | 缺稳定 schema version 和兼容策略 |
| 阶段 6：安装、部署和可移植性 | Planned | `deploy/cppjudge.service.example` | 有 systemd 示例，但缺正式安装、rootfs、SECURITY/TROUBLESHOOTING |
| 阶段 7：可靠性和性能 | Planned | — | 运行目录清理、磁盘限额、worker 模型尚未进入主线 |

## 2026-06-20 验证快照

在 Rocky VM 中执行：

```bash
bash scripts/check_nsjail_env.sh
bash scripts/run_all_tests.sh portable
bash scripts/run_all_tests.sh nsjail
bash scripts/run_all_tests.sh security
```

结果：

| 检查项 | 状态 | 细节 |
|--------|------|------|
| nsjail | PASS | `/usr/local/bin/nsjail` found |
| nsjail cgroup flags | PASS | `--use_cgroupv2`, `--cgroup_mem_max`, `--cgroup_pids_max` 等可用 |
| cgroup v2 | PASS | unified hierarchy detected; memory/pids controllers present |
| cgroup write permission | FAIL / ENV | 当前 SSH 会话无法在 `/sys/fs/cgroup` 创建子 cgroup |
| portable profile | PASS | 12/12 tests passed |
| nsjail profile | NOT_VERIFIED | 5 个 nsjail/security 测试全部 skipped |
| security profile | PARTIAL | builtin security passed；nsjail/seccomp tests skipped |

## 当前 P0 风险

1. **nsjail 安全测试没有在 delegated service 中通过。**
   当前 profile 的 PASS 包含 skipped tests，不能作为产品安全证据。

2. **seccomp 仍是 deny-list。**
   `sandbox/seccomp/cppjudge-runtime.kafel` 使用 `DEFAULT ALLOW`。它能挡住一批高危 syscall，但不是最终产品级最小权限策略。

3. **rootfs 未固定。**
   运行阶段挂载少量动态库，编译阶段只读挂载宿主 `/usr`、`/lib64`、`/lib`、`/bin`。产品级需要版本化 runtime/compile rootfs。

4. **UID/GID 与低权限模型未定型。**
   当前文档有部署方向，但 nsjail 参数和安装流程还没有形成稳定规范。

5. **CLI 与日志 schema 尚未产品化。**
   当前位置参数适合开发，但不适合长期 API；日志缺 schema version、rootfs version、sandbox version 等字段。

## Git 分支快照

GitHub 远端：

| 分支 | 提交 | 判断 |
|------|------|------|
| `master` | `0330f43` | 已包含 Stage 3A/3B/3C 与文档审计更新 |

VM 本地：

| 分支 | 提交 | 判断 |
|------|------|------|
| `master` | `0330f43` | VM 当前工作分支；已与 GitHub master 对齐 |

## 建议下一步

1. 在 delegated systemd 环境中跑通：
   - `bash scripts/run_all_tests.sh nsjail`
   - `bash scripts/run_all_tests.sh security`
2. 进入产品化 P0：fixed rootfs、seccomp allow-list、low-privilege mapping、doctor。
