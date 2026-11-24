@page project_contracts Project contracts and standards

Use these pages when a change affects ownership, compatibility, backend
behavior, documentation, versioning, or project direction. They define where
changes belong and which boundaries they must preserve.

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

Tool versions, providers, setup and update behavior, quality configuration, and
repository-local mutable storage are in @ref project_tools. Host package
installation and editor integration are in @ref project_environment_setup.

## Related pages

- @ref project_manual
- @ref project_tools
- @ref project_planning
- @ref project_library_compatibility
