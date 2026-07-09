@page project_release_automation Release automation

GameWIP release automation prepares milestone releases without deriving versions from issue counts, bypassing branch protection, or pushing release changes directly to `master`.

## Required configuration

Configure these Actions variables:

```text
ACTIVE_MILESTONE=R00 - Bootstrap
```

Configure `PROJECT_TOKEN` as an Actions secret. The token must be a dedicated GitHub App token or dedicated maintainer token with the minimum permissions needed to:

- Read repository contents, issues, milestones, pull requests, workflow runs, tags, and releases.
- Create release-preparation branches.
- Create release-preparation pull requests.
- Create annotated release tags.
- Create or publish GitHub releases.

Do not store the token in source, logs, variables, issue comments, pull request bodies, or generated release notes.

## Readiness check

Run the read-only readiness check first:

```powershell
gh workflow run release-preparation.yml -f command=check -f dry_run=true
```

The check verifies that:

- The selected milestone matches `ACTIVE_MILESTONE`.
- The milestone has explicit `Release version:` metadata.
- The root CMake project version matches the milestone release version.
- The milestone has no open implementation issues except its release issue.
- The target version is newer than the latest immutable release tag.
- The latest `master` commit has the required validation workflows passing.
- Release-preparation branches and pull requests can be reused safely.

Automatic issue and milestone events run the same readiness logic without mutating source, creating tags, or creating releases.

## Prepare the release pull request

After the dry run is correct, prepare the release pull request:

```powershell
gh workflow run release-preparation.yml -f command=prepare -f dry_run=false
```

The workflow creates or reuses:

- `release/vX.Y.Z`
- `release: prepare vX.Y.Z`
- `docs/releases/vX.Y.Z.md`

The generated pull request references the release issue but does not close it. The release issue remains open until the tag and GitHub release exist.

A maintainer must fill in the final validation evidence, review the release-preparation pull request, and merge it manually. The workflow must not write directly to `master`.

Finalization rejects release notes that still contain the generated validation-evidence placeholder or unchecked release checklist items.

## Finalize the release

After the release-preparation pull request merges and post-merge validation passes on `master`, capture the exact master commit:

```powershell
$releaseCommit = gh api repos/Andrei11033/GameWIP/branches/master --jq .commit.sha
```

Run a finalization dry run:

```powershell
gh workflow run release-preparation.yml -f command=finalize -f dry_run=true -f release_commit=$releaseCommit
```

If the dry run verifies the exact commit, publish the tag and release:

```powershell
gh workflow run release-preparation.yml -f command=finalize -f dry_run=false -f release_commit=$releaseCommit
```

Finalization creates an annotated `vX.Y.Z` tag and a matching GitHub release. Existing matching artifacts are reused. Conflicting tags, releases, branches, or pull requests cause a safe failure.

## Recovery behavior

| Failure | Required action |
| --- | --- |
| Readiness check fails. | Fix the reported milestone, version, open issue, or workflow state. Run the check again. |
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

- @ref project_versioning
- @ref project_repository_automation
- @ref project_static_analysis
