@page project_contracts Project contracts and standards

These pages describe the rules that keep the repository consistent. Use them
when a change affects ownership, compatibility, backend behavior, documentation,
versioning, or project direction.

They also provide the review points for deciding where a change belongs, how it
should be documented, and which boundaries it must preserve.

## Standards and extension contracts

- @subpage project_public_api_contract - Naming, errors, ownership, threading,
  exceptions, ABI, namespaces, and compatibility rules for public C++ APIs.
- @subpage project_documentation - Where each kind of information belongs and
  what complete project, library, source, and API documentation requires.
- @subpage project_extending - Cross-repository integration rules for new or changed
  libraries, APIs, executables, backends, tests, workflows, and documentation.
- @subpage project_cmake_infrastructure - Shared CMake helpers for libraries,
  platforms, packages, validation, reports, documentation, and runtime staging.
- @subpage project_platform_backend_contract - Layout, selection, error
  translation, cleanup, and validation rules for platform-specific code.
- @subpage project_versioning - Version format, generated build identity,
  compatibility meaning, and release numbering.
- @subpage project_decisions - Stable architectural, licensing, repository,
  dependency, platform, and release-policy decisions.

## Internal foundation infrastructure

- @ref internal_base - Admission, dependency, testing, and extension rules for
  narrow source-tree-only mechanisms.

## Repository automation standards

Static GameWIP registries use JSON Schema draft 2020-12. Repository constants
are in `scripts/config/project.json`, command metadata is in
`scripts/config/commands.json`, development-tool policy is in
`scripts/config/project-tools.json`, and setup metadata is in the two JSON
registries under `scripts/setup/config/`. Unsupported schema versions and
schema or semantic violations stop the requested action before it runs.

Repository-backed informational tools may read their displayed version from a
named `commands.json` value. That avoids recursively launching the project
helper while another helper operation owns the operation lock.

Reusable PowerShell functions use `Verb-GameWipNoun` with approved PowerShell
verbs and PascalCase parameters. `Assert-GameWip...` is the intentional naming
exception for validation helpers whose contract is to throw on failure; a
`Test-*` name remains reserved for Boolean queries.

Exact version-sensitive quality-tool pins are declared centrally:

| Tool | Exact version | Provider |
| --- | --- | --- |
| Ruff | 0.16.6 | Python |
| ESLint | 10.10.0 | npm |
| Prettier | 3.9.6 | npm |
| Gersemi | 0.28.1 | verified GitHub release |
| markdownlint-cli2 | 0.23.2 | npm |
| yamllint | 1.38.0 | Python |
| jsonschema | 4.26.0 | Python |
| PSScriptAnalyzer | 1.25.0 | PowerShell Gallery |
| actionlint | 1.7.12 | verified GitHub release |

`gamewip tools status` is offline. `gamewip tools check-updates` performs an
explicit online query without changing files or installations. `gamewip tools
update <id|all>` builds and validates a complete staged plan before consent.
The update applies source-preserving compare-and-set registry mutations and
declared live references, verifies the planned provider state, and runs the
repository quality gate without committing or pushing. Informational `path`
references, including historical release notes, are never rewritten. See
@ref project_tools for reference kinds, the strict UTF-8 contract, preview and
no-op behavior, and the provider rollback boundary.

Explicit formatter and linter configuration lives under `config/quality/` and
is passed to each owning tool. Optional hygiene profiles and their explanations
follow the same rule without becoming part of the required quality gate.
`.clang-format`, `.clang-tidy`, and `.editorconfig` remain at the repository
root because editors and C++ tools discover them by searching upward.

`setup.bat update` brings the machine into compliance with the versions already
declared by the checkout. It does not advance exact project pins. Non-pacman
tools persist under `C:\MSYS2\GameWIPTools` in the `bin`, `tools`, `npm`,
`python`, and `powershell` subdirectories.

Repository-local helper data is disposable and confined to
`build/gamewip/{cache,state,temp,runs}`. Each operation owns a unique marked
temporary directory and removes it in `finally`. Deleting `build/` is supported.
Read-only status and quality actions recreate their required storage without
reinstalling persistent tools.

## Related pages

- @ref project_manual
- @ref project_tools
- @ref project_planning
- @ref project_library_compatibility
