# GameWIP Project Decisions

## Purpose

This file records stable decisions that affect the whole product or repository. It is not a history log, implementation checklist, test catalog, library manual, or roadmap.

- Use `vision.md` for the product identity.
- Use `roadmap.md` for ordered future work.
- Use `implementation_checklist.md` for project-level implementation status.
- Use `testing_checklist.md` for milestone verification gates.
- Use `platform_backend_contract.md` for the shared platform boundary.
- Use `versioning.md` for project, package, build, and release version policy.
- Use each library's own docs for library-specific behavior and API contracts.
- Use `contributing.md` for day-to-day GitHub and merge workflow.

## Product direction

### Building and engineering

Building must be quick to begin and useful with defaults. Engineering depth is optional and added through configuration rather than required setup. Vehicles, buildings, and destructible world structures should share a structural foundation where practical.

Damage must affect structure and function. Parts may weaken, detach, fail, expose internals, or change connected system behavior. Realism is valuable when it creates understandable engineering choices; usability takes priority when realism only adds friction.

### Simulation and rendering

Simulation uses a fixed timestep and remains separate from rendering. Behavior must not depend on render frame rate. Only systems with a demonstrated need use higher-frequency updates; ordinary gameplay does not inherit the highest simulation rate by default.

Rendering is initially a development and debugging tool. Foundational simulation, visibility, and correctness come before presentation polish.

### Development order

Implement the smallest usable version of a system before adding advanced configuration. Prefer observable, testable foundations over broad unfinished feature sets. The roadmap is the authority for order and scope.

## Toolchain and platform

- The project language standard is C++23 without compiler extensions.
- CMake and Ninja own configuration and builds.
- Windows uses the MSYS2 UCRT64 GCC toolchain for normal builds and UCRT64 Clang for static analysis.
- The repository is Windows-first, but reusable public APIs remain portable unless a platform concept is itself the contract.
- Platform-specific implementation belongs behind internal backend contracts. The shared rules are in `platform_backend_contract.md`.
- Project-owned text and public UTF-8 strings use UTF-8. Win32 backends convert at the operating-system boundary and call wide-character APIs where required.

## Repository architecture

### Ownership

- `foundation/` owns low-level reusable runtime libraries.
- `tools/` owns reusable diagnostics and validation-support libraries.
- `engine/` owns engine systems and is reviewed on its own schedule.
- `game/` owns process composition, runtime entry, and project validation runners.
- `cmake/` owns cross-project build helpers.
- `docs/doxygen/` owns generated project manuals; library manuals remain beside their libraries.
- `external/` contains pinned third-party code and is not rewritten to satisfy project style.

### Dependency direction

Lower-level reusable libraries do not depend on the game executable, validation runner, or engine. The game composes libraries; libraries do not reach upward into game policy. TestSupport supports tests but production libraries do not depend on TestSupport.

Dependencies are declared at the narrowest correct CMake visibility. A dependency is `PUBLIC` only when a public header or link interface requires it; implementation-only dependencies are `PRIVATE`.

### Runtime entry

`game/main.cpp` stays small and stable. Optional correctness tests run first, optional benchmarks run next, and the game runtime facade runs last. Disabled startup validation compiles to no-op calls so shipping builds do not retain its code or dependencies.

The executable links only targets directly used by runtime code or enabled startup modules. Repository membership is not a reason to add a link dependency.

## Reusable library standard

### Meaning of standalone

A reusable library must be consumable through a clean installed CMake package without source-tree paths or the game executable. Current libraries are not promised as independent top-level source repositories; they share the root project version, platform resolver, and build helpers.

The `GameWIP::` CMake namespace and `GameWIP` C++ namespace identify ownership and prevent collisions. Product-specific runtime policy does not belong inside a reusable library merely because that library is mainly used by GameWIP.

### Source layout

A normal library owns:

```text
<library>/
  CMakeLists.txt
  <public-header>.h
  core/
  internal/
  platform/<platform-id>/
  docs/
  cmake/
```

Only directories a library actually needs should exist. Public headers expose the supported contract. `internal/` and platform headers are implementation details. Tests may include explicitly enabled internal hooks; installed consumers may not.

### Public API shape

- Names use the `GameWIP::<Library>` namespace, with `Types` only where a library's established API groups related public types.
- `Detail` declarations may appear in a public header only when a template, macro, ABI bridge, or pImpl ownership requires them.
- Public APIs prefer standard C++ value types, `std::string` for UTF-8 text, and `std::filesystem::path` for native filesystem paths.
- Native handles, backend state, test controls, and internal library types are not public API.
- Ownership, lifetime, thread safety, blocking, failure behavior, units, and meaningful cost must be explicit when the type system does not make them obvious.
- Aliases are added only when they improve a recurring caller pattern without hiding ownership or semantics.

