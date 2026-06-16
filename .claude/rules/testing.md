---
paths:
  - "tests/**/*"
  - "scripts/**/*"
  - "**/test_*.cpp"
  - "**/test_*.cc"
  - "**/test_*.sh"
  - "**/*_test.cpp"
---

# CPPJUDGE Test Rules

- 测试必须位于权威 Git 仓库内。
- 不在仓库父目录创建散落测试。
- 不删除失败测试。
- 不修改预期结果掩盖实现错误。
- 每个 bug 修复应保留回归测试。
- 结果只使用 PASS、FAIL、NOT_VERIFIED。
- 未执行测试不能写成 PASS。
- 环境问题和代码失败分开报告。
- 测试脚本失败时必须返回非零状态。
- 测试清理自己创建的文件和进程。
- 安全测试检查残留进程。
- MLE 测试同时检查普通 RE。
- nsjail 测试前先检查环境。
