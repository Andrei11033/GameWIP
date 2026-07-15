@page project_build Build configurations

GameWIP uses CMake presets to keep local development, validation, benchmarking, profiling, coverage, static analysis, documentation, optimized, and shipping builds separate. Presets write build trees under `build/<preset>` and set the project composition options required by each workflow.

## Scope

This page documents project-level CMake presets, project options, runtime dependency copying, version display behavior, and common build failures.

Library-local options remain documented in the owning library manual when they are consumer-facing. Validation command-line behavior is documented in @ref project_validation and @ref project_testing.

## Requirements

The supported Windows development environment is MSYS2 UCRT64 with CMake 4.4.x, Ninja, GCC or Clang as selected by the preset, and C++23 support. The root `cmake_minimum_required()` declaration is the authoritative CMake version; standalone validation entry points receive that value from the root configuration.

GameWIP does not currently use C++ modules, so module dependency scanning is disabled globally. This keeps Ninja compilation databases directly consumable by clang-tidy without requiring generated module-map response files.

Before configuring a fresh checkout, initialize submodules:

```powershell
git submodule update --init --recursive
```

The `address-sanitizer` preset uses the MSYS2 CLANG64 environment because the Windows AddressSanitizer runtime is provided there. Keep CLANG64 builds in their own build directory and place CLANG64 tools first on `PATH` before configuring that preset.

## Common workflow

From the repository root, configure and build one preset:

```powershell
cmake --preset development
cmake --build --preset development
```

Run tests through the validation preset:

```powershell
cmake --preset validation
cmake --build --preset validation
ctest --preset validation
```

Generate documentation through the docs preset:

```powershell
cmake --preset docs
cmake --build --preset docs
```

## Presets

| Preset | Build type | Main output | Purpose |
| --- | --- | --- | --- |
| `development` | `RelWithDebInfo` | `GameWIP` | Development game build with startup correctness tests enabled. |
| `validation` | `RelWithDebInfo` | `GameWIP`, `GameWIPTests`, `GameWIPBenchmarks` | Standalone correctness validation and benchmark registration checks. |
| `benchmark` | `Release` | `GameWIPBenchmarks` | Optimized benchmark executable without the game. |
| `tools` | `RelWithDebInfo` | `GameWIP` | Development build with tool support enabled and opened at startup. |
| `profiling` | `RelWithDebInfo` | `GameWIP` | Tracy profiling with startup validation disabled for clean runtime captures. |
| `profiling-validation` | `RelWithDebInfo` | `GameWIP` | Tracy profiling with startup correctness tests enabled. |
| `optimized` | `RelWithDebInfo` with `-O3` | `GameWIP` | Optimized game build with symbols and validation disabled. |
| `shipping` | `Release` with stripping | `GameWIP` | Shipping-style game build without validation or assertions. |
| `coverage` | `Debug` | `GameWIPTests`, coverage target | Correctness tests with coverage instrumentation. |
| `address-sanitizer` | `Debug` | `GameWIP`, `GameWIPTests` | CLANG64 AddressSanitizer validation build. |
| `static-analysis` | `RelWithDebInfo` | `static-analysis` target | clang-tidy and clang-format checks for maintained C++ sources. |
| `docs` | `Release` | `docs` target | Doxygen documentation only. |

## Commands

### Configure a preset

```powershell
cmake --preset validation
```

Use this after changing CMake files, options, package rules, platform selection, documentation registration, or dependencies.

### Build a preset

```powershell
cmake --build --preset validation
```

Use this to build the targets selected by the preset.

### Run a CTest preset

```powershell
ctest --preset validation
```

CTest presets exist for `validation`, `coverage`, and `address-sanitizer`.

### Print runtime version information

```powershell
.\build\development\GameWIP.exe --version
```

The executable prints the same generated display version used by Doxygen and runtime diagnostics without entering startup validation.

### Configure AddressSanitizer

```powershell
$env:PATH = "C:\MSYS2\clang64\bin;$env:PATH"
cmake --preset address-sanitizer
cmake --build --preset address-sanitizer
ctest --preset address-sanitizer
```

Use this only from an environment where CLANG64 tools are first on `PATH`.

## Project options

Project composition options use the `GAMEWIP_` prefix and are defined in `cmake/GameWIPOptions.cmake`.