### Compatibility and packages

Every reusable library exports a canonical `GameWIP::` target and installs only its public CMake file set. Shared libraries hide symbols by default and export only annotated ABI roots. Validation-only definitions and hooks do not appear in installed interfaces.

Packages are pre-1.0 and use exact project-version matching. No ABI compatibility is promised across compiler toolchains or package versions. Before a public distribution, generic package names must be reconsidered in favor of one `GameWIP` package with components or globally distinctive package names.

## Naming and source style

- Files and directories use lowercase snake_case except conventional project files and product-named artifacts.
- CMake project options use `GAMEWIP_`; library-local options use the owning library prefix.
- Public CMake targets use canonical `GameWIP::Name` aliases; short build-tree targets are implementation convenience only.
- Types and concepts use PascalCase. Functions and variables use camelCase. Constants use the established `kName` form. Macros and compile definitions use uppercase snake case.
- Platform source files use `<platform-id>_<feature>.cpp`.
- Tests use `<feature>_test.cpp`; benchmarks use `<feature>_benchmark.cpp` and `BM_<Module>_<Scenario>` function names.
- Formatting is defined by `.clang-format`; repository text rules are defined by `.editorconfig` and `.gitattributes`.

## Build and configuration

- Root CMake orchestrates directories; each directory owns its targets and immediate children.
- Sources are listed explicitly. Platform backend `.cpp` discovery is the intentional exception because selection is constrained to one backend directory.
- Presets define supported build modes and explicitly disable features that do not belong in each mode.
- A feature disabled for shipping must remove its sources and dependencies, not merely switch off runtime behavior.
- External dependencies are pinned and configured centrally.
- Generated files and all build artifacts remain under the build tree.
- Runtime DLL copying accepts only the documented UCRT64 runtime, avoiding accidental `mingw64` ABI mixing.

## Validation policy

### Correctness

Correctness tests answer whether behavior is right. Modules are independently discoverable, selectable, and visible to CTest. Tests are deterministic, isolate mutable process/filesystem state, and retain actionable reports. Rare failure paths may use non-installed hooks enabled only in validation builds.

Manual dialogs, terminal interaction, and privilege-dependent scenarios are opt-in. An automated run must never wait for human input.

### Performance

Google Benchmark owns measured loops, calibration, repetitions, and statistics. Correctness tests may record diagnostic elapsed time but do not enforce machine-dependent thresholds. Benchmarks report loss/error counters when a fast result could otherwise hide dropped work.

The game executable owns Tracy enablement and the profiler client. Reusable libraries remain profiler-agnostic by default, but may add private compile-time zones when a representative capture shows meaningful opaque work that needs subdivision. Tracy never appears in public library APIs or replaces a repeatable benchmark. Disabled builds retain no project-owned Tracy instrumentation or runtime dependency.

### Coverage and analysis

Coverage is diagnostic evidence, not a substitute for contract-based tests and not an arbitrary percentage gate. Static analysis and formatting warnings fail the project-owned check. Third-party and generated sources remain excluded.

The project-level evidence and commands are maintained in `testing_checklist.md` and the generated testing/validation pages; individual test cases remain with their libraries.

## Documentation ownership

- Public header comments are compact IntelliSense contracts.
- Library Markdown explains that library's API, examples, behavior, troubleshooting, and developer validation.
- Project generated Markdown explains repository structure, presets, composition, validation architecture, packaging, CI, and extension workflows.
- Repository Markdown in `docs/` records product direction, planning, stable decisions, contribution policy, contracts, and milestone ledgers. It is not generated unless registered explicitly.
- Internal source comments explain non-obvious ownership, synchronization, invariants, platform rules, fallbacks, units, and performance constraints.

Each fact has one authoritative owner. Other documents link to it instead of copying it. Doxygen inputs are explicit public headers and selected Markdown files; recursion is disabled.

First-party documentation follows the shared heading, voice, terminology, list, example, and consumer/maintainer boundary rules in `docs/doxygen/documentation.md`.

## Repository workflow

Feature work normally uses an issue, short-lived branch, pull request, required validation, and squash merge. Branch protection must require the documented pull-request, build/test, repository, and documentation checks.

Commit and pull-request titles use `area: imperative summary`. Detailed templates, labels, merge messages, and local synchronization commands are authoritative in `contributing.md`, not duplicated here.

Automation may reconcile deterministic metadata and status. Priority, scope, security disclosure, and product decisions remain human responsibilities. Pull-request code is not executed with privileged project credentials.

## Changing a decision

Update this file only for a durable project-wide choice. Explain the current rule and its reason, remove obsolete wording, and update affected contracts or generated project pages in the same change. Put implementation work in an issue/roadmap and proof in tests rather than turning this file into a change log.
