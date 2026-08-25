@page project_build Build configurations

GameWIP uses CMake presets to keep development, testing, benchmarking, profiling, coverage, analysis, documentation, and release builds separate.
Presets write build trees under `build/<preset>` and set the project composition options required by each workflow.

This guide explains the project-level presets and options, what each build
contains, where its artifacts go, how runtime dependencies are staged, and how
build identity reaches the executables.

Consumer-facing library options stay with the relevant library manual. For test
selection and runner behavior, see @ref project_validation and @ref
project_testing. For helper and executable syntax, see @ref
project_command_line_tools.

## Requirements

The supported Windows development environment is MSYS2 UCRT64 with CMake 4.4.2 or newer, Ninja, GCC or Clang as selected by
the preset, and C++23 support. The root `cmake_minimum_required()` declaration is the authoritative CMake version; standalone validation entry points
receive that value from the root configuration.

GameWIP does not use C++ modules. Module dependency scanning is therefore
disabled globally, which keeps Ninja compilation databases directly consumable
by clang-tidy without requiring generated module-map response files.

Before configuring a fresh checkout, initialize submodules:

```powershell
git submodule update --init --recursive
```

The `asan` preset uses the MSYS2 CLANG64 environment because the Windows AddressSanitizer runtime is provided there. Keep CLANG64 builds in their own
build directory and place CLANG64 tools first on `PATH` before configuring that preset.

## Common workflow

From the repository root, configure and build one preset:

```powershell
cmake --preset dev
cmake --build --preset dev
```

Run tests through the validation preset:

```powershell
cmake --preset test
cmake --build --preset test
ctest --preset test
```

Generate documentation through the docs preset:

```powershell
cmake --preset docs
cmake --build --preset docs
```

## Presets

| Preset | Build type | Main output | Purpose |
| --- | --- | --- | --- |
| `dev` | `RelWithDebInfo` | `GameWIP` | Daily game build with assertions and opt-in embedded tests. |
| `test` | `RelWithDebInfo` | `GameWIPTests` | Standalone correctness and package validation. |
| `benchmark` | `Release` | `GameWIPBenchmarks` | Optimized benchmark executable without the game. |
| `profile` | `RelWithDebInfo` | `GameWIP` | Tracy game build; `--startup-tests` profiles embedded validation. |
| `release` | `Release` with interprocedural optimization | `GameWIP` | Distributable game without validation, profiling, or assertions. |
| `coverage` | `Debug` | `GameWIPTests`, coverage target | Correctness tests with coverage instrumentation. |
| `asan` | `Debug` | `GameWIPTests` | CLANG64 AddressSanitizer validation build. |
| `analyze` | `RelWithDebInfo` | `static-analysis` target | clang-tidy and clang-format checks for maintained C++ sources. |
| `docs` | `Release` | `docs` target | Doxygen documentation only. |

## Commands

### Configure a preset

```powershell
cmake --preset test
```

Use this after changing CMake files, options, package rules, platform selection, documentation registration, or dependencies.

### Build a preset

```powershell
cmake --build --preset test
```

Use this to build the targets selected by the preset.

### Run a CTest preset

```powershell
ctest --preset test
```

CTest presets exist for `test`, `coverage`, and `asan`.

### Print runtime version information

```powershell
.\build\dev\GameWIP.exe --version
```

The executable prints the same generated display version used by Doxygen and runtime diagnostics without entering startup validation.

### Configure AddressSanitizer

