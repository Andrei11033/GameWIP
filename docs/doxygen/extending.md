@page project_extending Extending the project

This page is the repository-level checklist for adding maintainable code, tests, benchmarks, documentation, and platform support. Library manuals remain responsible for teaching their own APIs.

## Add a reusable library

1. Choose `foundation/<name>` for foundational runtime behavior or `tools/<name>` for diagnostics and development support.
2. Add `add_subdirectory(<name>)` to `foundation/CMakeLists.txt` or `tools/CMakeLists.txt` so the root composition includes the library.
3. Give the library one public include prefix and C++ namespace. Keep implementation headers under `<name>/internal/` and platform sources under `<name>/platform/<platform-id>/`.
4. Create a target and canonical alias such as `add_library(GameWIP::Example ALIAS Example)`. Require `cxx_std_23` and list sources explicitly except for platform backend discovery.
5. Put every supported header in a public CMake `FILE_SET`. Do not put internal or test-hook headers in that set.
6. Declare dependency visibility accurately: `PUBLIC` when a public header requires the dependency, `PRIVATE` when only the implementation does.
7. For shared libraries, generate an export header, annotate required ABI declarations, hide other symbols, and add a reviewed allowlist under `cmake/export_allowlists/` plus registration in `game/validation/tests/CMakeLists.txt`.
8. Add install/export rules, a config package, an exact pre-1.0 version file, and `find_dependency()` calls for transitive public packages.
9. Add an isolated public-header compile check and extend the clean consumer project under `game/validation/installed_consumer/`.
10. Add a library landing page and focused manual pages beside the library, then register only public headers and those docs with `gamewip_register_doxygen_library()`.
11. Add correctness coverage before performance benchmarks. Update project policy docs only when the new library changes project-wide composition or standards.

Use `GameWIP` in CMake target namespaces, generated export macros, and project integration where ownership must be unambiguous. Do not put product-specific runtime policy into a reusable library. A standalone library may still use the `GameWIP` namespace; standalone means independently consumable, not anonymously owned.

## Add a public API

Add the declaration to an installed public header and implementation to core or platform sources. Document purpose, parameters, return value, ownership, lifetime, thread safety, blocking behavior, failure behavior, and important cost. Prefer portable standard-library value types. Use `std::string` for UTF-8 text and `std::filesystem::path` for native filesystem paths.

Avoid exposing platform handles, test controls, implementation state, or another library's internal types. Use pImpl or an out-of-line bridge when a public type needs private storage. Add behavior tests and update the owning library manual. If a shared-library symbol is new, update its export allowlist deliberately.

## Add a correctness-test module

Create `game/validation/tests/<module>/CMakeLists.txt`, `module.cpp`, and focused test sources. Register it with:

```cmake
gamewip_add_test_module(
    NAME example
    SOURCES
        module.cpp
        example_test.cpp
    LINK_LIBRARIES
        Example
        TestSupport
)
```

The CMake helper does not own runtime order. In `module.cpp`, register the stable name, order, and adapter explicitly:

```cpp
const GameWIP::Validation::Tests::Registration registration({
    .name = "example",
    .order = 70,
    .run = run,
});
```

The adapter maps `ModuleInvocation` policy into its suite-specific options. Tests must be deterministic, isolate filesystem state with TestSupport scopes, return meaningful exit codes, and avoid performance thresholds. Add child-argument ownership only for scenarios that must launch the test executable recursively. The parent directory discovers the module automatically; no central source list should change.

Run the module directly and through CTest:

```powershell
cmake --preset validation
cmake --build --preset validation
.\build\validation\GameWIPTests.exe --test-module=example
ctest --preset validation
```

## Add a benchmark module

Create `game/validation/benchmarks/<module>/CMakeLists.txt` and benchmark sources, then call:

```cmake
gamewip_add_benchmark_module(
    NAME example
    SOURCES example_benchmark.cpp
    LINK_LIBRARIES Example
)
```

Use Google Benchmark iteration and naming (`BM_<Module>_<Scenario>`), keep setup outside measured loops, expose drop/error counters, and never turn machine-dependent timing into a correctness gate. Verify registration with `GameWIPBenchmarks.exe --benchmark_dry_run`; collect results from the optimized `benchmark` preset.

## Add documentation

Choose one authoritative owner:

- Put API usage, examples, behavior, failure modes, and troubleshooting in the owning library's `docs/` directory.
- Put presets, repository structure, integration, validation architecture, CI, packaging, and cross-library policy in `docs/doxygen/`.
- Put stable product/architecture choices in `docs/decisions.md` or a dedicated contract.
- Put ordered future work in `docs/roadmap.md`; use checklists only for milestone evidence.

Give generated pages stable lowercase snake_case IDs. Register project pages explicitly in `cmake/GameWIPDocumentation.cmake`; library directories are registered by their own target. Link to the authoritative page instead of copying its rules. Build `docs` and require an empty Doxygen warning log.

## Add a platform backend

Add `<library>/platform/<platform-id>/` with one or more `<platform-id>_<feature>.cpp` files. A single-file backend may use the library name as the feature, such as `win32_terminal.cpp`; a split backend uses descriptive feature names. Implement the existing internal platform contract without branching the portable public API. Use `platform.cmake` only for backend-local sources, system libraries, generated resources, or compile definitions. Then add the platform ID mapping in `cmake/LibraryPlatform.cmake` if CMake cannot already resolve it.

A new backend must compile the public-header checks, library correctness suite, installed consumer, and documentation. Platform-specific behavior belongs behind the owning internal contract; do not scatter new `#ifdef` branches through portable core files.

## Add or change a CMake option

Project composition options use the `GAMEWIP_` prefix and define their defaults in `cmake/GameWIPOptions.cmake`. Set intentional project values in every relevant preset and document user-facing controls under @ref project_build.

Library behavior options use the owning library prefix and define their defaults in that library's `CMakeLists.txt`. Top-level project composition may map a `GAMEWIP_` option to a library option explicitly, but the standalone library must not require the project option. Options that remove a feature must remove its sources and dependencies from shipping artifacts, not merely disable behavior at runtime.

## Final verification

Before merge, run the preset matching the change plus the complete validation, static-analysis, and documentation paths. For package or boundary changes, also inspect the clean installed consumer and shared export checks. Record exact commands and any skipped environment-dependent scenario in the pull request; “tested” is not sufficient evidence.
