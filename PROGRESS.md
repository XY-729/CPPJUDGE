# CPPJUDGE 项目进度

## 阶段完成状态

| 阶段 | 状态 | 关键 Commit | 备注 |
|------|------|------------|------|
| 阶段 1 | In Progress | ecc46d1, 43884a7 | 结构化 CE/RE/SE 主体完成；runner stderr 残留待清理 |
| 阶段 2 | Completed | 2dae1e3 | 测试基础设施, CI, 默认初始化修复 |
| 阶段 3 | Completed (3A/3B/3C) | bdf90e9 | cgroup v2 + nsjail delegated + seccomp 全部集成并通过测试 |
| 阶段 4 | Planned | — | CLI 和用户体验 |
| 阶段 5 | Planned | — | 题目格式和日志格式 |
| 阶段 6 | Planned | — | 安装、发布和可移植性 |
| 阶段 7 | Planned | — | 可靠性和性能 |

### 阶段 1 详情

#### 已完成

- CompileInfo / RunInfo 结构化返回
- CE、RE、SE 的主要责任边界
- fork / exec / 文件系统和沙箱启动错误的结构化传递
- judge.cpp 中基于 stderr 猜测 SE 的逻辑移除
- stderr 伪造不能再导致 SE 的回归测试

#### 尚未完成

- 移除 builtin runner 对 bad_alloc 等 stderr 文本的 MLE 判定（`src/runner.cpp:680-682`）
- 移除 nsjail runner 对 stderr 的 TLE、MLE、OLE 判定（`src/runner.cpp:938-965`）
- 使用墙钟、文件大小、进程状态和 cgroup 内核数据完成可信分类

### 阶段 2 详情

#### 已完成

- unit、integration、regression、security 测试分层
- quick、portable、full、nsjail、security profiles
- AC、WA、CE、RE、TLE、MLE、OLE、SE 自动化覆盖
- MUST_BLOCK 安全攻击测试
- CI 集成
- 测试失败返回非零状态

#### 后续扩展

- 阶段 3 每新增 cgroup、seccomp、rootfs 能力时继续补充攻击与回归测试

### 阶段 3A：cgroup v2 内存与进程控制

首要目标：

- `memory.max` — 硬内存上限
- `memory.peak` — 峰值内存记录
- `memory.events` — OOM 事件可信检测
- `pids.max` — 进程和线程总量限制
- `cgroup.kill` — 超限时清理

阶段 3A 将首先替换 nsjail MLE 的 stderr 文本判断。TLE 和 OLE 的可信判定也应在同一阶段检查并移除文本依赖。

## 全局优先级

```
P0 (决定可靠性):
  1. [DONE] 建立真实构建和测试基线
  2. [DONE] 重构 RunInfo / CompileInfo, 结构化区分 RE 和 SE
  3. [DONE] 建立完整 verdict 回归测试
  4. [DONE] 接入 cgroup v2 内存和进程控制
  5. [DONE] 增加 seccomp
  6. [ ] 明确低权限用户映射
  7. [ ] 固定运行和编译 rootfs
  8. [DONE] 确保生产模式永不回退 builtin

P1 (决定易用性):
  9. [ ] cppjudge doctor
  10. [ ] 清晰的子命令和参数
  11. [ ] 稳定的 problem.json 规范
  12. [ ] 稳定的 judge_log.json 规范
  13. [ ] 友好的错误信息
  14. [DONE] 一条命令构建和测试
  15. [ ] Quick Start 和示例题
  16. [ ] 正式安装流程

P2 (产品成熟后):
  17. [ ] 多 worker 并发
  18. [ ] Special Judge
  19. [ ] 多语言支持
  20. [ ] 静态编译支持
```
