---
paths:
  - "**/*.md"
  - ".claude/**/*"
---

# CPPJUDGE Documentation Rules

- `CLAUDE.md` 只保存稳定规则。
- `CURRENT_TASK.md` 只保存当前任务合同。
- `PROGRESS.md` 只保存当前快照。
- `LAST_TASK_REPORT.md` 只保存最近一次任务报告。
 - `docs/OVERVIEW.md` 保存从当前状态出发的项目大纲和长期架构背景。
- 不把计划写成已完成。
- 不把 NOT_VERIFIED 写成 PASS。
- 动态时间使用实际命令结果。
- 无法确认的信息写 `NOT_CONFIRMED`。
- 每次任务完成、部分完成或阻塞都必须更新报告。
- 旧报告归档后不得在启动上下文中自动导入。
- 删除远端分支、合并主线、推送到 GitHub 前必须记录明确用户确认。
- nsjail/security 测试 skipped 时不得写成 PASS。
