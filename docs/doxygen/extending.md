@page project_extending Extending the project

This page defines how a new project concept or an expanded public boundary fits
the repository. The integration rules keep implementation, tests,
documentation, packaging, and automation consistent across their owning areas.

Library manuals still explain their own APIs, and workflow pages still explain
how to run their tools. This contract connects those areas and identifies the
required ownership and integration behavior for each kind of change.

## Scope

Use this page when adding or changing:

- A reusable library.
- A public C++ API or macro.
- An installed CMake package or exported target.
- A correctness-test or benchmark module.
- Approved internal test hooks.
- A generated Doxygen page.
- A platform backend.
- A project CMake option.
- A repository-level workflow or contract.

## Core rules

- Choose one authoritative owner before adding a rule, workflow, or contract.
- Keep reusable libraries independent of game-specific runtime policy.
- Keep platform-specific behavior behind the backend contract.
- Keep validation hooks out of installed public headers.
- Register documentation, sources, public headers, exports, tests, and package files explicitly.
- Add correctness coverage before benchmark coverage.
- Record exact verification commands in the pull request.

## Reusable library integration

Place low-level runtime libraries under `foundation/` and diagnostics or development-support libraries under `tools/`.

Use this default structure:

```text
<area>/<name>/
  CMakeLists.txt
  <name>.h
  core/
  internal/
  platform/
    <platform-id>/
      platform.cmake
      <platform-id>_<feature>.cpp
  docs/
    <name>.md
    quick_start.md
    public_api.md
    examples.md
    testing.md
    troubleshooting.md
```

Add `docs/test_hooks.md` only when the library exposes approved source-tree-only validation hooks.

A reusable library has these integration surfaces:

- The owning parent `CMakeLists.txt` includes the library.
- One canonical target and imported alias define its CMake identity.
- The target requires `cxx_std_23`, lists sources explicitly, and declares dependency visibility accurately.
- Installation contains only the public header surface and generated export headers.
- Installable libraries provide package config and exact-version files.
- Public-header compilation and clean installed-consumer validation protect the package boundary.
- Correctness tests cover the supported behavior.
- The required library documentation set describes the public contract.
- `gamewip_register_doxygen_library()` registers the public headers and manual pages.

Use `PUBLIC` dependencies only when the dependency appears in installed public headers. Use `PRIVATE` dependencies when the dependency is
implementation-only.

## Public API integration

A public API is any installed symbol, type, macro, option, result type, or supported behavior that external consumers may use.

A public API change spans these owning surfaces:

- Declarations live in installed public headers with compact local contract documentation.
- Behavior lives in portable core code or the appropriate platform backend.
- The owning library manual explains the public behavior and likely failure modes.
- Non-trivial behavior has a supported public example.
- Correctness tests cover the contract.
- Package exports and shared-library allowlists reflect the intended public symbols.

Public API documentation requirements are defined in @ref project_documentation.

## Public macro behavior

A public macro is public API.

Macro documentation and tests describe:

- What the macro evaluates.
- Whether arguments are evaluated once or may be evaluated multiple times.
- Whether the macro is compiled out under any build option.
- Any side effects, logging behavior, assertion behavior, or process behavior.
- Success paths, failure paths, disabled-build behavior, and expression-evaluation behavior.

Prefer minimal macros that forward to typed implementation functions.

## Approved internal test hooks

Add test hooks only when ordinary public API tests cannot validate behavior safely or deterministically.

A hook interface must define:

- The compile definition or option that enables it.
- The internal header validation code may include.
- Its namespace.
- Whether each hook is one-shot, persistent, scoped, or query-only.
- The reset rule required between tests.
- The validation scenarios it supports.
- Restrictions on installed-package and production use.

Libraries with approved hooks should provide `docs/test_hooks.md` using the structure in @ref project_documentation.

Failure-injection hooks must be deterministic, resettable, and narrow enough to identify the intended failure boundary. Tests must verify the same
public status, native diagnostic, payload, and cleanup invariants that a real failure promises. Installed-consumer checks must reject the enabling
compile definition so hooks cannot become accidental package API.

## Correctness-test modules

Create modules under:

```text
game/validation/tests/<module>/
```

