#!/usr/bin/env python3

from __future__ import annotations

import getpass
import json
import os
import platform
import re
import sys
from pathlib import Path


def deny(message: str) -> None:
    print(f"BLOCKED BY CPPJUDGE WORKSPACE GUARD: {message}", file=sys.stderr)
    raise SystemExit(2)


def parse_key_value_file(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}

    if not path.is_file():
        return values

    for raw_line in path.read_text(
        encoding="utf-8", errors="replace"
    ).splitlines():
        line = raw_line.strip()

        if not line or line.startswith("#") or ":" not in line:
            continue

        key, value = line.split(":", 1)
        key = key.strip()
        value = value.strip()

        if re.fullmatch(r"[A-Z][A-Z0-9_]*", key):
            values[key] = value

    return values


def is_inside(child: Path, parent: Path) -> bool:
    try:
        child.relative_to(parent)
        return True
    except ValueError:
        return False


def resolve_target(raw_path: str, cwd: Path) -> Path:
    path = Path(raw_path).expanduser()

    if not path.is_absolute():
        path = cwd / path

    return path.resolve(strict=False)


def main() -> None:
    try:
        payload = json.load(sys.stdin)
    except Exception as exc:
        deny(f"invalid hook input: {exc}")

    project_dir = Path(
        os.environ.get("CLAUDE_PROJECT_DIR", os.getcwd())
    ).resolve()

    cwd = Path(payload.get("cwd") or project_dir).resolve()

    if not is_inside(cwd, project_dir):
        deny(
            f"tool cwd is outside project root: "
            f"cwd={cwd}, project={project_dir}"
        )

    workspace = parse_key_value_file(
        project_dir / "docs" / "WORKSPACE.md"
    )
    task = parse_key_value_file(project_dir / "CURRENT_TASK.md")

    mode = workspace.get("ACTIVE_MODE", "REMOTE_NATIVE")
    expected_os = workspace.get("EXPECTED_OS", "Linux")
    expected_user = workspace.get(
        "EXPECTED_REMOTE_USER", "xiyuan729"
    )

    if mode == "REMOTE_NATIVE":
        if platform.system() != expected_os:
            deny(
                f"REMOTE_NATIVE requires OS {expected_os}; "
                f"actual OS is {platform.system()}"
            )

        actual_user = getpass.getuser()

        if actual_user != expected_user:
            deny(
                f"REMOTE_NATIVE requires user {expected_user}; "
                f"actual user is {actual_user}"
            )

    elif mode == "LOCAL_MIRROR":
        if task.get("LOCAL_MIRROR_AUTHORIZATION") != "ALLOWED":
            deny(
                "LOCAL_MIRROR mode is active but current task "
                "does not authorize it"
            )

    else:
        deny(f"unknown ACTIVE_MODE: {mode}")

    tool_name = str(payload.get("tool_name", ""))
    tool_input = payload.get("tool_input") or {}

    if tool_name in {"Write", "Edit", "NotebookEdit"}:
        raw_path = (
            tool_input.get("file_path")
            or tool_input.get("notebook_path")
            or ""
        )

        if not raw_path:
            deny(f"{tool_name} has no target path")

        target = resolve_target(str(raw_path), cwd)

        if not is_inside(target, project_dir):
            deny(f"file write outside Git project: {target}")

        lowered = str(target).lower()

        if mode == "REMOTE_NATIVE":
            forbidden_fragments = (
                "/mnt/c/",
                "/mnt/d/",
                "\\desktop\\",
                "cppjudge_modified",
            )

            if any(fragment in lowered for fragment in forbidden_fragments):
                deny(f"remote mode cannot edit local mirror path: {target}")

    if tool_name == "Bash":
        command = str(tool_input.get("command", ""))

        if mode == "REMOTE_NATIVE":
            cross_machine_patterns = [
                r"(^|[;&|]\s*)scp(\s|$)",
                r"(^|[;&|]\s*)rsync(\s|$)",
                r"/mnt/c/",
                r"/mnt/d/",
                r"cppjudge_modified",
                r"[A-Za-z]:\\",
            ]

            for pattern in cross_machine_patterns:
                if re.search(pattern, command, flags=re.IGNORECASE):
                    deny(
                        "REMOTE_NATIVE forbids local-copy or "
                        f"cross-machine command: {command}"
                    )

        git_write_patterns = [
            r"\bgit\s+add\b",
            r"\bgit\s+commit\b",
            r"\bgit\s+push\b",
            r"\bgit\s+pull\b",
            r"\bgit\s+merge\b",
            r"\bgit\s+rebase\b",
            r"\bgit\s+reset\b",
            r"\bgit\s+clean\b",
            r"\bgit\s+checkout\b",
            r"\bgit\s+switch\b",
            r"\bgit\s+restore\b",
        ]

        if task.get("GIT_WRITE_AUTHORIZATION") != "ALLOWED":
            for pattern in git_write_patterns:
                if re.search(pattern, command):
                    deny(
                        "current task does not authorize Git write "
                        f"operations: {command}"
                    )

    raise SystemExit(0)


if __name__ == "__main__":
    main()
