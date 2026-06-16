# CPPJUDGE 项目进度

## 阶段完成状态

| 阶段 | 状态 | 关键 Commit | 备注 |
|------|------|------------|------|
| 阶段 1 | COMPLETED | ecc46d1, 43884a7 | 结构化错误处理, runner/compiler SE 分离 |
| 阶段 2 | COMPLETED | 2dae1e3 | 测试基础设施, CI, 默认初始化修复 |
| 阶段 3 | NOT_STARTED | — | 待开始 |

**阶段 2 总结**:
- 修复 commit: `2dae1e3`
- CI run: `27618081392`
- portable profile: 11/11 PASS
- 当前状态: READY_FOR_STAGE_3

## 全局优先级

```
P0 (决定可靠性):
  1. [DONE] 建立真实构建和测试基线
  2. [DONE] 重构 RunInfo / CompileInfo, 结构化区分 RE 和 SE
  3. [DONE] 建立完整 verdict 回归测试
  4. [ ] 接入 cgroup v2 内存和进程控制
  5. [ ] 增加 seccomp
  6. [ ] 明确低权限用户映射
  7. [ ] 固定运行和编译 rootfs
  8. [ ] 确保生产模式永不回退 builtin

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