```powershell
$env:PATH = "C:\MSYS2\clang64\bin;$env:PATH"
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

Use this only from an environment where CLANG64 tools are first on `PATH`.

## Project options

Project composition options use the `GAMEWIP_` prefix and are defined in `cmake/GameWIPOptions.cmake`.

| Option | Source default | Purpose |
| --- | --- | --- |
| `GAMEWIP_BUILD_GAME` | `ON` | Builds the `GameWIP` runtime executable. |
| `GAMEWIP_BUILD_TESTS` | `ON` | Builds the standalone `GameWIPTests` executable and CTest entries. |
| `GAMEWIP_BUILD_BENCHMARKS` | `OFF` | Builds the standalone `GameWIPBenchmarks` executable. |
| `GAMEWIP_WARNINGS_AS_ERRORS` | `OFF` | Promotes maintained first-party compiler warnings to errors. |
| `GAMEWIP_ENABLE_UNSAFE_BUFFER_WARNINGS` | `OFF` | Enables Clang's experimental unsafe-buffer migration diagnostics. |
| `GAMEWIP_ENABLE_STARTUP_TESTS` | `OFF` | Compiles correctness tests into `GameWIP` for explicit `--startup-tests` execution. |
| `GAMEWIP_RUN_BENCHMARKS_AT_STARTUP` | `OFF` | Compiles benchmark entry points into `GameWIP` and runs them after startup tests. |
| `GAMEWIP_ENABLE_TRACY` | `ON` | Enables Tracy profiler integration when selected by a preset. |
| `GAMEWIP_ENABLE_ASSERTS` | `ON` | Enables assertions and recoverable checks. |
| `GAMEWIP_ENABLE_COVERAGE` | `OFF` | Adds coverage instrumentation and the `coverage` target. |
| `GAMEWIP_ENABLE_ADDRESS_SANITIZER` | `OFF` | Adds AddressSanitizer instrumentation. |
| `GAMEWIP_ENABLE_STATIC_ANALYSIS` | `OFF` | Creates clang-tidy and clang-format validation targets. |
| `GAMEWIP_BUILD_DOCS` | `OFF` | Builds generated Doxygen documentation. |
| `GAMEWIP_INSTALL_DOCS` | `OFF` | Installs generated Doxygen HTML documentation. |
| `GAMEWIP_CLANG_TIDY_JOBS` | `4` | Controls parallel clang-tidy process count. |

Preset cache values may intentionally override source defaults. For example, the base preset disables Tracy and documentation by default, while the
`profile` and `docs` presets enable those workflows explicitly.

`GAMEWIP_WARNINGS_AS_ERRORS` remains `OFF` for ordinary local work so developers can inspect the warning baseline without a forced Werror policy.
Maintained first-party CI validation sets it to `ON`; external dependency targets retain their own warning policy.

The maintained GNU and Clang warning profiles reject conversion, lifetime, format, virtual-dispatch, switch, and declaration mistakes. The Clang-only
`buffer-safety` preset additionally reports raw pointer arithmetic, unchecked buffer indexing, and C-style buffer operations. That diagnostic is a
migration inventory rather than a defect oracle, so the preset leaves warnings visible without promoting them to errors:

```powershell
$env:PATH = "C:\MSYS2\clang64\bin;$env:PATH"
cmake --preset buffer-safety
cmake --build --preset buffer-safety
```

## Option constraints

- `GAMEWIP_ENABLE_STARTUP_TESTS` and `GAMEWIP_RUN_BENCHMARKS_AT_STARTUP` require `GAMEWIP_BUILD_GAME=ON`.
- `GAMEWIP_INSTALL_DOCS=ON` requires `GAMEWIP_BUILD_DOCS=ON`.
- `GAMEWIP_ENABLE_COVERAGE=ON` requires `GAMEWIP_BUILD_TESTS=ON`.
- Google Benchmark is required only when benchmark targets or startup benchmarks are enabled.
- Validation modules are compiled only when standalone tests or startup tests require them.

## Outputs and artifacts

| Artifact | Location | Producer |
| --- | --- | --- |
| Build tree | `build/<preset>/` | CMake preset |
| Game executable | `build/<preset>/GameWIP.exe` | Game target presets |
| Test executable | `build/test/GameWIPTests.exe` | Test preset |
| Benchmark executable | `build/benchmark/GameWIPBenchmarks.exe` | Benchmark preset |
| Doxygen HTML | `build/docs/docs/doxygen/html/` | Docs preset |
| Doxygen warning log | `build/docs/docs/doxygen/doxygen_warnings.log` | Docs preset |
| Coverage HTML | `build/coverage/coverage/index.html` | Coverage target |
| Coverage XML | `build/coverage/coverage/coverage.xml` | Coverage target |
| Helper logs, manifests, and retained results | `build/gamewip/runs/<timestamp>_<action>/` | `gamewip.bat` and `setup.bat` |

Runtime dependency copying places matching MSYS2 runtime DLLs beside project executables. The helper derives the runtime search directory from the
active compiler so UCRT64 and CLANG64 runtime files are not mixed accidentally.

## Version display

Every configure reports the generated GameWIP display version. The root numeric `PROJECT_VERSION` identifies the milestone or published correction.
Untagged builds add the first-parent build count, abbreviated Git commit, and dirty state.

Doxygen uses the generated display version as its project number. Runtime diagnostics use the same identity. See `docs/versioning.md` for
source-version, build-identity, and release-tag policy.

The game and docs targets refresh generated identity during every build. After switching commits or creating a commit, rebuilding either target
updates its generated version header; the docs target also refreshes the Doxygen project number before generation.

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

- @ref project_command_line_tools
- @ref project_validation
- @ref project_testing
- @ref project_benchmarking
- @ref project_profiling
- @ref project_coverage
- @ref project_static_analysis
- @ref project_library_compatibility
