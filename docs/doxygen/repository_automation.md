@page project_repository_automation Repository and project automation

GameWIP uses event-driven GitHub automation for repeatable metadata and project status updates. Automation is deliberately limited to deterministic state. Priority, scope, security disclosure, milestone intent, and product decisions remain human responsibilities.

## Scope

This page documents GitHub project status rules, linked pull-request metadata, event handling, required repository configuration, token requirements, dry-run behavior, and maintainer review points.

Contributor-facing GitHub workflow rules are documented in `docs/contributing.md`.

## Project status rules

Issue status uses the first matching rule in this order:

| Condition | Status |
| --- | --- |
| The issue is closed. | Done |
| At least one issue listed under **Blocked by** is open. | Blocked |
| A linked, non-draft pull request is ready for review. | Review |
| The issue has an assignee or an active draft/changes-requested pull request. | In Progress |
| The issue belongs to the active milestone and has `area:*`, `type:*`, and `priority:*` labels. | Ready |
| None of the preceding rules apply. | Backlog |

An unassigned issue is not automatically Backlog. A fully triaged issue in the active milestone is Ready before someone claims it. Blocked takes precedence over active work and review.

Open pull requests are Review unless they are drafts or have requested changes, in which case they are In Progress. Closed and merged pull requests are Done.

## Linked pull request metadata

Use a closing keyword such as `Closes #6` in the pull request body.

For linked issues, automation:

- Adds their `area:*` and `type:*` labels.
- Selects the highest linked `priority:*` label.
- Adds linked issue assignees, or the pull request author when no linked issue is assigned.
- Copies the milestone when every linked milestone agrees.

These updates are additive. Existing manually selected labels and assignees are preserved. Milestone conflicts are reported in the workflow summary and left for a maintainer to resolve.

## Events and reconciliation

`.github/workflows/project-automation.yml` reacts to issue and pull request changes. It also reconciles the complete project every six hours so dependency, review, or project-side changes that do not emit a usable repository event are repaired.

Manual dispatch supports all items or one issue or pull request, with an optional dry run.

The pull request trigger uses `pull_request_target` and checks out automation from the default branch. Pull request code is never executed with the project token.

## Required repository configuration

Configure these Actions variables:

```text
PROJECT_OWNER=Andrei11033
PROJECT_NUMBER=2
ACTIVE_MILESTONE=R00 - Bootstrap
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
.\gamewip.bat workflow -WorkflowAction run -Workflow project-dry-run
```

Inspect the workflow summary. If the dry run is correct, run one normal reconciliation.

Repository checks for automation scripts should also pass locally when those scripts change:

```powershell
node --check .github/scripts/project-automation.js
node --check .github/scripts/project-automation.test.js
node --test .github/scripts/project-automation.test.js
```

## Failure behavior

| Symptom | Likely cause | Action |
| --- | --- | --- |
| Items remain in the wrong status. | A repository event did not contain enough project context or reconciliation has not run yet. | Run manual reconciliation, preferably dry run first. |
| A pull request milestone is not copied. | Linked issues disagree on milestone. | Resolve the milestone conflict manually. |
| Labels are incomplete. | Linked issues are missing required labels or PR metadata is incomplete. | Fix issue labels or pull request metadata. |
| Authentication fails. | `PROJECT_TOKEN` is missing or lacks required scopes. | Replace the secret with a dedicated token that has `repo` and `project` scopes. |
| A security review flags `pull_request_target`. | The workflow may be executing untrusted PR code. | Verify that automation code is checked out from the default branch and PR code is not executed. |

## Maintainer notes

Automation may reconcile deterministic metadata and status. It must not decide product priority, accept security disclosure responsibility, override human scope decisions, or execute untrusted pull-request code with privileged credentials.

Native GitHub project workflows may still auto-add repository items or perform simple close transitions. This repository workflow is the authority that reconciles final metadata and status.

When changing automation:

- Update script tests with the behavior change.
- Run Node syntax and unit tests.
- Run a dry-run reconciliation before a write reconciliation.
- Keep token use out of logs.
- Update `docs/contributing.md` when contributor-facing workflow changes.

## Related pages

- @ref project_static_analysis
- @ref project_extending
- @ref project_documentation
