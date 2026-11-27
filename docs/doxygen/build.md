@page project_build Build configurations

GameWIP uses CMake presets to keep development, testing, benchmarking,
profiling, coverage, analysis, documentation, and release builds separate.
Native CMake builds use `build/<preset>`. The helper uses
`build/<target>/<preset>` for Windows and Linux targets so their caches stay
separate. Zig provides Linux AMD64 and ARM64 compiler targets on both Windows and
Linux hosts. Full project builds wait for Linux platform backends.

The default CMake install prefix is `<build-tree>/install`. Set
`CMAKE_INSTALL_PREFIX` explicitly when an install needs a different destination.

This guide explains the project-level presets and options, what each build
contains, where its artifacts go, how runtime dependencies are staged, and how
build identity reaches the executables.

Consumer-facing library options stay with the relevant library manual. For test
selection and runner behavior, see @ref project_validation and @ref
project_testing. For helper and executable syntax, see @ref
project_command_line_tools.

## Requirements

Use PowerShell for `gamewip.bat` on Windows and `bash ./gamewip.sh` on Linux.
Direct CMake examples below use MSYS2 UCRT64 Bash; direct sanitizer examples use
CLANG64. The helper's Windows targets use LLVM-MinGW Clang with libc++ and UCRT.
Linux targets use Zig with a glibc 2.35 baseline; a matching system Zig is
reused, otherwise setup installs the pinned archive.
Direct CMake commands use the active MSYS2 compiler. The supported Windows
environment is
MSYS2 UCRT64 with CMake 4.4.4 or newer, Ninja, GCC 16.2 or newer or Clang
23.1.3 or newer as selected by the preset, and C++23 support. Compiler floors
are declared as `minimum` in
`scripts/config/project-tools.json`; an older compiler is reported as outdated
instead of silently producing different diagnostics than CI. The
root `cmake_minimum_required()` declaration is the authoritative CMake version;
standalone validation entry points receive that value from the root
configuration.

GameWIP does not use C++ modules. Module dependency scanning is therefore
disabled globally, which keeps Ninja compilation databases directly consumable
by clang-tidy without requiring generated module-map response files.

Before configuring a fresh checkout, prepare the locked dependency cache:

```powershell
.\setup.bat deps
```

Native Windows CMake `asan` and `ubsan` builds use MSYS2 CLANG64. Keep those
trees separate from helper-managed target trees and place CLANG64 tools first
on `PATH`. Direct commands below use the default MSYS2 root. Set
`GAMEWIP_MSYS2_ROOT` to an absolute path when using an existing installation or
when the storage root has changed. The Windows helper uses the selected
LLVM-MinGW target;
AddressSanitizer is available for AMD64, but not ARM64.

CMake selects the dependency-cache default when it first creates the build
cache. If the storage root later changes, set `GAMEWIP_DEPENDENCY_CACHE_DIR`
to the desired cache path.

## Common workflow

From PowerShell at the repository root, the helper defaults to Windows AMD64,
selects LLVM-MinGW, and configures the preset when needed:

```powershell
.\gamewip.bat target
.\gamewip.bat build dev
.\gamewip.bat test test
.\gamewip.bat build docs
```

Select Windows ARM64 with:

```powershell
.\gamewip.bat target -TargetPlatform windows -TargetArchitecture arm64
```

`setup.bat repair` installs the pinned LLVM-MinGW toolchain when it is missing.
Target selection is stored under `<storage-root>/state` (default
`build/gamewip/state`); each target uses its own preset trees.

Select Linux AMD64 or ARM64 from either host:

```powershell
.\gamewip.bat target -TargetPlatform linux -TargetArchitecture amd64
.\gamewip.bat target -TargetPlatform linux -TargetArchitecture arm64
```

```bash
bash ./gamewip.sh target -TargetPlatform linux -TargetArchitecture amd64
bash ./gamewip.sh target -TargetPlatform linux -TargetArchitecture arm64
```

The target selection and compiler are ready for Linux backend work. Project
configuration stops when it reaches a library without a Linux backend.

The helper does not configure an ARM64 emulator. Run ARM64 executables on an
ARM64 Windows host; on AMD64 Windows, select AMD64 before running CTest.

From MSYS2 UCRT64 Bash, configure and build one preset:

```bash
cmake --preset dev
cmake --build --preset dev
```

Direct CMake can select a target with cache variables and a separate build
directory. The helper supplies the selected toolchain root automatically.

