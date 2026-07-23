@page project_repository_maintenance Repository maintenance policy

This page is the maintainer source of truth for GitHub presentation, protected
checks, merge policy, Actions configuration, manual workflows, and periodic
repository audits. Contributor behavior remains in @ref project_contributing;
automation-specific behavior remains in @ref project_repository_automation and
@ref project_release_automation.

## Public landing page

The GitHub repository should always provide a concise description, the
published Doxygen site as its homepage, and links from `README.md` to setup,
roadmap, documentation, issues, releases, contributing guidance, and security
reporting. README claims must describe the current default branch and latest
release rather than planned functionality.

GitHub topics should remain few and factual. Do not advertise unsupported
platforms, finished gameplay, stable APIs, or production readiness.

## Validation ownership

Pull requests into `master` run one `Validation` workflow. Its jobs divide the
required evidence without requiring contributors to dispatch duplicate runs:

| Required check | Owns |
| --- | --- |
| `PR Standards / Check PR Standards` | Ready-for-review title, body, linked issue, merge message, and labels. |
| `Validation / Build and Test` | Main configure/build path, non-package CTest contracts, benchmark registration, clang-tidy, and clang-format. |
| `Validation / Packages (CMake)` | Ordinary installed-package consumers with single- and multi-config generators. |
| `Validation / Repository Checks` | Automation/script tests, structured files, docs, links, Action pins, job policy, and public files. |
| `Validation / Coverage` | Instrumented tests and report generation; no percentage threshold. |
| `Validation / AddressSanitizer` | CLANG64 sanitizer configure, build, and tests. |
| `Validation / Docs Check` | Warning-free Doxygen build for the pull-request source. |

The `Doxygen Docs` workflow publishes the `Docs Check` artifact after the full
`Validation` workflow succeeds on a `master` push. It does not rebuild the same
source and is not an additional pull-request check. Manual deployment remains
an independent guarded recovery path and therefore performs its own build.

The base package cases run only in `Packages (CMake)`. Coverage and
AddressSanitizer intentionally retain their package entries because those runs
prove separately instrumented consumer link and execution behavior. Every
workflow job has a repository-checked timeout so malformed or hostile public
contributions cannot consume a runner indefinitely.

## Automation boundaries

Repository workflows remain separate when they have different trust or
permission boundaries. This avoids giving a convenience workflow broader access
only to save a small runner startup:

| Workflow | Authority | Reason it remains separate |
| --- | --- | --- |
| `Issue Area Labels` | Built-in `issues: write` token. | Maps an issue-form choice to one area label. |
| `Project Automation` | Dedicated project token. | Reconciles status and linked issue/PR metadata, including missed events. |
| `PR Standards` | Read-only pull-request metadata. | Enforces title, body, issue link, merge message, and labels without running contributor code. |
| `Validation` | Read-only contents plus artifact upload. | Produces build, test, analysis, docs, and repository evidence once. |
| `Release Preparation` | Dedicated project/release token. | Computes readiness automatically; writes occur only through guarded manual operations. |
| `Doxygen Docs` | Pages deployment permissions. | Reuses validated HTML after a successful `master` push; only guarded manual recovery rebuilds it. |

`Project Automation` uses `pull_request_target` only for metadata access. It
must always check out trusted default-branch automation with persisted checkout
credentials disabled, and must never fetch, build, import, or execute pull
request head content. The repository checker enforces this boundary.

Event-driven automation is the normal path. The project schedule is an
intentional repair pass for dependency, review, or project-side changes that do
not emit a complete repository event; maintainers should not manually repeat
routine reconciliation or validation.

## Required, change-driven, and release validation

The protected checks above are required for every non-draft pull request.
Contributors should also run the narrow local command that covers the changed
area before pushing. Formatting, focused tests, Markdown links, workflow/script
tests, or a local docs build are examples of change-driven checks.

Coverage inspection, profiling captures, benchmark measurement, manual UI
checks, and full local release validation are optional unless the change or
release checklist explicitly needs them. Benchmark CI verifies registration,
not performance thresholds.

Before release preparation, use the `local-release-check` helper bundle and the
release readiness dry run. Record actual evidence in the release pull request;
do not treat an earlier ordinary pull-request run as finalization evidence.

## Manual workflow map

Use `gamewip.bat workflow -WorkflowAction list` to discover supported
dispatches. The helper fixes the ref to `master`, shows the exact command, and
uses typed confirmation for writes and deployments.

| Dispatch | Use it when | Effect |
| --- | --- | --- |
| `validation` | Rechecking `master`, diagnosing CI, or validating repository settings. | Read-only repository validation. |
| `project-dry-run` | Inspecting project metadata reconciliation. | Read-only plan. |
| `project-write` | Repairing deterministic project metadata after reviewing a dry run. | Guarded project write. |
| `release-check` | Checking active-milestone readiness. | Read-only release plan. |
| `release-prepare` | Creating or refreshing the reviewed release pull request. | Guarded branch and PR write. |
| `release-finalize-dry-run` | Verifying the exact post-merge commit. | Read-only finalization plan. |
| `release-finalize` | Publishing the immutable tag and GitHub release. | Guarded production write. |
| `docs-deploy` | Recovering or deliberately republishing Pages. | Guarded deployment. |

