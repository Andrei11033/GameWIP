@page project_structure Project structure and runtime flow

GameWIP is a product repository built around reusable C++ libraries, project-level build infrastructure, validation tooling, generated documentation, and a small executable shell. Project code owns composition and runtime policy. Reusable libraries own their public contracts, implementations, platform backends, package boundaries, validation coverage, and manuals.

## Purpose

This page defines where code belongs and which direction dependencies may flow. It is the repository map, not a replacement for library manuals, workflow pages, or the platform backend contract.

## Repository map

| Path | Ownership |
| --- | --- |
| `foundation/` | Low-level reusable libraries such as IO, FileSystem, and Terminal. |
| `tools/` | Diagnostics, assertions, logging, validation support, and development tooling libraries. |
| `engine/` | Engine systems developed and reviewed separately from the reusable foundation and tool libraries. |
| `game/` | Executable entry point, runtime facade, startup validation wiring, validation runners, and game-facing integration. |
| `cmake/` | Repository-wide build, platform, validation, coverage, documentation, packaging, and analysis helpers. |
| `docs/doxygen/` | Generated project manual pages. |
| `docs/` | Vision, roadmap, decisions, versioning, and contributor workflow records. |
| `.github/` | Pull-request policy, CI, documentation publishing, and repository automation. |
| `external/` | Pinned third-party dependencies. |

Build output belongs under build directories selected by CMake presets.

## Dependency direction

The reusable dependency flow is:

```text
IO
  -> FileSystem
  -> Terminal

IO + FileSystem + Terminal
  -> Logger

Logger
  -> Assert runtime

TestSupport
  -> validation modules

Reusable libraries + optional validation modules
  -> GameWIP executable
```

Arrows mean “is consumed by.” Lower-level libraries must not depend on the game executable, runtime facade, validation runner, or benchmark runner.

Validation code may use libraries and approved internal hooks. Installed consumers must not see internal headers, test-hook headers, source-tree-only helper targets, or validation-only compile definitions.

## Library ownership

Each reusable library owns:

- Public headers and namespace.
- Public CMake target and package boundary.
- Core implementation and internal headers.
- Platform backend contract and backend sources.
- Correctness tests and benchmark coverage when applicable.
- Library manual, public API guide, examples, testing guide, troubleshooting notes, and approved test-hook docs when applicable.

A reusable library should not require the game executable to compile, test, install, or be consumed from a clean external CMake project.

## Public and internal boundaries

Installation exports only the public header file set and any generated shared-library export headers required by the package.

These are not consumer API:

- Internal headers.
- Platform backend headers.
- Test-hook headers.
- Validation-only helpers.
- Private `Detail` implementation helpers.
- Backend-native handles and platform-specific types.
- Source-tree-only CMake helpers.

Public manuals should not require readers to understand `Detail` namespaces. Maintainer documentation may mention them when explaining implementation boundaries, validation hooks, or backend contracts.

## Installed-package boundary

A library is standalone at the installed-package boundary when a clean external CMake project can use it through `find_package()` and the canonical imported target without source-tree paths or game executable dependencies.

In the pre-1.0 repository, standalone libraries may still share repository CMake helpers, the root project version, platform-selection logic, validation infrastructure, and documentation infrastructure.

`GameWIP::` imported targets are project ownership markers. They do not imply game-runtime coupling.

Package rules are documented in @ref project_library_compatibility.

## Platform backend boundary

Platform-specific implementation belongs behind the owning library's internal backend contract. Backend file layout, native error translation, Unicode/path behavior, cleanup rules, and test-hook restrictions are documented in @ref project_platform_backend_contract.

## Validation ownership

Validation code belongs under `game/validation/`.

Correctness tests live under:

```text
game/validation/tests/<module>/
```

Benchmarks live under:

```text
game/validation/benchmarks/<module>/
```

The validation runner owns command-line behavior, module selection, report generation, manual checks, child-process scenarios, and aggregate exit-code policy. Individual modules own their test cases and module-specific options.

See @ref project_validation, @ref project_testing, and @ref project_benchmarking.

## Executable integration

The `game/` tree owns executable composition, startup validation wiring, standalone validation runners, and the current runtime facade. `main.cpp` should remain a small process entry point that delegates runtime work behind `GameWIP::Game::run()`.

Executable layout, startup sequencing, generated version metadata, and source-comment expectations for `game/` files are documented in @ref project_game_executable.

## Documentation ownership

Generated workflow and contract pages are owned by `docs/doxygen/`. Product planning and policy records are owned by `docs/`. Library manuals are owned by each library's `docs/` directory.

See @ref project_documentation and @ref project_planning.

## Related pages

- @ref project_build
- @ref project_game_executable
- @ref project_testing
- @ref project_validation
- @ref project_extending
- @ref project_documentation
- @ref project_library_compatibility
- @ref project_platform_backend_contract
- @ref project_planning