```powershell
cmake --preset dev -B build/windows-arm64/dev `
  -DGAMEWIP_TARGET_PLATFORM=windows `
  -DGAMEWIP_TARGET_ARCHITECTURE=arm64
cmake --build build/windows-arm64/dev
```

The Windows helper manages target selection and build paths for this workflow.

Run tests through the validation preset:

```bash
cmake --preset test
cmake --build --preset test
ctest --preset test
```

Cross-build tests with the Linux helper, then run CTest on Windows.

Generate documentation through the docs preset:

```bash
cmake --preset docs
cmake --build --preset docs
```

### Incremental and fresh helper builds

Ordinary `gamewip.bat config`, `build`, and `test` invocations reuse the
selected preset tree for fast local iteration. Add `-CleanBuild` when a result must
not depend on prior generated files:

```powershell
.\gamewip.bat test test -CleanBuild
```

For offline dependency use, prepare the shared cache once and pass `-Offline`
to configure or build. The helper then requires the pinned sources already in
the cache and CMake does not download them. Direct CMake invocations use
`GAMEWIP_DEPENDENCIES_OFFLINE=ON`:

```powershell
.\gamewip.bat deps prepare
.\gamewip.bat config profile -Offline
.\gamewip.bat build profile -Offline
```

Fresh mode removes the complete selected target's preset directory before
configuration. A target clean or `cmake --fresh` is not equivalent: either can
leave orphaned object files, generated sources, instrumentation counters, or
runtime copies that are no longer part of the current build graph. The helper
accepts only cataloged preset names within the repository build tree, and
refuses recursive removal through reparse points.

The high-level `coverage`, `asan`, and `ubsan` actions are fresh by default.
The `local-release-check` and `sanitizer` bundles also declare fresh build
trees, while ordinary low-level commands and the `quick` bundle remain
incremental. GitHub-hosted validation jobs already start in a new runner
workspace. Workflows may cache downloaded tools or dependencies, but must not
restore CMake build trees across runs.

### Target selection

Direct CMake builds use the host platform and architecture unless a target is
selected before compiler detection. Set `GAMEWIP_TARGET_PLATFORM` to `windows`
or `linux` and `GAMEWIP_TARGET_ARCHITECTURE` to `amd64` or `arm64`. The helper
keeps each target in a separate build tree. `GAMEWIP_LLVM_MINGW_ROOT` and
`GAMEWIP_ZIG_ROOT` can select existing toolchain directories.

## Presets

| Preset | Build type | Main output | Purpose |
| --- | --- | --- | --- |
| `dev` | `RelWithDebInfo` | `GameWIP` | Daily game build with assertions and opt-in embedded tests. |
| `test` | `RelWithDebInfo` | `GameWIPTests` | Standalone correctness and package validation. |
| `benchmark` | `Release` | `GameWIPBenchmarks` | Optimized benchmark executable without the game. |
| `profile` | `RelWithDebInfo` | `GameWIP` | Tracy game build; `--startup-tests` profiles embedded validation. |
| `release` | `Release` with interprocedural optimization | `GameWIP` | Distributable game without validation, profiling, or assertions. |
| `coverage` | `Debug` | `GameWIPTests`, coverage target | Correctness tests with coverage instrumentation. |
| `asan` | `Debug` | `GameWIPTests` | AddressSanitizer validation build. |
| `ubsan` | `Debug` | `GameWIPTests` | UndefinedBehaviorSanitizer validation build. |
| `analyze` | `RelWithDebInfo` | `static-analysis` target | clang-tidy and clang-format checks for maintained C++ sources. |
| `docs` | `Release` | `docs` target | Doxygen documentation only. |

`ci-*` presets (`ci-validation`, `ci-package`, `ci-package-multi`,
`ci-coverage`, `ci-asan`, `ci-ubsan`, `ci-analyze`, `ci-docs`) mirror the
workflow presets above with warnings-as-errors and offline dependencies. They
are for CI; see `CMakePresets.json`.

## Commands

### Print runtime version information

For the helper's default Windows AMD64 target:

```powershell
.\build\windows-amd64\dev\GameWIP.exe --version
```

Direct CMake builds place the executable at `build/dev/GameWIP.exe`. The
executable prints the generated display version without entering startup
validation.

### Configure AddressSanitizer

```powershell
$msys2Root = if ($env:GAMEWIP_MSYS2_ROOT) { $env:GAMEWIP_MSYS2_ROOT } else { 'build/gamewip/tools/msys2' }
$env:PATH = "$(Join-Path $msys2Root 'clang64/bin');$env:PATH"
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

The equivalent commands in MSYS2 CLANG64 Bash are:

```bash
export PATH="/clang64/bin:$PATH"
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

