#!/usr/bin/env python3

from __future__ import annotations

import os
import subprocess
from pathlib import Path


MAX_FILE_CHARS = 4000


def run(command: list[str], cwd: Path) -> str:
    try:
        result = subprocess.run(
            command,
            cwd=cwd,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=10,
        )
        return result.stdout.strip()
    except Exception as exc:
        return f"NOT_AVAILABLE: {exc}"


def read_limited(path: Path) -> str:
    if not path.is_file():
        return "FILE_NOT_FOUND"

    try:
        content = path.read_text(encoding="utf-8", errors="replace")
    except Exception as exc:
        return f"READ_FAILED: {exc}"

    if len(content) <= MAX_FILE_CHARS:
        return content

    return (
        content[:MAX_FILE_CHARS]
        + "\n\n[TRUNCATED BY SESSION CONTEXT HOOK]"
    )


def main() -> None:
    project_dir = Path(
        os.environ.get("CLAUDE_PROJECT_DIR", os.getcwd())
    ).resolve()

    print("<cppjudge_session_context>")
    print(f"<project_dir>{project_dir}</project_dir>")
    print(f"<user>{run(['whoami'], project_dir)}</user>")
    print(f"<host>{run(['hostname'], project_dir)}</host>")
    print(f"<system>{run(['uname', '-s'], project_dir)}</system>")
    print(
        "<git_root>"
        + run(["git", "rev-parse", "--show-toplevel"], project_dir)
        + "</git_root>"
    )
    print(
        "<git_branch>"
        + run(["git", "branch", "--show-current"], project_dir)
        + "</git_branch>"
    )
    print(
        "<git_head>"
        + run(["git", "log", "-1", "--oneline"], project_dir)
        + "</git_head>"
    )
    print(
        "<git_status>\n"
        + run(["git", "status", "--short"], project_dir)
        + "\n</git_status>"
    )

    for relative_path, tag in [
        ("CURRENT_TASK.md", "current_task"),
        ("PROGRESS.md", "progress"),
        ("LAST_TASK_REPORT.md", "last_task_report"),
    ]:
        print(f"<{tag}>")
        print(read_limited(project_dir / relative_path))
        print(f"</{tag}>")

    print(
        "<instruction>"
        "Before taking action, obey CURRENT_TASK.md. "
        "Do not repeat completed tasks. "
        "If the current task is completed and no new task exists, stop and "
        "request a new task contract."
        "</instruction>"
    )
    print("</cppjudge_session_context>")


if __name__ == "__main__":
    main()
