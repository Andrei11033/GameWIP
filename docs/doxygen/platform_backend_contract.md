@page project_platform_backend_contract Platform backend contract

GameWIP reusable libraries keep platform-specific behavior behind internal
backend contracts; the public API stays portable unless the platform concept is
itself part of the public contract.

FileSystem, Terminal, Logger, Assert, TestSupport, Input, Desktop, and future
platform-dependent components use the same backend shape. This page covers
layout, platform selection, error translation, cleanup, and test seams so each
library has a consistent boundary.

## Scope

This contract applies to first-party platform backends under reusable
libraries. It does not document third-party vendor internals, game-specific
runtime policy, or public library APIs.

## Core rules

Native APIs should stay out of installed public headers. An intentional native
interop header may expose them when the public contract documents that boundary.
Platform-specific sources must live under the owning library's
`platform/<platform-id>/` directory. Portable core code must call the internal
backend contract instead of operating-system APIs directly. Native failures
must be translated into the owning library's status or result model. Validation
hooks must remain source-tree-only and disabled in consumer builds. An unrelated
library must not gain a platform branch just to avoid defining its own backend
contract.

Capability fallback follows @ref project_public_api_contract. Do not add empty
placeholder backends; a missing selected backend is a configuration error.

## Platform selection

Repository platform mapping is centralized in `cmake/GameWIPPlatform.cmake`:
Windows maps to `win32`, Linux to `linux`, and Darwin to `macos`. Every
platform-aware target calls
`gamewip_add_platform_backend(TARGET <target> ROOT <platform-root>)` from
`platform/CMakeLists.txt`. Configuration fails when the selected backend
directory or its `CMakeLists.txt` is absent. An externally
provided `GAMEWIP_PLATFORM_ID` must be a lowercase backend identifier made from
letters, digits, and single hyphens; the validator remains extensible and does
not claim that a valid identifier is implemented by every library.

Add a new platform ID only when CMake cannot already map the target environment.
Platform IDs must be stable, lowercase, and suitable for directory names.

## Required structure

Folder roles are defined in @ref project_structure. CMake selection and source
registration are defined in @ref project_cmake_infrastructure. Backend source
names describe the native area they implement.

## Public API boundary

Backends must not expose native handles, native error types, platform headers, or
backend configuration through installed public headers unless that is the
library's explicit public contract.

Public headers may expose portable value types, options, result types, and
enums. Native state must live in internal implementation objects,
backend-owned storage, or pImpl-style bridges.

## Internal contract standard

The internal backend contract must be small, explicit, and owned by the library.
It must define the portable operation requested by core code, input and output
ownership and lifetime, native failure translation, cleanup responsibility,
thread-safety or process-state constraints, and validation seams for behavior
that public APIs cannot test directly.

Portable core code must not depend on implementation details of one backend.

## Unicode and path rules

Backends that touch paths, terminal text, process arguments, or environment
variables must document their encoding boundary.

FileSystem must keep `std::filesystem::path` as the native path type and use
UTF-8 conversion helpers at public text boundaries. Terminal and Logger must
treat public `std::string` text as UTF-8 unless the owning library documents a
narrower contract.

## Error and cleanup rules

A backend must translate native errors before returning to portable core code.
Native cleanup must remain reliable across success, failure, and early-return
paths.

When the public API returns a `GameWIP::Status::Types::Status` or a result
type, backend failures must preserve the most useful stable project error code.
Platform-only numeric codes stay out of public behavior unless the public type
explicitly stores diagnostic detail for that purpose.

TestSupport backends return the shared status through `Result<T>`, with
portable codes for decisions and optional native details for diagnostics.
Child-process backends keep infrastructure failure separate from process
outcome and exact exit code. An approved deterministic hook may exercise the
public `Unsupported` category, but it does not substitute for a real platform
backend.

## Concurrency and process state

Backends that mutate process-global state, console state, environment variables,
the current directory, signal handlers, handles, file locks, or logger state
must document restoration and synchronization rules in the owning library's
maintainer docs.

Tests that mutate process state must use approved hooks, scoped guards, or
child-process isolation when ordinary public APIs cannot make the scenario
deterministic.

## Internal test hooks

Backend hooks are maintainer validation tools. They are not consumer API, are
not installed, and are not compatibility promises.

Each hook interface must define:

- How it is enabled.
- Which internal header may be included by validation code.
- Whether hooks are one-shot, persistent, scoped, or query-only.
- How state is reset between tests.
- Which backend behavior the hook validates.

Failure-injection hooks must preserve the public status and cleanup invariants of
real backend failures. Their source-tree compile definition must not escape into
installed targets.

A library with approved hooks must document them in `docs/test_hooks.md`.

## Add a platform backend

When adding a backend:

- Add the backend directory and its `CMakeLists.txt`.
- Implement the existing internal platform contract.
- Keep the public API unchanged unless the platform concept is deliberately
  public.
- Add correctness coverage or approved hooks for backend-specific behavior.
- Update platform selection only when needed.
- Update the owning library's testing and troubleshooting docs.
- Run the relevant validation, installed-consumer, and documentation workflows.

Unicode and IO are portable foundation libraries and do not receive artificial
backend directories. Action and WindowManager are unbuilt reference
sources outside the supported backend-completeness contract.

## Allowed exceptions

A public API may expose platform-specific behavior when the library is
intentionally modeling a platform concept. The platform dependency then belongs
in the public API guide, examples, package usage requirements, and compatibility
notes.

Temporary backend limitations may be documented in troubleshooting or testing
pages, but they must not silently weaken the public contract.

## Review invariants

Backend review checks that:

- Native includes and handles stay out of portable installed headers; documented
  native connectors may expose the required interop types.
- Portable core code uses the internal contract.
- Backend sources are registered only through the backend's `CMakeLists.txt`.
- Native failures are translated into the portable result/status model.
- Process-global state is restored or isolated after each operation.
- Test-hook state is reset between tests.
- The owning library documents supported platform-specific behavior.

## Related pages

- @ref project_structure
- @ref project_extending
- @ref project_documentation
- @ref project_library_compatibility
