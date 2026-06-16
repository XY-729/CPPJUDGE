---
name: finalize-task
description: Finalize any CPPJUDGE task by verifying the Git state, updating CURRENT_TASK.md and PROGRESS.md, writing LAST_TASK_REPORT.md, and returning the same structured report. Use whenever a task is completed, partially completed, paused, or blocked.
---

# Finalize CPPJUDGE Task

Do not begin new implementation work.

## Live repository evidence

### Current directory

!`pwd`

### Git root

!`git rev-parse --show-toplevel`

### Branch

!`git branch --show-current`

### HEAD

!`git log -1 --oneline --decorate`

### Working tree

!`git status --short`

### Diff summary

!`git diff --stat`

### Changed files

!`git diff --name-only`

## Instructions

1. Read:
   - `CURRENT_TASK.md`
   - `PROGRESS.md`
   - `docs/WORKSPACE.md`
   - the current conversation's actual commands and outputs.

2. Determine one status:
   - `COMPLETED`
   - `PARTIAL`
   - `BLOCKED`
   - `FAILED`

3. Do not claim a command or test ran unless the current session contains evidence.

4. Update `CURRENT_TASK.md`:
   - set the final status;
   - preserve task ID;
   - record unmet acceptance criteria;
   - do not create the next implementation task.

5. Update `PROGRESS.md`:
   - preserve only the current project snapshot;
   - replace stale status;
   - record confirmed current state;
   - do not append a long chronological transcript.

6. Before replacing `LAST_TASK_REPORT.md`, archive the previous report only when:
   - it has a different non-empty `TASK_ID`;
   - it represents an older completed, partial, blocked, or failed task.

   Archive location:

   ```text
   docs/task-reports/<TASK_ID>.md
   ```

7. Completely rewrite `LAST_TASK_REPORT.md` using the schema below.

8. Run:

   ```bash
   git status --short
   git diff --stat
   git diff --name-only
   ```

9. Return the same report in chat.

## Required LAST_TASK_REPORT.md schema

```markdown
# CPPJUDGE Last Task Report

REPORT_VERSION: 1

TASK_ID:

STATUS:

FINISHED_AT:

WORKSPACE_MODE:

REMOTE_USER:

REMOTE_HOST:

GIT_ROOT:

BRANCH:

HEAD_COMMIT:

## 1. Task objective

## 2. Scope actually performed

## 3. Files read

## 4. Files created

## 5. Files modified

## 6. Files deleted

## 7. Implementation summary

## 8. Commands executed

For every important command include:

- command;
- exit code;
- relevant output;
- reason for running it.

## 9. Build results

Use only:

- PASS
- FAIL
- NOT_VERIFIED
- NOT_APPLICABLE

## 10. Test results

For every test group include:

- status;
- exact command;
- evidence;
- covered acceptance criterion.

## 11. Acceptance criteria

For every criterion from `CURRENT_TASK.md` include:

- PASS;
- FAIL;
- NOT_VERIFIED;
- evidence.

## 12. Unexpected changes

## 13. Remaining risks

## 14. Blockers

## 15. Documentation state

- CURRENT_TASK.md:
- PROGRESS.md:
- PROJECT_OVERVIEW.md:
- LAST_TASK_REPORT.md:

## 16. Git state after task

- git status:
- diff stat:
- untracked files:
- staged files:
- commit created:
- push performed:

## 17. Exact next-state summary

Describe what is now true in the repository.

Do not describe plans as completed facts.

## 18. Recommended next task

Provide only:

- objective;
- prerequisites;
- allowed scope;
- prohibited scope;
- acceptance criteria;
- tests likely required.

Do not execute the next task.

## 19. GPT prompt-generation input

Provide a compact block containing:

```text
CURRENT_STATE:
COMPLETED:
PARTIAL:
FAILED:
NOT_VERIFIED:
CHANGED_FILES:
UNCOMMITTED_STATE:
RISKS:
BLOCKERS:
NEXT_OBJECTIVE:
NEXT_ALLOWED_SCOPE:
NEXT_PROHIBITED_SCOPE:
NEXT_ACCEPTANCE_CRITERIA:
REQUIRED_TESTS:
```

## Final response

Return the complete report.

Do not reply only with a summary.

Do not start another task after finalization.
