@page library_build Build configurations

GameWIP requires CMake 3.20 or newer, C++23, Ninja, and the MSYS2 UCRT64 GCC toolchain on Windows. Initialize submodules before configuring:

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
| `docs` | Doxygen target without game or validation executables. |
| `static-analysis` | clang-tidy and clang-format checks for maintained C++ sources. |

Build directories are named `build-<preset>`.

## Project options

Project-facing options use the `GAMEWIP_` prefix. Library-local options retain their library prefix so those libraries remain usable outside this top-level project.

Validation options are documented under @ref project_validation. Other important controls are:

- `GAMEWIP_BUILD_GAME`
- `GAMEWIP_ENABLE_ASSERTS`
- `GAMEWIP_ENABLE_TRACY`
- `GAMEWIP_ENABLE_TOOLS`
- `GAMEWIP_OPEN_TOOLS_AT_STARTUP`
- `GAMEWIP_ENABLE_COVERAGE`
- `GAMEWIP_BUILD_DOCS`
- `GAMEWIP_INSTALL_DOCS`
- `GAMEWIP_ENABLE_STATIC_ANALYSIS`

The root CMake file orchestrates major directories. `external`, `foundation`, `tools`, `engine`, `game`, and individual validation modules own their targets and immediate children.

## Runtime dependencies

Every project executable uses the shared runtime-dependency helper. It scans the built executable and copies matching UCRT64 runtime DLLs beside it, avoiding accidental use of incompatible `mingw64` DLLs earlier on `PATH`.
