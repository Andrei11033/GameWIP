@page project_extending Extending the project

Use this checklist when a change adds a new project concept or expands an
existing public boundary. It keeps implementation, tests, documentation,
packaging, and automation moving together instead of leaving follow-up work
hidden in another part of the repository.

Library manuals still explain their own APIs, and workflow pages still explain
how to run their tools. This checklist connects those areas and points to the
details that need review for each kind of change.

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

## Add a reusable library

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

Required integration:

- Add the library to the owning parent `CMakeLists.txt`.
- Create one canonical target and imported alias.
- Require `cxx_std_23`.
- List sources explicitly.
- Declare dependency visibility accurately.
- Install only the public header surface and generated export headers.
- Add package config and exact version files when the library is installable.
- Add public-header compile checks and clean installed-consumer validation.
- Add correctness tests.
- Add the required library documentation set.
- Register public headers and docs with `gamewip_register_doxygen_library()`.

Use `PUBLIC` dependencies only when the dependency appears in installed public headers. Use `PRIVATE` dependencies when the dependency is
implementation-only.

## Add a public API

A public API is any installed symbol, type, macro, option, result type, or supported behavior that external consumers may use.

Required work:

- Add the declaration to an installed public header.
- Implement the behavior in portable core code or the appropriate platform backend.
- Add compact public-header documentation.
- Update the owning library's public API manual.
- Add an example when the behavior is non-trivial.
- Add correctness tests.
- Update troubleshooting documentation when the API introduces likely failure modes.
- Update package exports or shared-library allowlists when applicable.

Public API documentation requirements are defined in @ref project_documentation.

## Add or change a public macro

A public macro is public API.

Document and test:

- What the macro evaluates.
- Whether arguments are evaluated once or may be evaluated multiple times.
- Whether the macro is compiled out under any build option.
- Any side effects, logging behavior, assertion behavior, or process behavior.
- Success paths, failure paths, disabled-build behavior, and expression-evaluation behavior.

Prefer minimal macros that forward to typed implementation functions.

## Add approved internal test hooks

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

## Add a correctness-test module

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

## Add a benchmark module

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

## Add documentation

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

## Add a project workflow or contract page

A workflow page documents a repeatable action. A contract page documents a rule that keeps the repository consistent.

When adding one:

- Choose one authoritative owner.
- Put generated manual pages under `docs/doxygen/`.
- Put long-form planning and policy records under `docs/`.
- Use the standard page structure from @ref project_documentation.
- Register generated pages explicitly.
- Link from the nearest relevant index page.
- Include copy-paste commands for workflows.
- Include review guidance for contracts.

## Add a platform backend

Platform backend structure and behavior are owned by @ref project_platform_backend_contract.

When adding a backend:

- Implement the owning internal platform contract.
- Keep the portable public API unchanged.
- Add backend-local sources, resources, libraries, or compile definitions in `platform.cmake`.
- Translate native failures into the owning library's status or result model.
- Keep native handles and platform-specific types out of installed public headers.
- Add tests or approved hooks when public behavior cannot be validated directly.
- Update the owning library's testing and troubleshooting docs.

## Add or change a CMake option

Project composition options use the `GAMEWIP_` prefix and are defined in `cmake/GameWIPOptions.cmake`. Maintainer-facing CMake helper conventions are
documented in @ref project_cmake_infrastructure.

When adding or changing an option:

- Choose the correct ownership prefix.
- Define the default in the owning CMake file.
- Set intentional values in relevant presets.
- Document project-facing controls under @ref project_build.
- Document consumer-facing library-local controls in the owning library manual.
- Add validation coverage for meaningful enabled and disabled behavior.

## Add or change packaging behavior

Package changes must preserve the installed-consumer boundary.

Update:

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