| Option | Source default | Purpose |
| --- | --- | --- |
| `GAMEWIP_BUILD_GAME` | `ON` | Builds the `GameWIP` runtime executable. |
| `GAMEWIP_BUILD_TESTS` | `ON` | Builds the standalone `GameWIPTests` executable and CTest entries. |
| `GAMEWIP_BUILD_BENCHMARKS` | `OFF` | Builds the standalone `GameWIPBenchmarks` executable. |
| `GAMEWIP_RUN_TESTS_AT_STARTUP` | `ON` | Compiles correctness tests into `GameWIP` and runs them before game startup. |
| `GAMEWIP_RUN_BENCHMARKS_AT_STARTUP` | `OFF` | Compiles benchmark entry points into `GameWIP` and runs them after startup tests. |
| `GAMEWIP_ENABLE_TRACY` | `ON` | Enables Tracy profiler integration when selected by a preset. |
| `GAMEWIP_ENABLE_TOOLS` | `OFF` | Enables editor and tool-window support in the game executable. |
| `GAMEWIP_OPEN_TOOLS_AT_STARTUP` | `OFF` | Opens tool windows automatically when tool support is enabled. |
| `GAMEWIP_ENABLE_ASSERTS` | `ON` | Enables assertions and recoverable checks. |
| `GAMEWIP_ENABLE_COVERAGE` | `OFF` | Adds coverage instrumentation and the `coverage` target. |
| `GAMEWIP_ENABLE_ADDRESS_SANITIZER` | `OFF` | Adds AddressSanitizer instrumentation. |
| `GAMEWIP_ENABLE_STATIC_ANALYSIS` | `OFF` | Creates clang-tidy and clang-format validation targets. |
| `GAMEWIP_BUILD_DOCS` | `OFF` | Builds generated Doxygen documentation. |
| `GAMEWIP_INSTALL_DOCS` | `OFF` | Installs generated Doxygen HTML documentation. |
| `GAMEWIP_CLANG_TIDY_JOBS` | `4` | Controls parallel clang-tidy process count. |

Preset cache values may intentionally override source defaults. For example, the base preset disables Tracy and documentation by default, while the `profiling` and `docs` presets enable those workflows explicitly.

## Option constraints

- `GAMEWIP_RUN_TESTS_AT_STARTUP` and `GAMEWIP_RUN_BENCHMARKS_AT_STARTUP` require `GAMEWIP_BUILD_GAME=ON`.
- `GAMEWIP_INSTALL_DOCS=ON` requires `GAMEWIP_BUILD_DOCS=ON`.
- `GAMEWIP_ENABLE_COVERAGE=ON` requires `GAMEWIP_BUILD_TESTS=ON`.
- Google Benchmark is required only when benchmark targets or startup benchmarks are enabled.
- Validation modules are compiled only when standalone tests or startup tests require them.

## Outputs and artifacts

| Artifact | Location | Producer |
| --- | --- | --- |
| Build tree | `build/<preset>/` | CMake preset |
| Game executable | `build/<preset>/GameWIP.exe` | Game target presets |
| Test executable | `build/validation/GameWIPTests.exe` | Validation preset |
| Benchmark executable | `build/benchmark/GameWIPBenchmarks.exe` | Benchmark preset |
| Doxygen HTML | `build/docs/docs/doxygen/html/` | Docs preset |
| Doxygen warning log | `build/docs/docs/doxygen/doxygen_warnings.log` | Docs preset |
| Coverage HTML | `build/coverage/coverage/index.html` | Coverage target |
| Coverage XML | `build/coverage/coverage/coverage.xml` | Coverage target |

Runtime dependency copying places matching MSYS2 runtime DLLs beside project executables. The helper derives the runtime search directory from the active compiler so UCRT64 and CLANG64 runtime files are not mixed accidentally.

## Version display

Every configure reports the generated GameWIP display version. The root numeric `PROJECT_VERSION` identifies the milestone or published correction. Untagged builds add the first-parent build count, abbreviated Git commit, and dirty state.

Doxygen uses the generated display version as its project number. Runtime diagnostics use the same identity. See `docs/versioning.md` for source-version, build-identity, and release-tag policy.

The game and docs targets refresh generated identity during every build. After switching commits or creating a commit, rebuilding either target updates its generated version header; the docs target also refreshes the Doxygen project number before generation.

## Failure behavior

| Symptom | Likely cause | Action |
| --- | --- | --- |
| A preset cannot find Ninja, CMake, or the compiler. | The expected MSYS2 environment is not first on `PATH`. | Start the correct MSYS2 shell or update `PATH` before configuring. |
| AddressSanitizer configuration fails. | The preset is being configured from UCRT64 instead of CLANG64. | Put `C:\MSYS2\clang64\bin` first on `PATH` and use a separate build tree. |
| Docs configuration fails. | Doxygen is not installed or not discoverable. | Install Doxygen or disable `GAMEWIP_BUILD_DOCS`. |
| Coverage configuration fails. | Tests are disabled while coverage is enabled. | Enable `GAMEWIP_BUILD_TESTS` or use the `coverage` preset. |
| Startup validation option fails configuration. | Startup validation was enabled while the game executable was disabled. | Enable `GAMEWIP_BUILD_GAME` or disable the startup option. |
| Runtime DLL errors occur at launch. | The executable is being run with mismatched MSYS2 runtime files. | Rebuild with one environment and run the executable from its preset output directory. |

## Maintainer notes

When adding a preset or project option:

- Define option defaults in `cmake/GameWIPOptions.cmake` when the option is project-owned.
- Set every relevant preset value intentionally.
- Document the option in this page or the owning library manual.
- Add configuration-time validation for invalid option combinations.
- Keep preset build directories separate when compiler runtime or ABI selection changes.
- Update validation, CI, editor tasks, and documentation when a preset becomes part of the supported workflow.

## Related pages

- @ref project_validation
- @ref project_testing
- @ref project_benchmarking
- @ref project_profiling
- @ref project_coverage
- @ref project_static_analysis
- @ref project_library_compatibility
