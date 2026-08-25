@page project_release_automation Release automation

GameWIP turns a verified milestone into a release in two guarded stages:
preparation creates a reviewable release pull request, and finalization tags the
exact validated merge commit. Neither stage derives versions from issue counts,
bypasses branch protection, or writes release changes directly to `master`.

## Required configuration

Configure these Actions variables:

```text
ACTIVE_MILESTONE=R01 - Window, Input, and Action Foundation
```

Change `ACTIVE_MILESTONE` to the next milestone only after the previous milestone's tag, GitHub release, closure issue, and handoff are complete. The
same workflow applies to R00, R01, later roadmap milestones, and compatible PATCH releases.

Configure `PROJECT_TOKEN` as an Actions secret. The token must be a dedicated GitHub App token or dedicated maintainer token with the minimum
permissions needed to:

- Read repository contents, issues, milestones, pull requests, workflow runs, tags, and releases.
- Create release-preparation branches.
- Create release-preparation pull requests.
- Create annotated release tags.
- Create or publish GitHub releases.

Do not store the token in source, logs, variables, issue comments, pull request bodies, or generated release notes.

Create two protected GitHub environments:

| Environment | Protects | Required configuration |
| --- | --- | --- |
| `maintainer-write` | Release-preparation branch and pull-request writes | Restrict to `master`; require a maintainer review. |
| `release-production` | Immutable tag and GitHub release creation | Restrict to `master`; require a maintainer review; disable administrator bypass when the gate must be mandatory. |

Self-approval may remain enabled for a single-maintainer repository. These
environment reviews are the remote authorization gates; `PROJECT_TOKEN` is a
workflow credential and must not be treated as an entered project password.
Readiness and finalization dry runs do not wait for environment approval.

After required reviewers are active, add these environment secrets:

```text
maintainer-write: MANUAL_WRITE_PROTECTION_CONFIGURED=required-reviewer
release-production: RELEASE_PRODUCTION_PROTECTION_CONFIGURED=required-reviewer
```

The approval jobs fail closed while the matching marker is absent. GitHub Free,
Pro, and Team provide required reviewers only for public repositories; keep the
markers unset until this repository is public or its plan supports the
protection rule for private repositories.

## Milestone release metadata

Every releasable milestone needs exactly one target version and exactly one release issue.

The milestone description must contain:

```text
Release version: `X.Y.Z`
```

The release issue can be resolved in either of these ways:

- Preferred for new milestones: exactly one issue in the milestone is labeled `type:release` or has a title beginning with `release:`.
- Explicit compatibility form: the milestone description contains:

```text
Release issue: `#N`
```

The release issue must stay open during readiness checks and release-preparation pull-request creation. It closes only after finalization has created
the immutable tag and GitHub release and the milestone handoff is complete.

## Readiness check

Run the read-only readiness check first:

```powershell
.\gamewip.bat workflow run release-check
gh workflow run release-preparation.yml -f command=check -f dry_run=true
```

The helper prints the raw `gh` command before asking for confirmation. Add
`-Preview` to print it without authentication or dispatch.

The manually dispatched check exits unsuccessfully and names the unmet release
condition while the active milestone is not ready. That fail-closed result is
expected during normal milestone development and must not be bypassed merely to
produce a green workflow run.

The check verifies that:

- The selected milestone matches `ACTIVE_MILESTONE`.
- The milestone has explicit `Release version:` metadata.
- The milestone has exactly one release issue, resolved from a `type:release` label, a `release:` title, or explicit milestone metadata.
- The root CMake project version matches the milestone release version.
- The milestone has no open implementation issues except its release issue.
- The target version is newer than the latest immutable release tag.
- The latest `master` commit has the required validation workflows passing.
- Release-preparation branches and pull requests can be reused safely.

Automatic issue and milestone events run the same readiness logic without mutating source, creating tags, or creating releases.

## Prepare the release pull request

After the dry run is correct, prepare the release pull request:

```powershell
.\gamewip.bat workflow run release-prepare
gh workflow run release-preparation.yml -f command=prepare -f dry_run=false
```

The helper requires the typed phrase `release-prepare master`. GitHub then
holds the write behind the `maintainer-write` environment approval.

The workflow creates or reuses:

- `release/vX.Y.Z`
- `release: prepare vX.Y.Z`
- `docs/releases/vX.Y.Z.md`

The generated pull request references the release issue but does not close it. The release issue remains open until the tag, GitHub release, and
milestone handoff exist.

A maintainer must fill in the final validation evidence, review the release-preparation pull request, and merge it manually. The workflow must not
write directly to `master`.

Finalization rejects release notes that still contain the generated validation-evidence placeholder or unchecked release checklist items.

## Finalize the release

After the release-preparation pull request merges and post-merge validation passes on `master`, capture the exact master commit:

```powershell
$releaseCommit = gh api repos/Andrei11033/GameWIP/branches/master --jq .commit.sha
```

Run a finalization dry run:

```powershell
.\gamewip.bat workflow run release-finalize-dry-run -ReleaseCommit $releaseCommit
gh workflow run release-preparation.yml -f command=finalize -f dry_run=true -f release_commit=$releaseCommit
```

If the dry run verifies the exact commit, publish the tag and release:

```powershell
.\gamewip.bat workflow run release-finalize -ReleaseCommit $releaseCommit
gh workflow run release-preparation.yml -f command=finalize -f dry_run=false -f release_commit=$releaseCommit
```

The write requires a typed phrase containing the exact commit SHA and approval
through the `release-production` environment.

Finalization creates an annotated `vX.Y.Z` tag and a matching GitHub release. Existing matching artifacts are reused. Conflicting tags, releases,
branches, or pull requests cause a safe failure.

## Recovery behavior

| Failure | Required action |
| --- | --- |
| Readiness check fails. | Fix the reported milestone, version, open issue, or workflow state. Run the check again. |
| Release issue closed early. | Reopen it. The implementation pull request may reference it, but the release issue must remain open until finalization and handoff are complete. |
| Release branch exists without a valid pull request. | Inspect the branch. Reuse it only if it is based on the expected `master` commit; otherwise delete it manually and rerun preparation. |
| Release pull request was closed without merge. | Reopen it if correct, or delete the release branch and rerun preparation. |
| Validation fails on the release pull request. | Fix through normal pull-request commits. Do not tag. |
| Post-merge validation fails. | Fix forward on `master` through a normal pull request. Do not move or reuse the failed release target. |
| Annotated tag creation fails. | Verify whether the tag exists. If it exists with the correct target, reuse it; if it points elsewhere, stop and choose a newer version. |
| GitHub release creation fails. | Rerun finalization. Existing matching tags and releases are reused. |
| Milestone is reopened after release. | Treat follow-up work as a new PATCH or later milestone task. Do not retarget the published tag. |

## Maintainer rules

- Release automation must never derive the version by counting issues.
- Release automation must never push version commits directly to `master`.
- Release automation must never create, move, or overwrite a published tag.
- Human review and merge are required before finalization.
- Untagged builds remain development snapshots.

## Related pages

- @ref project_repository_maintenance
- @ref project_versioning
- @ref project_repository_automation
- @ref project_static_analysis
