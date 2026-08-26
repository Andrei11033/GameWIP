@page project_contributing Contributor workflow

This is the day-to-day path for contributing a change: choose or create an
issue, work on a focused branch, show what you validated, and merge through a
reviewed pull request. The surrounding automation exists to keep that path
predictable, not to replace human judgment.

For library extension requirements, use @ref project_extending. Durable
technical choices belong in @ref project_decisions, and release-number rules
belong in @ref project_versioning.

## The contribution path

Most changes follow this sequence:

1. Find or open an issue that explains the outcome.
2. Assign it when work begins and create a short-lived branch.
3. Make the focused change and run the checks appropriate to it.
4. Open a pull request that explains the result and records concrete evidence.
5. Resolve review comments and required checks.
6. Squash-merge the finished work, then remove the branch.

The sections below define each step and the exceptions.

## Protect the default branch

The default branch is:

```text
master
```

Keep `master` readable and releasable. Normal feature work should happen on a short-lived branch and merge through a pull request.

## Describe the work with an issue

Create an issue for work that is not a tiny local cleanup.

GitHub issues are the active task tracker for implementation work, validation work, bugs, and follow-up cleanup. The roadmap defines milestone
completion criteria; issues define the work items used to satisfy those criteria.

Issue titles use a work-type prefix:

```text
bug: ...
feature: ...
task: ...
decision: ...
release: ...
```

Examples:

```text
feature: add filesystem directory watcher
bug: fix metadata copy on read-only files
task: document GitHub merge workflow
decision: choose the R02 scalar policy
release: publish R01
```

Use labels consistently:

```text
type:*       The work kind.
area:*       The primary affected area.
priority:*   The scheduling priority.
compat:breaking   An optional intentional compatibility-contract change.
```

Every normal issue has exactly one `type:*`, one `area:*`, and one
`priority:*` label. `compat:breaking` is optional when the work intentionally
changes a public API, package, save, network, configuration, content, or other
compatibility contract. Do not add another metadata dimension for status,
phase, release, blockers, platform, size, or risk.

A GitHub milestone means the issue is deliberately targeted to that concrete
release. A capability slice does not automatically receive a milestone, and a
useful future issue may remain in Backlog without one.

Assign an issue when work starts. Project automation then moves it to `In Progress`; future ideas remain in Backlog, while fully triaged work in the
active milestone becomes Ready.

Use GitHub's **Blocked by** relationship for hard dependencies. Describe
preferred sequencing that is not a hard blocker in the issue or roadmap. Do
not use a label to represent blocker state.

## Keep the branch focused

Issue-backed branch names should connect the focused work to its tracking
issue.

Preferred format:

```text
<area>/<issue-number>-<short-summary>
```

Examples:

```text
tools/72-wsl-linux-validation
roadmap/73-capability-slice-planning
filesystem/123-directory-watcher
github/124-project-metadata
```

The existing `refactor/repository-standardization` branch is grandfathered.
Tiny work that repository policy permits without an issue may use
`<area>/<short-summary>`. This is a preferred convention, not a branch-name CI
gate.

Keep branches focused. If a branch starts solving unrelated problems, split
the extra work into a new issue and branch.

## Contribution licensing

