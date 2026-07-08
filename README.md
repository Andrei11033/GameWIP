# GameWIP

[![Latest release](https://img.shields.io/github/v/release/Andrei11033/GameWIP?display_name=tag&sort=semver)](https://github.com/Andrei11033/GameWIP/releases/latest)

GameWIP is an early-stage C++23 sandbox game project focused on player-built vehicles, structures, weapons, components, and meaningful destruction.

The repository currently emphasizes reusable foundation libraries, Windows platform backends, diagnostics, modular correctness tests, Google Benchmark scenarios, generated documentation, and initial engine systems.

## Toolchain

- Windows and MSYS2 UCRT64 GCC
- MSYS2 CLANG64 for AddressSanitizer validation
- CMake 3.23 or newer
- Ninja
- Git submodules

Initialize dependencies:

```powershell
git submodule update --init --recursive
```

Make sure `C:\MSYS2\ucrt64\bin` appears before other MinGW environments on `PATH` when configuring.

VS Code users can open `GameWIP.code-workspace` for the shared UCRT64 formatting and task configuration. Configure the `development` preset before relying on IntelliSense because the workspace reads `build-development/compile_commands.json`.

## Development

The development preset builds the game and runs modular correctness tests at startup:

```powershell
cmake --preset development
cmake --build --preset development
.\build-development\GameWIP.exe --version
.\build-development\GameWIP.exe
```

`game/main.cpp` remains the stable process entry point. Validation is compiled out when its startup options are disabled.

## Standalone validation

```powershell
cmake --preset validation
cmake --build --preset validation
ctest --preset validation
.\build-validation\GameWIPBenchmarks.exe --benchmark_dry_run
```

Run static analysis and formatting checks for maintained C++ code:

```powershell
cmake --preset static-analysis
cmake --build --preset static-analysis
```

Run correctness tests under AddressSanitizer with the separate CLANG64 environment:

```powershell
$env:PATH = "C:\MSYS2\clang64\bin;$env:PATH"
cmake --preset address-sanitizer
cmake --build --preset address-sanitizer
ctest --preset address-sanitizer
```

Run one correctness module:

```powershell
.\build-validation\GameWIPTests.exe --test-module=filesystem
```

Validation keeps complete reports under `%TEMP%\GameWIP\logs\tests` and removes temporary fixtures and subsystem logs after each run. Add `--verbose-tests` for full console output. Human UI is disabled by default; use `--manual-ui` for module-owned interactive checks or `--logger-popup` for the Logger fatal-popup check.

Collect optimized benchmark results:

```powershell
cmake --preset benchmark
cmake --build --preset benchmark
.\build-benchmark\GameWIPBenchmarks.exe --benchmark_repetitions=5
```

## Profiling

The profiling preset enables game-owned Tracy instrumentation. Start the pinned profiler before launching the game, then connect to the discovered local client:

```powershell
cmake --preset profiling
cmake --build --preset profiling
Start-Process .\.tracy\tracy-profiler.exe
.\build-profiling\GameWIP.exe
```

The initial capture names the main thread and separates startup validation, startup benchmarks, and game runtime. See the generated profiling guide for marker ownership and zero-overhead disabled-build rules.

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

- [Project structure](docs/doxygen/project_structure.md)
- [Extending the project](docs/doxygen/extending.md)
- [Vision](docs/vision.md)
- [Roadmap](docs/roadmap.md)
- [Architecture decisions](docs/decisions.md)
- [Versioning policy](docs/versioning.md)
- [Contribution workflow](docs/contributing.md)
- [Implementation checklist](docs/implementation_checklist.md)
- [Testing checklist](docs/testing_checklist.md)
- [Platform backend contract](docs/platform_backend_contract.md)
- [Static analysis standard](docs/doxygen/static_analysis.md)
- [Repository automation](docs/doxygen/repository_automation.md)

## Repository layout

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
