# GameWIP

GameWIP is an early-stage C++23 sandbox game project focused on player-built vehicles, structures, weapons, components, and meaningful destruction.

The repository currently emphasizes reusable foundation libraries, Windows platform backends, diagnostics, modular correctness tests, Google Benchmark scenarios, generated documentation, and initial engine systems.

## Toolchain

- Windows and MSYS2 UCRT64 GCC
- CMake 3.20 or newer
- Ninja
- Git submodules

Initialize dependencies:

```powershell
git submodule update --init --recursive
```

Make sure `C:\MSYS2\ucrt64\bin` appears before other MinGW environments on `PATH` when configuring.

## Development

The development preset builds the game and runs modular correctness tests at startup:

```powershell
cmake --preset development
cmake --build --preset development
.\build-development\GameWIP.exe --no-manual-ui
```

`game/main.cpp` remains the stable process entry point. Validation is compiled out when its startup options are disabled.

## Standalone Validation

```powershell
cmake --preset validation
cmake --build --preset validation
ctest --preset validation
.\build-validation\GameWIPBenchmarks.exe --benchmark_dry_run
```

Run one correctness module:

```powershell
.\build-validation\GameWIPTests.exe --test-module=filesystem
```

Validation keeps complete reports under `%TEMP%\GameWIP\logs\tests` and removes temporary fixtures and subsystem logs after each run. Add `--verbose-tests` for full console output.

Collect optimized benchmark results:

```powershell
cmake --preset benchmark
cmake --build --preset benchmark
.\build-benchmark\GameWIPBenchmarks.exe --benchmark_repetitions=5
```

## Shipping

The shipping preset excludes tests, benchmarks, TestSupport startup code, assertions, Tracy, and tools from the game executable:

```powershell
cmake --preset shipping
cmake --build --preset shipping
```

## Documentation

Generated API and developer documentation is published at [GameWIP Doxygen Documentation](https://andrei11033.github.io/GameWIP/).

Build it locally:

```powershell
cmake --preset docs
cmake --build --preset docs
```

Generated HTML starts at `build-docs/docs/doxygen/html/index.html`.

Project references:

- [Vision](docs/vision.txt)
- [Roadmap](docs/roadmap.txt)
- [Architecture decisions](docs/decisions.txt)
- [Contribution workflow](docs/contributing.md)
- [Implementation checklist](docs/implementation_checklist.txt)
- [Testing checklist](docs/testing_checklist.txt)
- [Platform backend contract](docs/platform_backend_contract.txt)

## Repository Layout

```text
foundation/   Reusable IO, Terminal, and FileSystem libraries.
engine/       Input, action, window, and window-management systems.
tools/        Logger, Assert, and TestSupport libraries.
game/         Stable game entry point, runtime facade, and modular validation.
external/     Pinned Tracy and Google Benchmark submodules.
cmake/        Project orchestration and shared CMake helpers.
docs/         Generated-doc sources, decisions, roadmap, and checklists.
```

Commit and pull-request standards are defined in [docs/contributing.md](docs/contributing.md).
