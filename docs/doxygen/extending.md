@page project_extending Extending the project

This page explains how a new project concept or an expanded public boundary fits
into the repository. It connects implementation, tests, documentation,
packaging, and automation across their owning areas.

Library manuals explain their own APIs, and workflow pages explain how to run
their tools. This page connects those areas and describes the ownership and
integration work that accompanies each kind of change.

## Scope

This page applies to reusable libraries, public C++ APIs and macros, installed
CMake packages and exported targets, correctness and benchmark modules,
approved internal test hooks, generated Doxygen pages, platform backends,
project CMake options, and repository-level workflows or contracts.

## Core rules

Each rule, workflow, or contract has one authoritative owner. Reusable libraries
must stay independent of game-specific runtime policy, platform behavior must
stay behind the backend contract, and validation hooks must stay out of
installed public headers. Documentation, sources, public headers, exports,
tests, and package files must be registered explicitly. Correctness coverage
must come before benchmark coverage, and pull requests must record the
verification commands that were run.

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

A reusable library must have all of the following:

- Its parent `CMakeLists.txt` includes it.
- One canonical target and imported alias define its CMake identity.
- The target requires `cxx_std_23`, lists sources explicitly, and declares
  dependency visibility accurately.
- Installation contains only its public headers and generated export headers.
- Installable libraries provide package config and exact-version files.
- Public-header compilation and clean installed-consumer validation protect the
  package boundary.
- Correctness tests cover the supported behavior.
- The required manual pages describe the public contract.
- `gamewip_register_doxygen_library()` registers the public headers and manual
  pages.

Use `PUBLIC` dependencies when a dependency appears in installed public headers.
Use `PRIVATE` dependencies for implementation-only requirements.

## Public API integration

A public API is any installed symbol, type, macro, option, result type, or supported behavior that external consumers may use.

A public API change must be reflected in all relevant parts of the project:

- Declarations live in installed public headers with compact local contract
  documentation.
- Behavior lives in portable core code or the appropriate backend.
- The owning library manual explains public behavior and likely failure modes.
- Non-trivial behavior has a supported example.
- Correctness tests cover the contract.
- Package exports and shared-library allowlists reflect the intended symbols.

Public API documentation requirements are defined in @ref project_documentation.

## Public macro behavior

A public macro is public API.

Macro documentation and tests must describe:

- What the macro evaluates.
- Whether arguments are evaluated once or may be evaluated multiple times.
- Whether the macro is compiled out under any build option.
- Any side effects, logging behavior, assertion behavior, or process behavior.
- Success paths, failure paths, disabled-build behavior, and
  expression-evaluation behavior.

Prefer minimal macros that forward to typed implementation functions.

## Approved internal test hooks

Add test hooks only when ordinary public API tests cannot validate behavior safely or deterministically.

Each hook interface must define:

- The compile definition or option that enables it.
- Which internal headers validation code may include.
- Its namespace.
- Whether each hook is one-shot, persistent, scoped, or query-only.
- The reset rule required between tests.
- The validation scenarios it supports.
- Restrictions on installed-package and production use.

Libraries with approved hooks must provide `docs/test_hooks.md` using the structure in @ref project_documentation.

Failure-injection hooks must be deterministic, resettable, and narrow enough to
identify the intended failure boundary. Tests must verify the public status,
native diagnostic, payload, and cleanup invariants promised by a real failure.
Installed-consumer checks must reject the enabling compile definition so hooks
cannot become accidental package API.

## Correctness-test modules

Create modules under:

```text
game/validation/tests/<module>/
```

A module must have:

- A stable lowercase name.
- A deterministic order.
- Explicit sources.
- A `module.cpp` registration whose name matches the CMake module name.

Tests must:

- Be deterministic, isolated, and behavior-focused.
- Use TestSupport scopes for temporary filesystem state and check construction
  status before using a scope.
- Check infrastructure result status before reading its payload.
- Reserve child processes for scenarios that need process isolation and evaluate
  their infrastructure status separately from process outcome and exit code.
- Use approved hooks only when public APIs cannot validate the scenario.

Run focused and aggregate validation before merge:

```powershell
cmake --preset test
cmake --build --preset test
.\build\test\GameWIPTests.exe --test-module=<module>
ctest --preset test
```

Runner behavior is documented in @ref project_validation. Test authoring is
documented in @ref project_testing.

## Benchmark modules

Create modules under:

```text
game/validation/benchmarks/<module>/
```

Benchmark modules must use Google Benchmark naming, keep setup outside measured
loops where practical, report relevant drop or error counters, and avoid
correctness assertions based on timing.

Verify registration before collecting results:

```powershell
.\gamewip.bat benchmark dry-run
```

Benchmark authoring rules are documented in @ref project_benchmarking.

## Documentation ownership

Choose the owner before writing a page.

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

A workflow page documents a repeatable action. A contract page documents a rule
that keeps the repository consistent.

A workflow or contract page has the following integration properties:

- Each page has one authoritative owner.
- Generated manual pages live under `docs/doxygen/`; long-form planning and
  policy records live under `docs/`.
- Page structure follows @ref project_documentation.
- Generated pages are registered explicitly and linked from the nearest relevant
  index.
- Workflow pages provide copy-paste commands and explain their outputs and
  failure behavior.
- Contract pages state their review criteria as repository invariants.

## Platform backend integration

Platform backend structure and behavior are owned by @ref project_platform_backend_contract.

A backend integrates through these boundaries:

- The backend implements the owning internal platform contract without changing
  the portable public API.
- `platform.cmake` owns backend-local sources, resources, libraries, and compile
  definitions.
- Native failures translate into the owning library's status or result model.
- Installed public headers contain no native handles or platform-specific types.
- Tests or approved hooks cover behavior that public API tests cannot validate
  directly.
- The owning library's testing and troubleshooting pages describe supported
  backend behavior and limitations.

## CMake option integration

Project composition options use the `GAMEWIP_` prefix and are defined in `cmake/GameWIPOptions.cmake`. Maintainer-facing CMake helper conventions are
documented in @ref project_cmake_infrastructure.

A project option must be reflected in all of the following:

- Its name uses the correct ownership prefix, and its default lives in the
  owning CMake file.
- Relevant presets set intentional values rather than relying on accidental
  defaults.
- @ref project_build describes project-facing controls; the owning library
  manual describes consumer-facing local controls.
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

Use the workflow page that matches the change and record the exact commands in
the pull request. @ref project_testing, @ref project_documentation, and @ref
project_benchmarking describe the relevant validation paths. A generic
statement such as `tested` is not sufficient verification evidence.