GameWIP first-party source code and documentation are distributed under the
[Apache License 2.0](https://github.com/Andrei11033/GameWIP/blob/master/LICENSE).
Unless a contributor explicitly states otherwise in writing and the maintainer
accepts different terms, a contribution intentionally submitted for inclusion
is provided under Apache-2.0 as described by section 5 of that license.

Submit only original work or material that you have the right to contribute.
Keep third-party license and attribution notices with the corresponding
dependency or material. Do not copy code, documentation, media, or generated
assets into the project merely because they are publicly visible. Contributors
retain copyright in their work; contribution does not transfer ownership of
the official repository, project settings, releases, or branding.

## Explain the result in a pull request

Open a pull request before merging into `master`. `CODEOWNERS` routes review to
the maintainer, while required checks and resolved conversations enforce the
merge boundary.

The pull request title should normally be the squash commit title:

```text
area: imperative summary
```

This is a primary-area prefix, unlike an issue title's work-type prefix. The
standalone squash subject omits GitHub's generated `(#123)` suffix.

The pull request body should include:

- What changed.
- Linked issue numbers when applicable.
- The validation commands or inspections performed.
- Checklist items from the pull-request template.
- The intended squash merge message for non-trivial changes.

Use `Draft` only while the pull request is not ready for final review or merge.

## Required pull-request metadata

Ready-for-review pull requests must pass the `PR Standards` workflow.

The workflow enforces:

- Title format: `area: imperative summary`.
- Required pull-request body sections from the template.
- Non-empty summary and validation notes.
- A linked issue such as `Closes #123`, or an explicit `No linked issue: reason`.
- A concrete merge-message title.
- Exactly one `area:*`, one canonical `type:*`, and one canonical `priority:*` label.
- No unsupported `compat:*` label; optional `compat:breaking` is allowed.

Draft pull requests may be incomplete while work is still moving.

Dependabot pull requests are exempt from the human metadata check, but they still run the normal validation workflow.

## Record concrete validation evidence

Validation notes should be concrete enough that a future maintainer understands which behavior was verified.

Good examples:

```text
- `ctest --preset test` passed all modular correctness-test entries.
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

## Understand the automated checks

The `Validation` workflow runs on pull requests into `master`, pushes to `master`, and manual dispatch.

It performs:

- MSYS2 UCRT64 configure, build, non-package CTest contracts, and modular correctness tests with internal test hooks enabled.
- MSYS2 CLANG64 AddressSanitizer configure, build, and test, including instrumented package consumers.
- Ordinary installed-package validation with CMake `4.4.2` or newer across Ninja and Ninja Multi-Config consumers.
- GCC coverage configure, test, instrumented package-consumer validation, and report generation.
- Google Benchmark registration dry run without performance thresholds.
- Doxygen documentation build with `GAMEWIP_BUILD_DOCS=ON` and an empty warning log.
- The complete repository quality gate: clang-format/clang-tidy, Ruff,
    PSScriptAnalyzer, ESLint, Prettier, Gersemi, yamllint, markdownlint-cli2,
    actionlint, JSON Schema/semantic checks, repository standards, documentation
    standards, and local Markdown links over maintained first-party files.
- JavaScript policy/unit tests and PowerShell helper regression tests.
- Immutable Action pins, workflow timeouts/permissions, trusted
    `pull_request_target` policy boundaries, and required public repository files.

The `Doxygen Docs` workflow publishes the retained documentation artifact only
after the complete `Validation` workflow succeeds on a `master` push. This
avoids rebuilding the same documentation for Pages. Pull requests build docs
for validation but do not publish them. Manual dispatch remains a guarded
recovery path that performs its own build.

Branch protection for `master` must require:

```text
PR Standards / Check PR Standards
Validation / Build and Test
Validation / AddressSanitizer
Validation / Coverage
Validation / Packages (CMake)
Validation / Repository Checks
Validation / Docs Check
```

Local static-analysis commands and file scope are documented in
@ref project_static_analysis. The authoritative check ownership, validation
tiers, manual dispatch map, protected-branch baseline, and repository audit
checklist are documented in @ref project_repository_maintenance.

## Project automation

Use a closing keyword such as `Closes #6` in the pull request body. When a PR
lacks a primary dimension, project automation inherits one agreed area or type
from linked issues and selects their highest priority. Existing valid PR
primary metadata wins. Conflicting linked areas, types, or milestones are
reported instead of creating duplicate primary metadata. Assignees and
optional `compat:breaking` are also synchronized.

Issue status is derived from closure, **Blocked by** relationships, linked pull requests, assignees, active milestone, and required labels. The
complete rule order, repository variables, token requirement, and dry-run command are documented in @ref project_repository_automation.

## Finish with a readable merge

Prefer `Squash and merge` for feature branches so `master` keeps one clean changelog-style commit per completed piece of work.

Use `Rebase and merge` only when the individual commits are already meaningful and worth preserving.

Avoid ordinary merge commits on `master` unless there is a deliberate reason to preserve branch structure.

After a pull request is merged, delete the feature branch unless more work will continue on it immediately.

## Squash commit messages

This file is the authoritative workflow for squash commit messages. The durable `area: imperative summary` decision is summarized in @ref
project_decisions.

Commit title format:

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
- Added or changed test coverage, test support, validation, or verification workflows.

Documentation:
- Added or changed docs, examples, public API notes, or developer guidance.
```

Do not keep GitHub's default `(#123)` suffix if the project history should read like a standalone changelog. Mention the pull request or issue in the
body instead when it matters.

## Local sync after merge

After a pull request merges into `master`:

```powershell
git switch master
git pull --ff-only origin master
git branch -d branch/name
```

If GitHub deleted the remote branch, prune stale remote-tracking names:

```powershell
git fetch origin --prune
```

## Related pages

- @ref project_extending
- @ref project_repository_automation
- @ref project_static_analysis
- @ref project_versioning
- @ref project_decisions
