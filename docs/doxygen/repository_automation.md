@page project_repository_automation Repository and project automation

GameWIP uses event-driven GitHub automation for repeatable metadata and status updates. Automation
is deliberately limited to deterministic state; priority and planning decisions remain manual.

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

An unassigned issue is not automatically Backlog. A fully triaged issue in the active milestone is
Ready even before someone claims it. Blocked always takes precedence over active work or review.

Open pull requests are Review unless they are drafts or have requested changes, in which case they
are In Progress. Closed and merged pull requests are Done.

## Linked pull request metadata

Use a closing keyword such as `Closes #6` in the pull request body. For linked issues, automation:

- Adds their `area:*` and `type:*` labels
- Selects the highest linked `priority:*` label
- Adds linked issue assignees, or the pull request author when no issue is assigned
- Copies the milestone when every linked milestone agrees

These updates are additive. Existing manually selected labels and assignees are preserved. A
milestone conflict is reported in the workflow summary and is left for a maintainer to resolve.

## Events and reconciliation

`.github/workflows/project-automation.yml` reacts to issue and pull request changes. It also
reconciles the complete project every six hours so dependency, review, or project-side changes that
do not emit a usable repository event are repaired. Manual dispatch supports all items or one issue
or pull request, with an optional dry run.

The pull request trigger uses `pull_request_target` and always checks out automation from the default
branch. Pull request code is never executed with the project token.

## Required repository configuration

Configure these Actions variables:

```text
PROJECT_OWNER=Andrei11033
PROJECT_NUMBER=2
ACTIVE_MILESTONE=R00 - Bootstrap
```

Add a `PROJECT_TOKEN` Actions secret containing a dedicated classic personal access token with the
`repo` and `project` scopes. Do not place the token in a variable, workflow file, commit, or log.

After the workflow reaches `master`, verify configuration without writes:

```powershell
gh workflow run project-automation.yml -f kind=all -f dry_run=true
```

Then inspect the workflow summary and run one normal reconciliation. Native project workflows may
still auto-add repository items or perform simple close transitions; this repository workflow is
the authority that reconciles final metadata and status.
