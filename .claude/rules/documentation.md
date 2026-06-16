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
- `PROJECT_OVERVIEW.md` 只保存长期架构背景。
- 不把计划写成已完成。
- 不把 NOT_VERIFIED 写成 PASS。
- 动态时间使用实际命令结果。
- 无法确认的信息写 `NOT_CONFIRMED`。
- 每次任务完成、部分完成或阻塞都必须更新报告。
- 旧报告归档后不得在启动上下文中自动导入。