Or use the PowerShell helper, which uses the selected Windows target and starts
a fresh build. AddressSanitizer supports AMD64 only:

```powershell
.\gamewip.bat asan
```

### Configure UndefinedBehaviorSanitizer

```powershell
$msys2Root = if ($env:GAMEWIP_MSYS2_ROOT) { $env:GAMEWIP_MSYS2_ROOT } else { 'build/gamewip/tools/msys2' }
$env:PATH = "$(Join-Path $msys2Root 'clang64/bin');$env:PATH"
cmake --preset ubsan
cmake --build --preset ubsan
ctest --preset ubsan
```

The equivalent commands in MSYS2 CLANG64 Bash are:

```bash
export PATH="/clang64/bin:$PATH"
cmake --preset ubsan
cmake --build --preset ubsan
ctest --preset ubsan
```

Or use the PowerShell helper, which uses the selected Windows target and starts
a fresh build:

```powershell
.\gamewip.bat ubsan
```

## Project options

Project composition options use the `GAMEWIP_` prefix and are defined in `cmake/GameWIPOptions.cmake`.

| Option | Source default | Purpose |
| --- | --- | --- |
| `GAMEWIP_BUILD_GAME` | `ON` | Builds the `GameWIP` runtime executable. |
| `GAMEWIP_BUILD_TESTS` | `ON` | Builds the standalone `GameWIPTests` executable and CTest entries. |
| `GAMEWIP_BUILD_BENCHMARKS` | `OFF` | Builds the standalone `GameWIPBenchmarks` executable. |
| `GAMEWIP_WARNINGS_AS_ERRORS` | `OFF` | Promotes maintained first-party compiler warnings to errors. |
| `GAMEWIP_ENABLE_STARTUP_TESTS` | `OFF` | Compiles correctness tests into `GameWIP` for explicit `--startup-tests` execution. |
| `GAMEWIP_RUN_BENCHMARKS_AT_STARTUP` | `OFF` | Compiles benchmark entry points into `GameWIP` and runs them after startup tests. |
| `GAMEWIP_ENABLE_TRACY` | `ON` | Enables Tracy profiler integration when selected by a preset. |
| `GAMEWIP_DEPENDENCIES_OFFLINE` | `OFF` | Requires the prepared locked dependency cache and disallows dependency downloads. |
| `GAMEWIP_ENABLE_ASSERTS` | `ON` | Enables assertions and recoverable checks. |
| `GAMEWIP_ENABLE_COVERAGE` | `OFF` | Adds coverage instrumentation and the `coverage` target. |
| `GAMEWIP_ENABLE_ADDRESS_SANITIZER` | `OFF` | Adds AddressSanitizer instrumentation. |
| `GAMEWIP_ENABLE_UNDEFINED_BEHAVIOR_SANITIZER` | `OFF` | Adds UndefinedBehaviorSanitizer instrumentation. |
| `GAMEWIP_ENABLE_STATIC_ANALYSIS` | `OFF` | Creates clang-tidy and clang-format validation targets. |
| `GAMEWIP_BUILD_DOCS` | `OFF` | Builds generated Doxygen documentation. |
| `GAMEWIP_INSTALL_DOCS` | `OFF` | Installs generated Doxygen HTML documentation. |
| `GAMEWIP_CLANG_TIDY_JOBS` | `4` | Controls parallel clang-tidy process count. |

Preset cache values may intentionally override source defaults. For example, the base preset disables Tracy and documentation by default, while the
`profile` and `docs` presets enable those workflows explicitly.

`GAMEWIP_WARNINGS_AS_ERRORS` remains `OFF` for ordinary local work so developers can inspect the warning baseline without a forced Werror policy.
Maintained first-party CI validation sets it to `ON`; external dependency targets retain their own warning policy.

The maintained GNU and Clang warning profiles reject conversion, lifetime,
format, virtual-dispatch, switch, and declaration mistakes. Clang builds also
reject unreviewed raw pointer arithmetic, unchecked buffer indexing, and
C-style buffer operations through `-Wunsafe-buffer-usage`. Code at a native or
language-runtime boundary must validate the available size, convert once to a
bounded view, and keep any diagnostic annotation limited to that documented
conversion.

## Option constraints

