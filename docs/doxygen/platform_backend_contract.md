@page project_platform_backend_contract Platform backend contract

GameWIP reusable libraries keep platform-specific behavior behind internal backend contracts. The public API stays portable unless the platform concept is itself part of the public contract.

## Purpose

This page defines the shared backend rules used by libraries such as FileSystem, Terminal, Logger, and future platform-dependent systems. It is the authority for backend layout, platform selection, native error translation, cleanup rules, and backend test-hook restrictions.

## Scope

This contract applies to first-party platform backends under reusable libraries. It does not document third-party vendor internals, game-specific runtime policy, or public library APIs.

## Core rules

- Keep native APIs out of installed public headers.
- Put platform-specific sources under the owning library's `platform/<platform-id>/` directory.
- Put backend-local source lists, system libraries, resources, and compile definitions in `platform.cmake`.
- Keep portable core code calling an internal backend contract instead of operating-system APIs directly.
- Translate native failures into the owning library's status or result model.
- Keep validation hooks source-tree-only and disabled in consumer builds.
- Do not add platform branches to unrelated libraries to avoid defining a backend contract.

## Platform selection

Repository platform mapping is centralized in `cmake/LibraryPlatform.cmake`. A library should use the shared mapping before adding library-local platform selection logic.

Add a new platform ID only when CMake cannot already map the target environment. Platform IDs should be stable, lowercase, and suitable for directory names.

## Required structure

A backend should use this structure:

```text
<library>/platform/<platform-id>/
  platform.cmake
  <platform-id>_<feature>.cpp
```

Single-file backends may use the library name as the feature. Split backends should use names that describe the native area they implement.

`platform.cmake` should be the only place that adds backend-local files, libraries, resources, or compile definitions to the target.

## Public API boundary

Backends must not expose native handles, native error types, platform headers, or backend configuration through installed public headers unless that is the explicit public contract of the library.

Public headers may expose portable value types, options, result types, and enums. Native state should live in internal implementation objects, backend-owned storage, or pImpl-style bridges.

## Internal contract standard

The internal backend contract should be small, explicit, and owned by the library. It should define:

- The portable operation requested by core code.
- Input ownership and lifetime expectations.
- Output ownership and lifetime expectations.
- Native failure translation rules.
- Cleanup responsibility.
- Thread-safety or process-state constraints.
- Validation seams when public APIs cannot test the behavior directly.

Portable core code should not depend on implementation details of one backend.

## Unicode and path rules

Backends that touch paths, terminal text, process arguments, or environment variables must document their encoding boundary.

FileSystem should keep `std::filesystem::path` as the native path type and use UTF-8 conversion helpers at public text boundaries. Terminal and Logger should treat public `std::string` text as UTF-8 unless the owning library documents a narrower contract.

## Error and cleanup rules

A backend must translate native errors before returning to portable core code. Native cleanup must be reliable across success, failure, and early-return paths.

When the public API returns an `IO::Types::Status` or a library result type, backend failures should preserve the most useful stable project error code available. Avoid leaking platform-only numeric codes into public behavior unless the public type explicitly stores diagnostic detail for that purpose.

## Concurrency and process state

Backends that mutate process-global state, console state, environment variables, current directory, signal handlers, handles, file locks, or logger state must document restoration and synchronization rules in the owning library's maintainer docs.

Tests that mutate process state should use approved hooks, scoped guards, or child-process isolation when ordinary public APIs cannot make the scenario deterministic.

## Internal test hooks

Backend hooks are maintainer validation tools. They are not consumer API, are not installed, and are not compatibility promises.

A hook interface must define:

- How it is enabled.
- Which internal header may be included by validation code.
- Whether hooks are one-shot, persistent, scoped, or query-only.
- How state is reset between tests.
- Which backend behavior the hook validates.

A library with approved hooks should document them in `docs/test_hooks.md`.

## Add a platform backend

When adding a backend:

- Add the backend directory and `platform.cmake`.
- Implement the existing internal platform contract.
- Keep the public API unchanged.
- Update platform selection only when needed.
- Add correctness coverage or approved hooks for backend-specific behavior.
- Update the owning library's testing and troubleshooting docs.
- Verify the relevant validation, installed-consumer, and documentation workflows.

## Allowed exceptions

A public API may expose platform-specific behavior only when the library is intentionally modeling a platform concept. In that case, the platform dependency must be documented in the public API guide, examples, package usage requirements, and compatibility notes.

Temporary backend limitations may be documented in troubleshooting or testing pages, but they should not silently weaken the public contract.

## Review checklist

Reviewers should check that:

- Native includes and handles do not leak into installed public headers.
- Portable core code uses the internal backend contract.
- Backend sources are registered only through the backend's `platform.cmake`.
- Native failures are translated into project status or result types.
- Process-global state is restored or isolated.
- Hook state cannot leak between tests.
- The owning library docs explain any supported platform-specific behavior.

## Related pages

- @ref project_structure
- @ref project_extending
- @ref project_documentation
- @ref project_library_compatibility
