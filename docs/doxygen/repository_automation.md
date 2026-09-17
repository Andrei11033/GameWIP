@page project_repository_automation Repository and project automation

GameWIP uses local scripts and GitHub workflows for the updates that are easy to
make repeatable: tool declarations, project metadata, and project status. The
scripts handle deterministic state. Priority, scope, security disclosures,
milestone intent, and product decisions still belong to a maintainer.

This page covers the project-tool updater, project reconciliation, event
handling, required configuration, and dry-run behavior. The important boundary
is that the automation can apply a plan, but it does not decide what the
project should build or how a security report should be handled.

Contributor-facing GitHub workflow rules are documented in `docs/contributing.md`.

## Project-tool update automation

Project-tool updates start with `scripts/config/project-tools.json`. Provider
adapters handle upstream queries, installation, verification, and rollback for
the machine state they manage. The shared tool engine handles discovery,
planning, tracked staging, validation, preview, consent, application,
post-update verification, and the final quality gate. A normal tool for an
existing provider, or another reference to an existing tool version, belongs in
configuration rather than new updater code.

The updater has a closed set of files and fields that it may change. It may
update planned registry string fields and declared live references only. Registry
changes use the expected old value and planned new value as a compare-and-set,
and unrelated JSON source stays byte-for-byte stable. A text-reference
`pattern` is a literal template with exactly one `{version}` token, not a regular
expression. `text` and `cmakeMinimum` references are live; `path` references
only record where a tool is used and are never rewritten.

Repository text reads use strict UTF-8 and reject malformed input. Writes use
UTF-8 without a byte-order mark. `tools update ... -Preview` still runs
discovery, planning, staging, and validation, then stops before changing tracked
files or machine state. When everything is current, the plan stages nothing and
does not reinstall an identical tool. A provider owns rollback once it starts
changing its managed machine state; the shared engine does not try to wrap every
provider in one universal machine transaction.

## Project status rules

Issue status uses the first matching rule in this order:

| Condition | Status |
| --- | --- |
| The issue is closed. | Done |
| At least one issue listed under **Blocked by** is open. | Blocked |
| A linked, non-draft pull request is ready for review. | Review |
| The issue has an assignee or an active draft/changes-requested pull request. | In Progress |
| The issue belongs to the active milestone and has exactly one canonical `area:*`, `type:*`, and `priority:*` label. | Ready |
| None of the preceding rules apply. | Backlog |

An unassigned issue is not automatically Backlog. A fully triaged issue in the active milestone is Ready before someone claims it. Blocked takes
precedence over active work and review.

Open pull requests are Review unless they are drafts or have requested changes, in which case they are In Progress. Closed and merged pull requests
are Done.

## Linked pull request metadata

Use a closing keyword such as `Closes #6` in the pull request body.

For linked issues, automation:

- Inherits one area when all linked issues that provide an area agree.
- Inherits one type when all linked issues that provide a type agree.
- Selects the highest linked `priority:*` label.
- Adds `compat:breaking` when any linked issue has it.
- Adds linked issue assignees, or the pull request author when no linked issue is assigned.
- Copies the milestone when every linked milestone agrees.

An existing valid PR area, type, or priority wins over a differing inherited
candidate. Missing dimensions receive an unambiguous candidate. Duplicate PR
primary labels and conflicting linked areas, types, or milestones are reported
instead of guessed or expanded into multiple labels. Existing assignees and a
manually selected milestone remain preserved.

## Events and reconciliation

`.github/workflows/project-automation.yml` reacts to issue and pull request
changes. It also reconciles the complete project every six hours. That scheduled
run repairs dependency, review, or project-side changes that did not produce a
usable repository event.

Manual dispatch supports all items or one issue or pull request, with an optional dry run.

Guarded helper dispatch records the pre-dispatch run set and dispatch time, then
correlates the visible run to the current GitHub CLI user, workflow, branch, and
dispatch window. If more than one new run still satisfies that correlation, the
helper fails closed rather than attaching to an arbitrary workflow run.

The pull request trigger uses `pull_request_target` and checks out automation
from the default branch. Pull request code is never executed with the project
token.

## Required repository configuration

Configure these Actions variables:

```text
PROJECT_OWNER=Andrei11033
PROJECT_NUMBER=2
ACTIVE_MILESTONE=R01 - Desktop, Input, and Action Foundation
```

Add a `PROJECT_TOKEN` Actions secret containing a dedicated classic personal access token with the `repo` and `project` scopes.

Do not place the token in a variable, workflow file, commit, test fixture, issue comment, pull request body, or log.

Create a `maintainer-write` GitHub environment, restrict its deployment branch
to `master`, and configure a required maintainer reviewer. Self-approval may
remain enabled for a single-maintainer repository; the approval is a deliberate
second gate, not a two-person policy. Disable administrator bypass if manual
writes must never skip the gate.

After the reviewer rule is active, add the environment secret
`MANUAL_WRITE_PROTECTION_CONFIGURED=required-reviewer`. The approval job fails
closed while this marker is absent. Do not add the marker merely to bypass a
plan that does not support required reviewers.

The approval job runs only for a manually dispatched reconciliation with
`dry_run=false`. Scheduled and event-driven reconciliation continues
unattended, and manual dry runs do not request approval. A repository secret is
a workflow credential, not an interactive password; never add a password input
or attempt to compare a user-entered value with `PROJECT_TOKEN`.

## Verification commands

After the workflow reaches `master`, verify configuration without writes:

```powershell
gh workflow run project-automation.yml -f kind=all -f dry_run=true
.\gamewip.bat workflow run project-dry-run
```

Inspect the workflow summary. If the dry run is correct, run one normal reconciliation.

Repository checks for automation scripts should also pass locally when those scripts change:

```bash
for file in .github/scripts/*.js; do
    node --check "$file"
done
node --test .github/scripts/*.test.js
```

## Failure behavior

| Symptom | Likely cause | Action |
| --- | --- | --- |
| Items remain in the wrong status. | A repository event did not contain enough project context or reconciliation has not run yet. | Run manual reconciliation, preferably dry run first. |
| A pull request milestone is not copied. | Linked issues disagree on milestone. | Resolve the milestone conflict manually. |
| Labels are incomplete or conflicting. | A primary dimension is missing or duplicated, or linked issues disagree. | Set exactly one primary area, type, and priority manually where needed. |
| Authentication fails. | `PROJECT_TOKEN` is missing or lacks required scopes. | Replace the secret with a dedicated token that has `repo` and `project` scopes. |
| A security review flags `pull_request_target`. | The workflow may be executing untrusted PR code. | Verify that automation code is checked out from the default branch and PR code is not executed. |

## When automation changes

Automation may reconcile deterministic metadata and status. It must not decide
product priority, take responsibility for a security disclosure, override a
scope decision, or execute untrusted pull-request code with privileged
credentials.

Native GitHub project workflows may still auto-add repository items or perform simple close transitions. This repository workflow is the authority
that reconciles final metadata and status.

When automation changes, update the script tests, run the Node syntax and unit
tests, and perform a dry-run reconciliation before a write reconciliation. Keep
tokens out of logs. Update `docs/contributing.md` when the contributor-facing
workflow changes. Project-tool facts and reference declarations belong in
`scripts/config/project-tools.json`; provider code is needed only for a new
provider contract.

## Related pages

- @ref project_repository_maintenance
- @ref project_static_analysis
- @ref project_extending
- @ref project_documentation
