@page project_build Build configurations

GameWIP requires CMake 3.23 or newer, C++23, Ninja, and the MSYS2 UCRT64 GCC toolchain on Windows. The static-analysis preset uses UCRT64 Clang so clang-tidy receives a compatible compile database. The address-sanitizer preset uses the separate MSYS2 CLANG64 environment because its compiler runtime provides Windows AddressSanitizer. Initialize submodules before configuring:

```powershell
git submodule update --init --recursive
```

## Named presets

```powershell
cmake --preset development
cmake --build --preset development
```

Available modes:

| Preset | Purpose |
| --- | --- |
| `development` | RelWithDebInfo game with startup correctness tests. |
| `validation` | Standalone tests and benchmark smoke validation. |
| `benchmark` | Release benchmark executable without the game. |
| `tools` | Development build with tool windows enabled. |
| `profiling` | Development build with Tracy enabled. |
| `optimized` | Optimized game with symbols and no validation. |
| `shipping` | Stripped Release game without validation or assertions. |
| `coverage` | Debug correctness tests with coverage instrumentation. |
| `address-sanitizer` | Debug correctness tests instrumented with CLANG64 AddressSanitizer. |
| `docs` | Doxygen target without game or validation executables. |
| `static-analysis` | clang-tidy and clang-format checks for maintained C++ sources. |

Build directories are named `build-<preset>`.

Every configure reports the generated GameWIP display version. The root numeric `PROJECT_VERSION` identifies the milestone or published correction, while untagged builds add the first-parent build count, abbreviated Git commit, and dirty state. The game reports the same identity without entering startup validation:

```powershell
.\build-development\GameWIP.exe --version
```

Doxygen uses this full display version as its project number. See `docs/versioning.md` for the source-version, build-identity, and release-tag policy.

The `address-sanitizer` preset uses CLANG64 because the UCRT64 GCC and Clang packages do not provide the Windows AddressSanitizer runtime. CLANG64 still targets UCRT, but uses LLVM, LLD, and libc++. Keep its build directory separate from UCRT64 builds and place the CLANG64 tools first on `PATH` when configuring, building, and testing:

```powershell
$env:PATH = "C:\MSYS2\clang64\bin;$env:PATH"
cmake --preset address-sanitizer
cmake --build --preset address-sanitizer
ctest --preset address-sanitizer
```

See @ref project_profiling for the Tracy workflow, marker policy, and disabled-build contract.

## Project options

Project-facing options use the `GAMEWIP_` prefix. Library-local options retain their library prefix so those libraries remain usable outside this top-level project.

Validation options are documented under @ref project_validation. Other important controls are:

- `GAMEWIP_BUILD_GAME`
- `GAMEWIP_ENABLE_ASSERTS`
- `GAMEWIP_ENABLE_TRACY`
- `GAMEWIP_ENABLE_TOOLS`
- `GAMEWIP_OPEN_TOOLS_AT_STARTUP`
- `GAMEWIP_ENABLE_COVERAGE`
- `GAMEWIP_ENABLE_ADDRESS_SANITIZER`
- `GAMEWIP_BUILD_DOCS`
- `GAMEWIP_INSTALL_DOCS`
- `GAMEWIP_ENABLE_STATIC_ANALYSIS`

The root CMake file orchestrates major directories. `external`, `foundation`, `tools`, `engine`, `game`, and individual validation modules own their targets and immediate children.

## Runtime dependencies

Every project executable uses the shared runtime-dependency helper. It derives the runtime search directory from the selected C++ compiler and copies matching runtime DLLs beside the executable, avoiding ABI mixing between MSYS2 environments.

See @ref project_library_compatibility for installed package names, canonical imported targets, and ABI policy.
