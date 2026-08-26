@page project_structure Project structure and runtime flow

GameWIP is a product repository built around reusable C++ libraries, project-level build infrastructure, validation tooling, generated documentation,
and a small executable shell. Project code owns composition and runtime policy. Reusable libraries own their public contracts, implementations,
platform backends, package boundaries, validation coverage, and manuals.

Use this map to decide where code belongs and which dependency directions are
valid. It shows the system-level relationships; library manuals and workflow
pages provide the behavior inside each box.

## Repository map

| Path | Ownership |
| --- | --- |
| `foundation/` | Internal Base mechanisms and low-level reusable libraries such as Unicode, IO, FileSystem, and Terminal. |
| `tools/` | Diagnostics, assertions, logging, validation support, and development tooling libraries. |
| `engine/` | The documented Window library plus provisional Input and Action code. Preserved WindowManager code is currently outside the build. |
| `game/` | Executable entry point, runtime facade, startup validation wiring, validation runners, and game-facing integration. |
| `cmake/` | Repository-wide build, platform, validation, coverage, documentation, packaging, and analysis helpers. |
| `config/quality/` | Explicit formatter and linter policy consumed by the project quality helper. |
| `docs/doxygen/` | Generated project manual pages. |
| `docs/` | Vision, roadmap, decisions, versioning, and contributor workflow records. |
| `.github/` | Pull-request policy, CI, documentation publishing, and repository automation. |
| `external/` | Pinned third-party dependencies. |

Build output belongs under build directories selected by CMake presets.

## Dependency direction

The reusable dependency flow is:

```text
Base (internal mechanisms only)
  -> selected library implementations

Unicode
  -> IO
  -> Terminal

Unicode
  -> TestSupport

IO
  -> FileSystem
  -> Terminal

IO + FileSystem
  -> Window

IO + FileSystem + Terminal
  -> Logger

Logger
  -> Assert runtime

TestSupport
  -> validation modules

Reusable libraries + optional validation modules
  -> GameWIP executable
```

Arrows mean "is consumed by." Lower-level libraries must not depend on the game executable, runtime facade, validation runner, or benchmark runner.

Unicode has no reusable/public GameWIP library dependency. It owns platform-neutral scalar, encoding, conversion, and grapheme algorithms without
taking filesystem-path, terminal, rendering, or editing policy. Its implementation may consume narrow internal Base mechanisms such as checked
arithmetic without exposing Base through the installed package. IO uses Unicode only for strict text-boundary validation; IO byte primitives remain
encoding-agnostic.

## Internal foundation infrastructure

@ref internal_base documents the source-tree-only `GameWIP::Base` target. Base shares checked arithmetic and typed Win32 procedure lookup without
owning Unicode, logging, errors, filesystem, window, cursor, DPI, time, or simulation policy. It is neither installed nor listed as a supported
consumer library, and an installed target that exposes Base is invalid.

TestSupport is a standalone validation library and installed package. It may use
Unicode privately for its documented UTF-8 boundary contracts, but it must not
acquire IO, FileSystem, Terminal, Window, Logger, Assert, engine, or other
higher-level GameWIP dependencies. Validation modules depend on TestSupport, not
the reverse. This keeps `find_package(TestSupport)` independently usable without
giving its portable result contracts unrelated higher-level dependencies.

## Engine-system status

Window is the supported, documented engine library and participates in package,
public-header, correctness, benchmark, and manual validation. Input and Action
are compiled source-tree prototypes whose public contracts and package
boundaries are not yet stable; they are intentionally absent from the reusable
library manual. WindowManager targets a retired Window surface and is preserved
for a later coordination-layer migration, but it is not currently compiled.

Do not treat a header under `engine/input`, `engine/action`, or
`engine/window_manager` as a supported installed API. Their completion gates are
tracked in the project roadmap.

Validation code may use libraries and approved internal hooks. Installed consumers must not see internal headers, test-hook headers, source-tree-only
helper targets, or validation-only internal compile definitions.

## Library ownership

Each reusable library owns:

- Public headers and namespace.
- Public CMake target and package boundary.
- Core implementation and internal headers.
- Platform backend contract and backend sources.
- Correctness tests and benchmark coverage when applicable.
- Library manual, public API guide, examples, testing guide, troubleshooting notes, and approved test-hook docs when applicable.

A reusable library must not require the game executable to compile, test, install, or be consumed from a clean external CMake project.

## Source organization

File organization follows responsibility, but public, implementation, internal, and validation boundaries solve different problems and are not
required to mirror one another.

- `Library::Types` organizes what public concepts are. Real conceptual families may live under `Types::<Domain>` while shared/core types remain
  directly under `Types`.
- Public headers organize what consumers need to include and which public concepts have independent ownership. Do not split a header only because it
  became long.
- `.cpp` files organize implementation responsibility. Split a translation unit when a coherent subsystem can be owned, maintained, and built
  independently without manufacturing a broad private API merely to move lines around.
- Internal headers own private contracts only when multiple implementation files genuinely share them or when a private subsystem becomes materially
  clearer. Do not create internal headers solely to make source files smaller.
- Correctness-test sources organize behavioral domains inside one logical module. A module may use focused private case fragments or compile several
  focused case translation units while retaining one registration, one options interface, and one reporting contract. Choose the form that avoids
  duplicated fixtures and artificial private interfaces.

There is no line-count or file-size quota. Size is a signal that ownership may have become unclear, not an automatic split trigger. Prefer a small
number of coherent responsibility files over mechanical one-type/one-function decomposition. A useful review question is whether a maintainer can
identify where behavior belongs without searching a monolithic translation unit.

A library target's `STATIC` or `SHARED` form follows runtime ownership, ABI, and process-coordination requirements rather than repository symmetry.
Use one shared runtime when the contract requires process-wide coordination to be unique across consuming modules; do not convert otherwise
independent libraries to shared form merely so neighboring targets look alike.

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

Public manuals must not require readers to understand `Detail` namespaces. Maintainer documentation may mention them when explaining implementation
boundaries, validation hooks, or backend contracts.

## Installed-package boundary

A library is standalone at the installed-package boundary when a clean external CMake project can use it through `find_package()` and the canonical
imported target without source-tree paths or game executable dependencies.

In the pre-1.0 repository, standalone libraries may still share repository CMake helpers, the root project version, platform-selection logic,
validation infrastructure, and documentation infrastructure.

`GameWIP::` imported targets are project ownership markers. They do not imply game-runtime coupling.

Package rules are documented in @ref project_library_compatibility.

## Platform backend boundary

Platform-specific implementation belongs behind the owning library's internal backend contract. Backend file layout, native error translation,
Unicode/path behavior, cleanup rules, and test-hook restrictions are documented in @ref project_platform_backend_contract.

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

The validation runner owns command-line behavior, module selection, report generation, manual checks, child-process scenarios, and aggregate exit-code
policy. Individual modules own their test cases and module-specific options.

See @ref project_validation, @ref project_testing, and @ref project_benchmarking.

## Executable integration

The `game/` tree owns executable composition, startup validation wiring, standalone validation runners, and the runtime facade. `main.cpp` must remain
a small process entry point that delegates runtime work behind `GameWIP::Game::run()`.

Executable layout, startup sequencing, generated version metadata, and source-comment expectations for `game/` files are documented in @ref
project_game_executable.

## Documentation ownership

Generated workflow and contract pages are owned by `docs/doxygen/`. Product planning and policy records are owned by `docs/`. Library manuals are
owned by each library's `docs/` directory.

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
