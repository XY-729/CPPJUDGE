# CPPJUDGE 项目进度

## 全局优先级

```
P0 (决定可靠性):
  1. [DONE] 建立真实构建和测试基线
  2. [DONE] 重构 RunInfo / CompileInfo, 结构化区分 RE 和 SE
  3. [ ] 建立完整 verdict 回归测试
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
  14. [ ] 一条命令构建和测试
  15. [ ] Quick Start 和示例题
  16. [ ] 正式安装流程

P2 (产品成熟后):
  17. [ ] 多 worker 并发
  18. [ ] Special Judge
  19. [ ] 多语言支持
  20. [ ] 静态编译支持
  21. [ ] 编译缓存
  22. [ ] 远程任务队列
  23. [ ] Web/API 服务
  24. [ ] 分布式评测
```

---

## 阶段 0: 建立基线 — DONE (2026-06-15)

### 环境
- OS: Rocky Linux 9.7, kernel 5.14.0 x86_64
- GCC 11.5.0, CMake 3.26.5, nsjail at `/usr/local/bin/nsjail`
- Git: `master` @ `c882bc1`, 工作区干净

### 构建
- `cmake -S . -B build-baseline` → 成功, 零 warning
- `cmake --build build-baseline -j2` → 成功
- 二进制: 947KB ELF 64-bit

### 测试基线

| 测试套件 | 结果 | 详情 |
|----------|------|------|
| `run_tests.sh` | **15/15 PASS** | AC/WA/TLE/MLE/OLE/RE/CE + 8个 SE 场景 |
| `run_security_tests.sh` | **4/4 PASS** | open_many_files/fork_many/core_dump/stderr |
| `run_nsjail_tests.sh` | **8/9 PASS** | nsjail_mle 失败: RLIMIT_AS 导致 TLE 而非 MLE |

### 已知问题
- **nsjail MLE**: RLIMIT_AS + SIGKILL 竞争导致误判 TLE, 需 cgroup v2 解决
- **builtin MLE**: VmSize 轮询偶有漂移 (2/3 稳定)
- **动态库列表硬编码**: 仅 5 个库文件, 换发行版可能失效
- **isolate 未实现**: 占位返回 SE
- **无 CI 配置**: 无 `.github/workflows`

---

## 阶段 1: 结构化错误模型 — DONE (2026-06-15)

### 数据结构变更

**runner.h:**
```cpp
enum class RunResult { OK, TLE, MLE, OLE, RE, SE };  // +SE

struct RunInfo {
    RunResult result;
    int time_ms;
    int memory_mb;
    bool system_error = false;     // NEW
    std::string error_message;     // NEW
    int exit_code = -1;            // NEW
    int signal = -1;               // NEW
};
```

**compiler.h:**
```cpp
enum class CompileResult { OK, CE, SE };

struct CompileInfo {
    CompileResult result;
    bool system_error = false;
    std::string error_message;
};

CompileInfo compile_cpp_structured(...);  // NEW 主入口
// bool compile_cpp(...) 保留为兼容包装
```

### 修改文件

| 文件 | 行变化 |
|------|--------|
| `src/runner.h` | +8 |
| `src/runner.cpp` | +380 / -406 |
| `src/compiler.h` | +20 |
| `src/compiler.cpp` | +250 / -248 |
| `src/judge.h` | +4 / -1 |
| `src/judge.cpp` | +221 / -426 |

### 关键变更
1. runner 直接返回 `RunResult::SE` — 不再伪装成 RE 然后由 judge 猜测
2. 使用 `pipe2(O_CLOEXEC)` 自管道技术检测 exec 失败 — 零字符串匹配
3. `judge.cpp` 删除 `is_sandbox_system_error()` — 不再依赖 stderr 固定字符串
4. `judge_log.json` 新增 `exit_code`, `signal`, `system_error` 字段
5. 编译基础设施失败返回 SE 而非 CE
6. 旧 `compile_cpp()` 留为兼容包装

### 新增测试
- **用户 stderr 不会误判 SE**: 用户打印 "Failed to execute nsjail" 等字符串仍得到 AC
- **g++ exec 失败 → SE**: PATH 无 g++ 时编译基础设施失败正确返回 SE
- **nsjail exec 失败 → SE**: 恶意解释器脚本导致 nsjail exec 失败 → SE
- **isolate → SE**: runner 直接返回 SE (非 RE)

### 遗留问题
- nsjail MLE 仍依赖 RLIMIT_AS + stderr 启发式
- builtin MLE 偶有 VmSize 轮询竞争
- 编译超时仍映射为 CE (未引入 Compile TLE)
- exit_code 127 在极端情况下可能被用户程序返回 (pipe 技术已规避)

---

## 阶段 2: 自动化测试体系 — 待开始

目标是建立四层测试:
1. 单元测试 (纯逻辑: 字符串转换/配置验证/浮点比较)
2. 集成测试 (真实编译运行: AC/WA/TLE/MLE/OLE/RE/CE/SE)
3. 沙箱安全测试 (fs/proc/network/fork/shell/符号链接/env/fd)
4. 回归测试 (每个修过的 bug 一条测试)

验收标准:
- 一条命令运行全部测试
- 测试失败返回非零退出码
- 每个 verdict 至少一个稳定测试
- 关键安全能力至少一个攻击测试

---

## 阶段 3: nsjail 产品级加固 — 待开始

3.1 cgroup v2 — memory.max/memory.peak/memory.events/pids.max
3.2 seccomp — 禁止 mount/umount/ptrace/bpf/reboot/namespace 再创建
3.3 低权限身份 — UID/GID/no_new_privs/capabilities
3.4 最小 rootfs — 受控动态库/依赖扫描
3.5 编译沙箱独立加固 — 独立 rootfs

---

## 阶段 4-7: 待开始

- 阶段 4: CLI 和用户体验 (doctor/validate/init-problem)
- 阶段 5: 稳定题目格式和日志格式 (schema_version)
- 阶段 6: 安装、发布和可移植性 (cmake --install/RPM)
- 阶段 7: 可靠性和性能 (worker 进程/优雅退出)
