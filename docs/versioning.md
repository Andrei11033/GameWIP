@page project_versioning Versioning policy

GameWIP has two related identities: a release version that communicates
compatibility, and a generated build identity that identifies the exact source
being run. This page explains where each value comes from and when it changes.

[Semantic Versioning 2.0.0](https://semver.org/spec/v2.0.0.html) is the normative version-format and precedence definition. This page records how GameWIP applies it.

## Release version: the compatibility promise

The root `project(GameWIP VERSION ...)` declaration in `CMakeLists.txt` is the only editable numeric project-version source.

Generated package version files, first-party dependency checks, Doxygen metadata, runtime version displays, and release notes derive their numeric version from `PROJECT_VERSION`.

Libraries distributed from this repository share the GameWIP project version.

## Pre-1.0 milestone mapping

Before V1, the version fields mean:

- MAJOR identifies the public compatibility generation and remains `0` until V1.
- MINOR identifies the roadmap milestone: R00 is `0`, R01 is `1`, R02 is `2`, and so on.
- PATCH identifies a published compatible correction within that milestone.

R00 uses `0.0.1` for its first release because `0.0.0` is reserved for the pre-release bootstrap. Later milestone baselines reset PATCH to zero.

Examples:

| Project state | Version |
| --- | --- |
| R00 first release | `0.0.1` |
| First compatible R00 correction | `0.0.2` |
| R01 baseline | `0.1.0` |
| First compatible R01 correction | `0.1.1` |
| R02 baseline | `0.2.0` |
| Stable V1 baseline | `1.0.0` |

Before `1.0.0`, a breaking public API or package change requires the next milestone MINOR version and a migration note. PATCH releases must remain backward compatible with their milestone baseline.

Version `1.0.0` means that the V1 public API, package, save-data, and compatibility promises have been explicitly defined and validated. It does not mean GameWIP development is finished.

## Build identity: the exact source revision

Every commit merged into `master` receives generated build identity without editing `CMakeLists.txt` or committing a build counter.

Official builds use the first-parent commit count as a monotonically increasing build number and include the abbreviated Git commit identifier.

Intended display forms:

```text
0.1.0-dev.247+g2238b22
0.1.0-dev.247+g2238b22.dirty
0.1.0
```

An untagged commit uses the `-dev.<build>` form. A build with tracked worktree modifications adds `dirty` to its build metadata. A clean commit carrying the exact annotated `v<PROJECT_VERSION>` tag uses the release form.

Development identifiers are diagnostics. They must not be published or treated
as package-compatibility releases. The generated header exposes the build count
separately; the clean annotated release display remains exactly the numeric
version.

Build identity is generated from Git when available. CI must fetch enough history to compute the first-parent count. A source archive or environment without Git must receive explicit generated identity inputs or report the build number and commit as unknown; it must not invent them.

Timestamps are not version identities because they prevent reproducible builds.

The numeric `PROJECT_VERSION` does not change for every merge. Only the generated build identity changes.

## Turning a version into a release

1. Set the root project version to the milestone target when development for that version begins.
2. Keep untagged builds marked as development snapshots.
3. Run release preparation only when the active milestone, root version, and explicit milestone target agree.
4. Open a release-preparation pull request for release notes and required derived metadata.
5. Never write directly to protected `master` for release preparation.
6. Require all protected validation checks and a human merge decision.
7. After merge, verify the exact commit and post-merge checks before creating the annotated `vX.Y.Z` tag and GitHub release.
8. Never move, overwrite, or reuse a published tag or version. Any correction receives a newer version.

Release preparation is performed by the guarded release-preparation workflow. The workflow must pass in read-only mode before it is allowed to create or reuse a release-preparation branch and pull request.

Only an immutable annotated tag and matching GitHub release make a version a completed release. A numeric value in an untagged source tree is a release target, not proof that the release exists.

## Generated and package versions

- CMake package version files use `PROJECT_VERSION`.
- Dependencies between bundled GameWIP packages use the shared exact project version.
- Doxygen's project number and runtime diagnostics use the full generated display version.
- Clean release-tag builds show the numeric version.
- Development builds include their generated build number and commit.
- Release notes name the root project version selected by the verified release commit.
- Hard-coded version examples are not allowed when they can be generated, substituted, or written without a fixed numeric value.

The repository README does not duplicate the editable source version. Its latest-release badge derives the published version from immutable GitHub release tags. Before the first release exists, the badge may report that no release is available; it must not present an untagged development target as published.

## Independent schema versions

Project versioning remains separate from save formats, network protocols, asset formats, manifest formats, CMake preset schemas, workflow schemas, and other compatibility domains.

Each schema receives its own version only when that domain requires migration or compatibility handling.

## Related pages

- @ref project_roadmap
- @ref project_library_compatibility
- @ref project_repository_automation
- @ref project_release_automation
- @ref project_contributing