A module must have a stable lowercase name, deterministic order, explicit sources, and a `module.cpp` registration. The registration name must match
the CMake module name.

Tests must be deterministic, isolated, and behavior-focused. Use TestSupport scopes for temporary filesystem state and inspect each scope's
construction status before using it. Inspect infrastructure result status before consuming its payload. Use child-process execution only for scenarios
that require process isolation, and evaluate its infrastructure status separately from its process outcome and exit code. Use approved hooks only when
public APIs cannot validate the scenario.

Run focused and aggregate validation before merge:

```powershell
cmake --preset test
cmake --build --preset test
.\build\test\GameWIPTests.exe --test-module=<module>
ctest --preset test
```

Detailed runner behavior is documented in @ref project_validation. Test authoring rules are documented in @ref project_testing.

## Benchmark modules

Create modules under:

```text
game/validation/benchmarks/<module>/
```

Benchmark modules must use Google Benchmark naming, keep setup outside measured loops where practical, report relevant drop or error counters, and
avoid correctness assertions based on timing.

Verify registration before collecting results:

```powershell
.\gamewip.bat benchmark dry-run
```

Benchmark authoring rules are documented in @ref project_benchmarking.

## Documentation ownership

Choose the owner before writing the page.

| Documentation type | Owner |
| --- | --- |
| Public API usage, examples, behavior, failure modes, and troubleshooting | Owning library `docs/` directory |
| Build, validation, testing, benchmark, profiling, coverage, static-analysis, documentation, and automation workflows | `docs/doxygen/` |
| Vision, roadmap, decisions, versioning, contribution workflow, and milestone evidence | `docs/` |
| Public API reference | Installed public headers |
| Private implementation notes | Internal headers and source files |

Generated Doxygen pages must be registered explicitly. Library pages are registered by the owning library target. Project pages are registered in
`cmake/GameWIPDocumentation.cmake`.

Documentation changes must follow @ref project_documentation.

## Project workflow and contract pages

A workflow page documents a repeatable action. A contract page documents a rule that keeps the repository consistent.

A workflow or contract page has the following integration properties:

- Each page has one authoritative owner.
- Generated manual pages live under `docs/doxygen/`; long-form planning and policy records live under `docs/`.
- Page structure follows @ref project_documentation.
- Generated pages are registered explicitly and linked from the nearest relevant index.
- Workflow pages provide copy-paste commands and explain their outputs and failure behavior.
- Contract pages state their review criteria as repository invariants.

## Platform backend integration

Platform backend structure and behavior are owned by @ref project_platform_backend_contract.

A backend integrates through these boundaries:

- The backend implements the owning internal platform contract without changing the portable public API.
- `platform.cmake` owns backend-local sources, resources, libraries, and compile definitions.
- Native failures translate into the owning library's status or result model.
- Installed public headers contain no native handles or platform-specific types.
- Tests or approved hooks cover behavior that public API tests cannot validate directly.
- The owning library's testing and troubleshooting pages describe supported backend behavior and limitations.

## CMake option integration

Project composition options use the `GAMEWIP_` prefix and are defined in `cmake/GameWIPOptions.cmake`. Maintainer-facing CMake helper conventions are
documented in @ref project_cmake_infrastructure.

A project option has these owning surfaces:

- Its name uses the correct ownership prefix, and its default lives in the owning CMake file.
- Relevant presets set intentional values rather than relying on accidental defaults.
- @ref project_build describes project-facing controls; the owning library manual describes consumer-facing local controls.
- Validation covers meaningful enabled and disabled behavior.

## Packaging behavior

Package changes must preserve the installed-consumer boundary.

Package behavior spans:

- Install rules.
- Exported targets.
- Package config files.
- Version files when necessary.
- `find_dependency()` calls for public transitive dependencies.
- Clean installed-consumer validation.
- Public-header compile checks.
- Shared-library export allowlists when applicable.
- @ref project_library_compatibility.

## Final verification

Run the preset that matches the change and record the exact commands in the pull request.

Common verification paths:

```powershell
cmake --preset test
cmake --build --preset test
ctest --preset test
```

```powershell
cmake --preset docs
cmake --build --preset docs
```

```powershell
.\gamewip.bat benchmark dry-run
```

A generic statement such as `tested` is not sufficient verification evidence.
