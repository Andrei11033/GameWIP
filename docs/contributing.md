# GameWIP Contributor Workflow

## Purpose

This file defines the normal repository workflow for issues, branches, pull requests, validation notes, and merge messages.

Use this file for day-to-day GitHub process. Use `docs/decisions.md` for stable architecture, tooling, naming, and commit-message decisions.

---

## Default Branch

The default branch is currently:

```text
master
```

Keep `master` readable and releasable. Normal feature work should happen on a short-lived branch and merge through a pull request.

---

## Issues

Create an issue for work that is not a tiny local cleanup.

Use issue titles in the same compact style as commit titles:

```text
type: imperative summary
```

Examples:

```text
feature: add filesystem directory watcher
bug: fix metadata copy on read-only files
task: document GitHub merge workflow
docs: add FileSystem troubleshooting example
build: add default-branch validation workflow
```

Use labels consistently:

```text
area:*       The main affected system.
type:*       The kind of work.
priority:*   The scheduling priority.
```

Prefer one main `area:*` label and one main `type:*` label. Add extra labels only when they clarify ownership or review.

Assign an issue when work starts. Project automation then moves it to `In Progress`; future ideas remain in Backlog, while fully triaged work in the active milestone becomes Ready.

Use GitHub's **Blocked by** relationship for real dependencies. Do not use the Blocked status as a general priority or waiting label.

---

## Branches

Branch names should describe the work without encoding implementation history.

Preferred format:

```text
area/short-summary
```

Examples:

```text
filesystem/directory-watcher
github/workflow-standards
docs/filesystem-troubleshooting
build/main-validation
```

Keep branches focused. If the branch starts solving unrelated problems, split the extra work into a new issue and branch.

---

## Pull Requests

Open a pull request before merging into `master`.

The pull request title should normally be the squash commit title:

```text
area: imperative summary
```

The pull request body should include:

* what changed;
* linked issue numbers when applicable;
* the validation commands or inspections performed;
* checklist items from the template;
* the intended squash merge message for non-trivial changes.

Use `Draft` only while the PR is not ready for final review or merge.

---

## Required Pull Request Metadata

Ready-for-review pull requests must pass the `PR Standards` workflow.

That workflow enforces:

* title format: `area: imperative summary`;
* required PR body sections from the template;
* non-empty summary and validation notes;
* a linked issue such as `Closes #123`, or an explicit `No linked issue: reason`;
* a concrete merge-message title;
* at least one `area:*`, one `type:*`, and one `priority:*` label.

Draft pull requests may be incomplete while work is still moving.

Dependabot pull requests are exempt from the human metadata check, but they still run the normal validation workflow.

---

## Validation Notes

Validation notes should be concrete enough that a future maintainer understands what was actually proven.

Good examples:

```text
- `ctest --preset validation` passed all modular correctness-test entries.
- `GameWIPBenchmarks.exe --benchmark_dry_run` passed.
- Doxygen docs built with `GAMEWIP_BUILD_DOCS=ON`; warning log was empty.
- Inspected the generated FileSystem public API page.
```

Weak examples:

```text
- Tested.
- Looks good.
- Built it.
```

If validation is not run, say so directly and explain why.

---

## Automated Validation

The `Validation` workflow runs on pull requests into `master`, pushes to `master`, and manual dispatch.

It performs:

* an MSYS2 UCRT64 configure/build pass with modular correctness tests and internal test hooks enabled;
* a Google Benchmark registration dry run without performance thresholds;
* a Doxygen docs build with `GAMEWIP_BUILD_DOCS=ON`;
* a Doxygen warning-log check;
* clang-tidy and clang-format checks for maintained C++ code;
* JavaScript syntax and unit tests, JSON parsing, and actionlint workflow validation.

The `Doxygen Docs` workflow publishes GitHub Pages only from `master` or manual dispatch. Pull requests build docs for validation but do not publish them.

Branch protection for `master` must require:

```text
PR Standards / Check PR Standards
Validation / Build and Test
Validation / Repository Checks
Validation / Docs Check
```

Local static-analysis commands and file scope are documented under `docs/doxygen/static_analysis.md`.

---

## Project Automation

Use a closing keyword such as `Closes #6` in the pull request body. The project workflow uses that relationship to copy issue labels, assignees, and a non-conflicting milestone to the pull request, then moves both items through In Progress, Review, and Done.

Issue status is derived from closure, **Blocked by** relationships, linked pull requests, assignees, active milestone, and required labels. The complete rule order, repository variables, token requirement, and dry-run command are documented under `docs/doxygen/repository_automation.md`.

---

## Merge Style

Prefer `Squash and merge` for feature branches so `master` keeps one clean changelog-style commit per completed piece of work.

Use `Rebase and merge` only when the individual commits are already meaningful and worth preserving.

Avoid ordinary merge commits on `master` unless there is a deliberate reason to preserve branch structure.

After a PR is merged, delete the feature branch unless more work will continue on it immediately.

---

## Squash Commit Messages

Squash commit messages must follow the standard in `docs/decisions.md`:

```text
area: imperative summary
```

Use patch-note sections when they apply:

```text
Added:
- Added new APIs, modules, targets, tests, docs, or assets.

Changed:
- Changed existing behavior, structure, wiring, or documentation.

Fixed:
- Fixed bugs, incorrect behavior, edge cases, or broken workflows.

Build:
- Changed CMake, toolchain, package, install, editor, or dependency setup.

Tests:
- Added or changed test coverage, test support, validation, or checklists.

Documentation:
- Added or changed docs, examples, public API notes, or developer guidance.
```

Do not keep GitHub's default `(#123)` suffix if the project history should read like a standalone changelog. Mention the PR or issue in the body instead when it matters.

---

## Local Sync After Merge

After a PR merges into `master`:

```powershell
git switch master
git pull --ff-only origin master
git branch -d branch/name
```

If GitHub deleted the remote branch, prune stale remote-tracking names:

```powershell
git fetch origin --prune
```