- `GAMEWIP_ENABLE_STARTUP_TESTS` and `GAMEWIP_RUN_BENCHMARKS_AT_STARTUP` require `GAMEWIP_BUILD_GAME=ON`.
- `GAMEWIP_INSTALL_DOCS=ON` requires `GAMEWIP_BUILD_DOCS=ON`.
- `GAMEWIP_ENABLE_COVERAGE=ON` requires `GAMEWIP_BUILD_TESTS=ON`.
- Google Benchmark is required only when benchmark targets or startup benchmarks are enabled.
- Validation modules are compiled only when standalone tests or startup tests require them.

## Outputs and artifacts

| Artifact | Location | Producer |
| --- | --- | --- |
| Build tree | `build/<preset>/` direct; `build/<target>/<preset>/` helper | CMake preset |
| Game executable | `<build-tree>/GameWIP.exe` | Game target presets |
| Test executable | `<build-tree>/GameWIPTests.exe` | Test preset |
| Benchmark executable | `<build-tree>/GameWIPBenchmarks.exe` | Benchmark preset |
| Validation reports | `<build-tree>/logs/validation/` | Standalone or embedded validation |
| Validation and benchmark temporary workspaces | `<build-tree>/temp/` | Validation/benchmark runners and child processes |
| Doxygen HTML | `<build-tree>/docs/doxygen/html/` | Docs preset |
| Doxygen warning log | `<build-tree>/docs/doxygen/doxygen_warnings.log` | Docs preset |
| Coverage HTML | `<build-tree>/coverage/index.html` | Coverage target |
| Coverage XML | `<build-tree>/coverage/coverage.xml` | Coverage target |
| Helper logs, manifests, and retained results | `<storage-root>/runs/<timestamp>_<action>/` (default `build/gamewip/runs/`) | `gamewip.bat` and `setup.bat` |
| Default CMake install prefix | `<build-tree>/install/` | `cmake --install` |

Runtime dependency copying places matching compiler runtime DLLs beside project
executables. The helper searches the active compiler and target runtime
directories so UCRT64 and LLVM-MinGW files are not mixed accidentally.

## Version display

Every configure reports the generated GameWIP display version. The root numeric
`PROJECT_VERSION` identifies the milestone or published correction. Untagged
builds add the first-parent build count, abbreviated Git commit, and dirty state
for tracked changes or non-ignored untracked files.

Doxygen uses the generated display version as its project number. Runtime
diagnostics use the same identity. See `docs/versioning.md` for source-version,
build-identity, and release-tag policy.

The game and docs targets refresh generated identity during every build. After
switching commits or creating a commit, rebuilding either target updates its
generated version header. The docs target also refreshes the Doxygen project
number before generation.

## Failure behavior

| Symptom | Likely cause | Action |
| --- | --- | --- |
| A preset cannot find Ninja, CMake, or the compiler. | A selected tool is missing or unavailable on `PATH`. | Run `setup.bat repair` or `bash ./setup.sh repair`, then check the selected target. |
| Native Windows CMake sanitizer configuration fails. | The preset is being configured from UCRT64 instead of CLANG64. | Put `<msys2-root>/clang64/bin` first on `PATH` and use a separate build tree. |
| Docs configuration fails. | Doxygen is not installed or not discoverable. | Install Doxygen or disable `GAMEWIP_BUILD_DOCS`. |
| Coverage configuration fails. | Tests are disabled while coverage is enabled. | Enable `GAMEWIP_BUILD_TESTS` or use the `coverage` preset. |
| Startup validation option fails configuration. | Startup validation was enabled while the game executable was disabled. | Enable `GAMEWIP_BUILD_GAME` or disable the startup option. |
| Runtime DLL errors occur at launch. | The executable is being run with mismatched MSYS2 runtime files. | Rebuild with one environment and run the executable from its preset output directory. |

## Maintainer notes

When adding a preset or project option:

- Define option defaults in `cmake/GameWIPOptions.cmake` when the option is
  project-owned.
- Set every relevant preset value intentionally.
- Document the option in this page or the owning library manual.
- Add configuration-time validation for invalid option combinations.
- Keep preset build directories separate when compiler runtime or ABI selection
  changes.
- Update validation, CI, editor tasks, and documentation when a preset becomes
  part of the supported workflow.

## Related pages

- @ref project_command_line_tools
- @ref project_validation
- @ref project_testing
- @ref project_benchmarking
- @ref project_profiling
- @ref project_coverage
- @ref project_static_analysis
- @ref project_library_compatibility
