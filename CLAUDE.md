# CLAUDE.md

开始任何任务前必须读取并遵守 .claude/EXECUTION_PROTOCOL.md；一旦触发停止条件，必须立即输出中文 BLOCKED 报告并停止。

CPPJUDGE 是一个轻量级 C++ 判题内核，目标演变成适合学校或实验室可信环境使用的判题后端。

## 远程开发环境

本项目通过用户本机配置的 SSH alias 连接远程 Linux 虚拟机进行开发。
不要在仓库中写死 IP 地址、用户名或私钥路径。

典型 SSH 配置示例:

```
Host cppjudge-vm
    HostName <vm-ip>
    User <username>
    IdentityFile ~/.ssh/<your-key>
```

所有代码读取、修改、构建和测试在远程虚拟机中执行:

```bash
ssh cppjudge-vm 'cd /home/<user>/cppjudge && <command>'
```

传输文件:

```bash
scp <local> cppjudge-vm:/home/<user>/cppjudge/<path>
```

## 构建

```bash
cmake -S . -B build
cmake --build build -j2
```

生成的二进制: `build/cppjudge` (动态链接 ELF 64-bit)

## 测试

```bash
bash scripts/run_tests.sh                     # 默认回归测试
bash scripts/run_security_tests.sh            # builtin 安全测试
bash scripts/run_nsjail_tests.sh              # nsjail MVP 测试 (需 nsjail)
bash scripts/run_structured_error_tests.sh    # 结构化错误回归测试
bash scripts/check_nsjail_env.sh              # nsjail/cgroup 环境诊断
bash scripts/preview_nsjail_cgroup_args.sh    # cgroup 参数预览
```

## 使用方法

```bash
./build/cppjudge [submission] [problem_dir] [time_limit_ms] [memory_limit_mb] [output_limit_mb] [compare_mode] [compile_time_limit_ms]
```

示例:
```bash
./build/cppjudge submissions/tests/ac.cpp problems/A+B 1000 128 1 floating 5000
```

## 项目结构

```
src/
├── main.cpp                     # 入口
├── config.h                     # 默认常量
├── judge.h / judge.cpp          # 核心编排: 解析→编译→运行→比较→JSON日志
├── compiler.h / compiler.cpp    # 编译模块 (g++ / nsjail+g++)
├── runner.h / runner.cpp        # 运行模块 (builtin/nsjail/isolate)
└── comparer.h / comparer.cpp    # 输出比较 (exact/floating)
include/
└── json.hpp                     # nlohmann/json v3.12.0
problems/A+B/                    # 默认示例题
submissions/tests/               # 测试用例 (ac/wa/tle/mle/ole/re/ce)
submissions/tests/security/      # 安全测试用例
scripts/                         # 测试和环境诊断脚本
docs/                           # 项目文档 (WORKSPACE, TESTING, nsjail-plan, security-test-matrix)
```

## 评测调用链

```
main() → judge(argc, argv)
  ├─ load_problem_config() → ProblemConfig
  ├─ validate_sandbox_for_environment()
  ├─ compile_cpp_structured() → CompileInfo {OK, CE, SE}
  ├─ [每个测试点]:
  │   ├─ run_program() → RunInfo {OK,TLE,MLE,OLE,RE,SE + exit_code,signal}
  │   ├─ compare_output() → bool
  │   └─ 记录 case_json
  └─ write_log_file() → build/judge_log.json
```

## 错误模型 (Phase 1 后)

**RunResult**: `OK | TLE | MLE | OLE | RE | SE`
**CompileResult**: `OK | CE | SE`

系统错误 (SE) 直接由 runner/compiler 层返回，`judge.cpp` 不依赖 stderr 字符串匹配区分 RE/SE。

## 开发约束

 - 不执行 `sudo`、`git reset --hard`、`git push --force`
- 不安装/卸载系统软件
- 不修改系统网络、mount、namespace 或 cgroup 配置
- 修改前必须理解声明、实现、调用方和测试
- 所有修改后必须: 构建 → 运行原有测试 → 运行相关新测试 → git diff
 - 默认不自动 commit、不 push；只有用户明确要求仓库维护、发布、合并或分支清理时，才在确认的范围内执行
 - 删除远端分支、合并到主线、推送到 GitHub 前必须确认目标分支和待删除分支清单
- 保持 C++17，构建零 warning
- 保持现有 CLI、problem.json 和 verdict 文本兼容

## 当前项目状态提示

- 当前最新开发状态在 VM 的 `stage3d-rootfs`，HEAD 为 `7e69963`。
- GitHub `master` 仍落后该分支；`stage3d-rootfs` 尚未作为 GitHub 分支存在。
- `docs/OVERVIEW.md` 是从 2026-06-20 起的项目大纲。
- `docs/product-readiness-audit.md` 记录距离产品级判题器的主要差距。
- nsjail/cgroup/seccomp 代码已接入，但当前普通 SSH 会话缺少 cgroup delegation，nsjail/security/seccomp 测试可能 skipped；不要把 skipped 当成 PASS。
