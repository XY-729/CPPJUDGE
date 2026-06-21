# CPPJUDGE 项目进度

Updated: 2026-06-21

## 当前结论

CPPJUDGE 已进入“具备产品化骨架的单机 C++ 判题内核”阶段。核心判题流程、结构化错误模型、portable 测试、cgroup v2 管理、seccomp 接入、delegated nsjail 验证、P1 用户友好 CLI、P1 diagnostics、日志 schema v1 草案、基础 exit code 语义、Quick Start AC 示例、排错文档和发布检查清单均已进入主线工作区。

当前普通用户入口是：

```bash
./build/cppjudge judge --problem problems/A+B --submission submissions/solution.cpp
```

仓库默认 `submissions/solution.cpp` 是 A+B Accepted 示例；上述命令应返回 `Accepted` 和 exit code 0。

产品级安全路径仍定义为 **nsjail + cgroup v2 + seccomp**；`builtin` 仅用于学习、实验、可信调试和 portable 回归测试。

## 阶段完成状态

| 阶段 | 状态 | 关键 Commit / 证据 | 备注 |
|------|------|--------------------|------|
| 阶段 1：结构化错误模型 | Completed | `ecc46d1`, `43884a7`, `d772239` | CE/RE/SE 结构化边界已建立 |
| 阶段 2：测试基础设施 | Completed | `ac04ac7` → `4106bbb` | unit/integration/regression/security profile 已建立 |
| 阶段 3A：cgroup v2 生命周期 | Completed / Environment-gated | `10d2bdd` | delegated 路径已验证；普通 SSH 会话仍可能缺 cgroup write permission |
| 阶段 3B：可信 nsjail verdict | Completed / Delegated verified | `10d2bdd` | delegated nsjail 9/9 通过 |
| 阶段 3C：seccomp 接入 | Implemented / Policy needs hardening | `7e69963` | 已接入 Kafel policy；仍需产品级 allow-list |
| P1 User-facing CLI | Completed | `tests/cli/test_user_facing_cli.sh` | 新增 `cppjudge judge --problem ... --submission ...` |
| P1 Diagnostics and JSON schema | Completed | `tests/cli/test_version_and_doctor.sh`, `tests/regression/test_judge_log_schema.sh` | `--version`, `doctor`, schema v1 字段和文档已完成 |
| P1 Quick Start and docs | Completed | `tests/cli/test_quickstart_example.sh`, `docs/TROUBLESHOOTING.md`, `docs/RELEASE_CHECKLIST.md` | 默认 sample Accepted；Quick Start 回归已纳入 portable |
| P1 Exit code semantics | Completed baseline | portable tests + delegated security | help/version 0；AC 0；non-AC 1；CLI/doctor not-ready 2；SE 3 |
| P1 Security harness exit-code compatibility | Completed | `scripts/run_security_tests.sh` | delegated security profile 4/4 PASS |
| 阶段 5：题目格式 / 日志格式稳定化 | Partial | `docs/PROBLEM_FORMAT.md`, `docs/JSON_SCHEMA.md` | schema v1 是草案，但已有兼容说明 |
| 阶段 6：安装、部署和可移植性 | Planned | `deploy/cppjudge.service.example`, `docs/RELEASE_CHECKLIST.md` | 缺正式安装、fixed rootfs、SECURITY |
| 阶段 7：可靠性和性能 | Planned | — | 运行目录清理、磁盘限额、worker 模型尚未进入主线 |

## 2026-06-21 验证快照

在 Rocky VM 中执行：

```bash
cmake -S . -B build
cmake --build build -j2
./build/cppjudge --version
./build/cppjudge doctor
./build/cppjudge judge --problem problems/A+B --submission submissions/solution.cpp
python3 -m json.tool build/judge_log.json
./build/cppjudge submissions/solution.cpp problems/A+B 1000 128 1 exact 5000
bash scripts/run_all_tests.sh portable
```

结果：

| 检查项 | 状态 | 细节 |
|--------|------|------|
| build | PASS | `build/cppjudge` 构建成功 |
| version | PASS | `cppjudge 0.1.0-dev (git: 10d2bdd)` |
| doctor | NOT_VERIFIED / PASS | 普通 SSH 会话缺 delegated cgroup write；doctor 正常返回 2 |
| quickstart | PASS | 默认 sample Accepted，exit 0 |
| JSON valid | PASS | `python3 -m json.tool` 通过 |
| old positional CLI | PASS | 默认 sample Accepted，exit 0 |
| portable profile | PASS | 16/16 CTest tests passed |
| cgroup/nsjail/seccomp rewrite | NOT_CHANGED | 本任务未修改主安全路径 |

## 当前风险

1. **seccomp 仍需产品级 allow-list。**
2. **rootfs 未固定。**
3. **schema v1 仍是草案。**
   已有兼容说明，但还未冻结长期 API 承诺。
4. **P1 工作区尚未提交。**
   当前包含多轮 P1 改动和既有 `.claude/rules/sandbox.md` diff。

## 建议下一步

`CPPJUDGE_P1_COMMIT_REVIEW_AND_OPTIONAL_DELEGATED_VERIFY_001`

- review 完整 P1 diff
- 可选运行 delegated nsjail/security profiles
- 在明确授权后再执行 git add / commit / push
