# CPPJUDGE Testing Contract

## 1. 结果状态

只能使用：

### PASS

命令实际执行，并且退出状态和行为符合预期。

### FAIL

命令实际执行，但代码或测试行为与预期不一致。

### NOT_VERIFIED

命令没有获得有效结果，例如：

- 环境不满足；
- 依赖缺失；
- 权限不足；
- nsjail 不存在；
- namespace 或 cgroup 不可用；
- SSH 或远程系统异常；
- 测试未执行。

## 2. 证据要求

每组测试记录：

- 命令；
- 工作目录；
- 环境；
- 退出码；
- 关键输出；
- 对应验收条件；
- PASS、FAIL 或 NOT_VERIFIED。

只写"测试通过"不构成有效报告。

## 3. 测试层次

### 单元测试

覆盖：

- 配置解析；
- sandbox 类型；
- verdict 字符串；
- comparer；
- 结构化错误转换；
- 纯函数和单位换算。

### 集成测试

覆盖：

- AC；
- WA；
- CE；
- RE；
- TLE；
- MLE；
- OLE；
- Judge/System Error；
- 日志；
- 多测试点流程。

### 沙箱测试

分别检查 builtin 和 nsjail：

- 子进程清理；
- 文件访问；
- 网络访问；
- 进程数量；
- 输出大小；
- 环境变量；
- 文件描述符；
- 沙箱启动错误。

### 回归测试

永久保留：

- MLE 不得误判为 RE；
- 普通 RE 不得误判为 MLE；
- nsjail 启动错误不得判为用户 RE；
- 超时不得残留用户子进程；
- OLE 不得退化为普通 RE。

## 4. 安全测试约束

以下测试可能耗尽资源：

- fork bomb；
- 无限线程；
- 无限输出；
- 大量 socket；
- 大量文件；
- 长时间运行程序。

执行以前必须确认测试本身具备：

- 进程数上限；
- 超时；
- 输出上限；
- 清理逻辑；
- 独立测试虚拟机；
- 可恢复方案。

没有这些保护时不得运行攻击性测试。

## 5. 修改对应测试

修改 runner：

- RE；
- TLE；
- MLE；
- OLE；
- 进程组清理。

修改 compiler：

- 编译成功；
- CE；
- 编译超时；
- 编译器启动失败。

修改 comparer：

- AC；
- WA；
- exact；
- 浮点比较。

修改 sandbox 或 nsjail：

- builtin 回归；
- nsjail 环境检查；
- 启动错误；
- namespace；
- mount；
- cgroup；
- 网络隔离。

修改错误模型：

- 所有 verdict；
- 退出码；
- 终止信号；
- 日志兼容；
- RE/MLE；
- 沙箱错误分类。

## 6. 当前任务例外

阶段 2-B 不修改项目代码。

本次只验证：

- hooks Python 语法；
- settings JSON；
- hook 模拟输入；
- 文档引用；
- Git diff 范围。

本次不运行 CPPJUDGE 业务测试。
