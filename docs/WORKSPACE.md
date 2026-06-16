# CPPJUDGE Workspace Policy

ACTIVE_MODE: REMOTE_NATIVE

EXPECTED_OS: Linux

EXPECTED_REMOTE_USER: xiyuan729

AUTHORITATIVE_SOURCE: REMOTE_GIT_REPOSITORY

LOCAL_MIRROR_STATE: PRESERVED_READ_ONLY

## 1. REMOTE_NATIVE

默认模式。

Claude Code 必须：

- 运行在虚拟机 Linux 中；
- 从远程 CPPJUDGE Git 仓库内启动；
- 直接读取和修改虚拟机文件；
- 在虚拟机执行构建和测试；
- 使用远程 Git 工作区记录修改。

启动后必须确认：

```bash
whoami
hostname
uname -s
pwd
git rev-parse --show-toplevel
git status --short
```

预期：

```text
whoami = xiyuan729
uname -s = Linux
```

Git 根目录不能位于：

```text
/mnt/c/
/mnt/d/
/c/
C:\
```

## 2. Windows 本地副本

Windows 中已有：

```text
claude Worker/
cppjudge_modified/
test_*.cpp
test_*.sh
```

这些文件暂时：

* 保留；
* 不删除；
* 不继续修改；
* 不用于构建结论；
* 不覆盖远程；
* 不视为权威实现。

保留原因是尚不能排除其中存在唯一修改。

## 3. 禁止的远程工作流

REMOTE_NATIVE 模式下禁止：

* scp 源码到本地修改；
* rsync 仓库到本地修改；
* 创建新的 `cppjudge_modified`；
* 在仓库父目录创建测试；
* 手工复制单个源码文件往返同步；
* SSH 失败后改为本地执行并声称远程完成；
* 将本地测试结果视为 nsjail 或 Linux 安全测试结果。

## 4. 可选 LOCAL_MIRROR 模式

只有用户明确授权后才能切换。

切换时必须同时满足：

1. 修改本文件 `ACTIVE_MODE`；
2. `CURRENT_TASK.md` 写明：
   `LOCAL_MIRROR_AUTHORIZATION: ALLOWED`；
3. 本地目录必须是完整 Git clone；
4. 不允许零散源码副本；
5. 以 Git commit 哈希作为同步单位；
6. 不使用手工复制覆盖；
7. 远程测试必须 checkout 同一 commit；
8. nsjail、安全、namespace、cgroup 测试仍在虚拟机执行；
9. 最终报告同时记录本地和远程 commit；
10. 未完成远程验证时结果标记为 NOT_VERIFIED。

## 5. 权威性原则

REMOTE_NATIVE 模式：

```text
远程 Git 工作区及其提交历史为权威来源。
```

LOCAL_MIRROR 模式：

```text
双方确认一致的 Git commit 哈希为权威来源。
```

永远不能以"文件看起来更新"作为权威判断。

## 6. 模式切换

Claude Code 不得自行切换工作模式。

模式切换必须由用户明确授权，并在：

* `docs/WORKSPACE.md`
* `CURRENT_TASK.md`
* `PROGRESS.md`
* `LAST_TASK_REPORT.md`

中同时记录。
