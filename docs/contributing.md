@page project_contributing Contributor workflow

Use this page when taking a change from an issue to a reviewed pull request.
It covers the project conventions that affect day-to-day work.

For library extension requirements, use @ref project_extending. Project-wide
technical decisions belong in @ref project_decisions, and release-number rules
belong in @ref project_versioning.

## The contribution path

Most changes follow this path:

1. Find or open an issue that explains the outcome.
2. Assign it when work begins and create a short-lived branch.
3. Make the focused change and run the checks appropriate to it.
4. Open a pull request that explains the result and records concrete evidence.
5. Resolve review comments and required checks.
6. Squash-merge the finished work and remove the branch.

## Protect the default branch

The default branch is:

```text
master
```

Keep `master` readable and releasable. Normal feature work should happen on a short-lived branch and merge through a pull request.

## Describe the work with an issue

Create an issue for anything beyond a tiny local cleanup. GitHub issues track
implementation work, validation work, bugs, and follow-up cleanup. The roadmap
defines milestone gates; issues define the work needed to meet them.

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
`priority:*` label. Add `compat:breaking` only when the work intentionally
changes a compatibility contract. Status, release, and blocker information
belong to GitHub's own fields and relationships.

A GitHub milestone means the issue is targeted to that release. A capability
slice does not receive a milestone until it is concrete enough to schedule.
A useful future issue may remain in Backlog without a milestone.

Assign an issue when work starts. Project automation moves it to `In Progress`.
Fully triaged work in the active milestone becomes Ready; future work remains
in Backlog.

Use GitHub's **Blocked by** relationship for hard dependencies. Describe softer
sequencing in the issue or roadmap instead of adding another label.

Start an issue with the problem or missing behavior. If timing matters, say why
it belongs now. Note the constraints and alternatives that matter, describe
what done looks like, and say how it will be checked. The issue forms provide
fields for this information.

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

Existing branches do not need to be renamed solely to match this convention.
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
the maintainer, and required checks must pass before the merge.

Use this format for pull request titles:

```text
area: imperative summary
```

Final squash commit subjects should keep GitHub's generated pull request
suffix:

```text
area: imperative summary (#123)
```

The pull request title uses the primary area. Issue titles use their work type.
Keeping the suffix links the squash commit in `master` to the pull request.

The pull request body should include:

- What changed.
- Linked issue numbers when applicable.
- The validation commands or inspections performed.
- Important implementation discoveries, corrected assumptions, platform
  surprises, or changes from the original direction.
- Required confirmations from the pull-request template.
- The intended squash merge message for changes that need more than a subject.

Use `Draft` only while the pull request is not ready for final review or merge.

## Required pull-request metadata

Ready-for-review pull requests must pass the `PR Standards` workflow. It checks
the title, required body sections, linked issue or explanation, merge message,
and primary labels. Its policy is read from the trusted base branch.

Draft pull requests may be incomplete while work is still moving.

Dependabot pull requests are exempt from the metadata check, but still run the
normal validation workflow.

## Record concrete validation evidence

Validation notes should name what was checked and what happened.

Good examples:

```text
- `ctest --preset test` passed all modular correctness-test entries.
- `GameWIPBenchmarks.exe --benchmark_dry_run` passed.
- The docs preset built successfully with no unexpected Doxygen warnings.
- Inspected the generated FileSystem public API page.
```

If a check was not run, say so directly and explain why.

## Understand the automated checks

The `Validation` workflow runs on pull requests into `master`, pushes to
`master`, and manual dispatch. It owns the build, tests, package checks,
sanitizers, coverage, documentation, and repository checks. The exact jobs and
branch-protection requirements are maintained in
@ref project_repository_maintenance. Use @ref project_static_analysis for
local quality commands.

## Project automation

Use a closing keyword such as `Closes #6` in the pull request body. Project
automation synchronizes issue and pull request metadata, including status,
assignees, labels, milestones, and dependencies. See
@ref project_repository_automation for the rule order and dry-run commands.

## Finish with a readable merge

Prefer `Squash and merge` so `master` keeps one commit for each completed piece
of work.

Use `Rebase and merge` only when the individual commits are already meaningful
and worth preserving.

Avoid ordinary merge commits on `master` unless there is a deliberate reason to
preserve branch structure.

After a pull request is merged, delete the feature branch unless more work will continue on it immediately.

## Squash commit messages

The pull request title and squash commit subject use the same area prefix. The
squash commit keeps GitHub's pull request suffix:

```text
area: imperative summary (#123)
```

Use a subject that states the change:

```text
desktop: keep window construction state available during callbacks (#123)
```

Add a short body when the reason, an important discovery, or a non-obvious
tradeoff is not clear from the subject. For example:

```text
desktop: keep window construction state available during callbacks (#123)

CreateWindowExW can dispatch messages before the HWND is published as an open
Window. Keep construction state available during that period while delaying
runtime-only handling until creation completes.

This prevents false NotOpen failures during visible-window creation.
```

Keep GitHub's generated `(#123)` suffix so
the commit in `master` links directly to its originating pull request.

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
