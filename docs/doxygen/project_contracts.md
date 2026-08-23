@page project_contracts Project contracts and standards

Project contracts define the rules that keep the repository consistent. Update them deliberately when ownership, compatibility, backend behavior,
documentation policy, versioning, or architectural direction changes.

Use these pages when reviewing whether a change belongs in the project, how it should be documented, and which boundaries it must preserve.

## Standards and extension contracts

- @subpage project_public_api_contract — Naming, errors, ownership, threading,
  exceptions, ABI, namespaces, and compatibility rules for public C++ APIs.
- @subpage project_documentation — Where each kind of information belongs and
  what complete project, library, source, and API documentation requires.
- @subpage project_extending — Cross-repository checklists for new or changed
  libraries, APIs, executables, backends, tests, workflows, and documentation.
- @subpage project_cmake_infrastructure — Shared CMake helpers for libraries,
  platforms, packages, validation, reports, documentation, and runtime staging.
- @subpage project_platform_backend_contract — Layout, selection, error
  translation, cleanup, and validation rules for platform-specific code.
- @subpage project_versioning — Version format, generated build identity,
  compatibility meaning, and release numbering.
- @subpage project_decisions — Stable architectural, licensing, repository,
  dependency, platform, and release-policy decisions.

## Internal foundation infrastructure

- @subpage internal_base - Admission, dependency, testing, and extension rules for narrow source-tree-only mechanisms.

## Repository automation standards

Static GameWIP registries are JSON Schema draft 2020-12 documents. Repository
constants live in `scripts/config/project.json`, command metadata lives in
`scripts/config/commands.json`, development-tool policy lives in
`scripts/config/project-tools.json`, and setup metadata lives in the two JSON
registries under `scripts/setup/config/`. Unsupported schema versions and
schema or semantic violations fail before the requested action runs.

Reusable PowerShell functions use `Verb-GameWipNoun` with approved PowerShell
verbs and PascalCase parameters. `Assert-GameWip...` is the intentional naming
exception for validation helpers whose contract is to throw on failure; a
`Test-*` name remains reserved for Boolean queries.

Exact version-sensitive quality-tool pins are centrally declared as follows:

| Tool | Exact version | Provider |
| --- | --- | --- |
| Ruff | 0.16.4 | Python |
| ESLint | 10.9.0 | npm |
| Prettier | 3.9.6 | npm |
| Gersemi | 0.28.0 | Python |
| markdownlint-cli2 | 0.23.2 | npm |
| yamllint | 1.38.0 | Python |
| PSScriptAnalyzer | 1.25.0 | PowerShell Gallery |
| actionlint | 1.7.12 | verified GitHub release |

`gamewip tools status` is offline. `gamewip tools check-updates` performs an
explicit online query without changing files or installations. `gamewip tools
update -Tool <id|all>` requires a clean tracked tree, plans all requested
updates first, updates the authoritative pin and live references, installs the
managed version, and validates the result without committing or pushing.
Historical release notes are excluded from version rewrites.

Explicit formatter/linter configuration lives under `config/quality/` and is passed to each owning tool explicitly. `.clang-format`, `.clang-tidy`, and `.editorconfig` remain at repository root because their upward-discovery behavior is useful to editors and C++ tools.

`setup.bat update` brings the machine into compliance with the versions already
declared by the checkout; it does not advance exact project pins. Non-pacman
tools persist under `C:\MSYS2\GameWIPTools` with the exact `bin`, `tools`,
`npm`, `python`, and `powershell` subdirectories.

Repository-local helper data is disposable and confined to
`build/gamewip/{cache,state,temp,runs}`. Each operation owns a unique marked
temp directory and removes it in `finally`. Deleting `build/` is supported;
read-only status and quality actions recreate their required storage without
reinstalling persistent tools.

## Related pages

- @ref project_manual
- @ref project_tools
- @ref project_planning
- @ref project_library_compatibility
