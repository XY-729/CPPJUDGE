---
paths:
  - "**/*sandbox*"
  - "**/runner.*"
  - "**/compiler.*"
  - "scripts/**/*nsjail*"
  - "tests/**/*nsjail*"
  - "tests/**/*security*"
---

# CPPJUDGE Sandbox Rules

## 安全评价标准

- **产品级安全路径** = nsjail + cgroup v2 + seccomp，三者缺一不可。
- **builtin** = 学习 / 实验 / 本地调试用，不纳入产品安全评价。
- 安全审计只针对产品级路径；builtin 的 verdict 正确性仍需保证，但不作为安全边界。
- nsjail / cgroup / seccomp 任一缺失时，该次运行标记为 NOT_VERIFIED 或 SE，不得降级为 PASS。

## 具体规则

- builtin 不是强安全沙箱。
- nsjail 编译与运行隔离必须分别确认。
- nsjail 不存在不属于用户 RE。
- namespace、mount、chroot、cgroup 和权限错误不属于用户 RE。
- 用户程序未成功启动时不得生成普通 RE。
- isolate 未经测试证明前视为未实现或不支持。
- 超时或超限时清理整个用户进程组。
- 不通过取消安全限制使测试通过。
- 不默认开放网络。
- 不默认继承完整环境变量。
- 不默认继承无关文件描述符。
- stderr 仅作诊断，不作为长期主要错误接口。
- 修改沙箱后必须运行 builtin 回归。
- 无法运行 nsjail 时标记 NOT_VERIFIED。