Routine pull requests should not manually dispatch `validation`; the pull
request event already runs it. Routine pushes to `master` should not manually
dispatch `docs-deploy`; the push event already publishes the manual.

## Protected `master` baseline

Maintain these repository settings:

- Require the seven checks listed under **Validation ownership**, with the
  branch required to be up to date before merging.
- Require pull requests, linear history, and resolved review conversations.
- Enable branch updates so contributors can satisfy the strict up-to-date check
  without a maintainer repeating the update.
- Disable force pushes and branch deletion.
- Enforce protection for administrators.
- Allow squash merging as the normal merge method. Avoid merge commits; use
  rebase merging only for deliberately preserved commits.

Check names are an interface with branch protection. Rename a workflow or job
only when the branch-protection context is updated in the same maintenance
window and the new context has completed successfully.

Use one protected-branch policy owner. Do not duplicate the same `master` rules
in a second ruleset or Actions policy unless performing a documented migration;
overlapping rules create two settings that must be kept in sync. Actions
dependency policy is separate: workflow references are pinned to immutable
commits, and the repository-level full-SHA requirement should be enabled after
the pinned workflow baseline reaches `master`. Keep **Allow all actions and
reusable workflows** unless the trust model changes; a selected-actions allowlist
would duplicate the reviewed pins and create a second dependency list to maintain.

## Repository configuration

Actions variables:

```text
PROJECT_OWNER=Andrei11033
PROJECT_NUMBER=2
ACTIVE_MILESTONE=R01 - Window, Input, and Action Foundation
```

Update `ACTIVE_MILESTONE` only after the prior release, release issue,
milestone closure, project handoff, roadmap, and new milestone metadata agree.

`PROJECT_TOKEN` is the dedicated automation credential. Protected environments
and their marker secrets are documented by the owning automation pages. Keep
write markers unset until the matching required-reviewer rule really exists.

## Milestones, labels, and templates

Only the active milestone should carry ordinary implementation work. Future
milestones may exist as roadmap containers but should not be presented as
active. Close a completed milestone after its release and handoff are complete.

Every normal issue and ready pull request needs one primary `area:*`, `type:*`,
and `priority:*` label. Add a new label only when it represents durable routing
or triage information that existing labels cannot express. Keep issue forms,
the pull-request template, label descriptions, and automation mappings aligned.

## Public-repository baseline

The public repository must preserve these conditions:

1. The Apache-2.0 root `LICENSE`, project `NOTICE`, README summary, contributor
   terms, and GitHub license detection agree. Third-party dependency licenses
   remain separate.
2. Credentials, personal data, generated artifacts, large files, and material
   without distribution rights must not enter reachable history. Rotate a
   disclosed credential immediately and sanitize history when the disclosure
   requires it.
3. Issues and Discussions provide ordinary project contact. Security and
   conduct reports must follow their owning policies and must not place
   sensitive report details in public issues.
4. The README description, topics, homepage, latest release, roadmap,
   contribution entry point, code of conduct, security policy, issue forms, and
   `CODEOWNERS` routing remain accurate.
5. Branch protection and all seven required checks remain enabled for `master`.
6. Project and release dry runs, guarded-command previews, and Pages publication
   from `master` continue to succeed.
7. Collaborators, deploy keys, webhooks, installed apps, Actions secrets and
   variables, environment secrets, and repository access remain limited to
   their documented owners and purposes.

### Security and access configuration

- Discussions remains enabled so the README and issue-form question links have
  a valid destination.
- Private vulnerability reporting, Dependabot alerts and security updates,
  secret scanning, push protection, and code scanning remain enabled, with
  security-alert notifications routed to the maintainer.
- Actions are pinned to full-length commit SHAs. The local repository checker
  enforces the same rule in pull requests.
- The `maintainer-write`, `release-production`, and `github-pages` environments
  require review and restrict deployments to `master` before their protection
  markers are set.
- Actions permissions, fork-pull-request approval, merge methods, branch
  deletion, signoff policy, Pages, homepage, topics, and the social preview
  remain consistent with the documented repository workflow.
- Untrusted pull-request code receives no maintainer secrets or write access and
  cannot merge or deploy without maintainer authorization.
- Issue forms, Discussions, security reporting, release downloads, and generated
  documentation remain usable to signed-out visitors.

License choice and accepted history decisions are recorded in @ref
project_decisions. A newly discovered credential or distribution problem still
requires immediate investigation and a focused GitHub issue.

## Periodic audit

Repeat the configuration audit at each milestone handoff and after workflow,
branch-protection, environment, permission, or visibility changes. Record exact
remote setting changes and the pull request used to validate them. Create a
focused follow-up issue for anything intentionally deferred.

## Related pages

- @ref project_contributing
- @ref project_repository_automation
- @ref project_release_automation
- @ref project_static_analysis
- @ref project_documentation
