@page project_contracts Project contracts and standards

Project contracts define the rules that keep the repository consistent. Update them deliberately when ownership, compatibility, backend behavior, documentation policy, versioning, or architectural direction changes.

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

## Related pages

- @ref project_manual
- @ref project_planning
- @ref project_library_compatibility
