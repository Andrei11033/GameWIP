# GameWIP

[![Latest release](https://img.shields.io/github/v/release/Andrei11033/GameWIP?display_name=tag&sort=semver)](https://github.com/Andrei11033/GameWIP/releases/latest)

GameWIP is an early-stage C++23 sandbox game project focused on player-built vehicles, structures, weapons, components, and meaningful destruction.

The repository currently emphasizes reusable foundation libraries, Windows platform backends, diagnostics, modular validation, benchmark registration, generated documentation, and initial engine systems.

## Requirements

- Windows with MSYS2 UCRT64 GCC for normal development.
- MSYS2 CLANG64 for AddressSanitizer validation.
- CMake 3.23 or newer.
- Ninja.
- Git submodules.

Initialize third-party dependencies after cloning:

```powershell
git submodule update --init --recursive
```

When configuring with UCRT64, make sure `C:\MSYS2\ucrt64\bin` appears before other MinGW environments on `PATH`.

VS Code users can open `GameWIP.code-workspace`. Configure the `development` preset before relying on IntelliSense because the workspace reads `build/development/compile_commands.json`.

## Quick start

Configure, build, and run the development preset:

```powershell
cmake --preset development
cmake --build --preset development
.\build\development\GameWIP.exe --version
.\build\development\GameWIP.exe
```

The development preset builds the game and runs configured startup validation before entering the runtime facade.

## Validation

Run the standalone correctness-test workflow:

```powershell
cmake --preset validation
cmake --build --preset validation
ctest --preset validation
```

Run one validation module directly:

```powershell
.\build\validation\GameWIPTests.exe --test-module=filesystem
```

Run C++ static-analysis and formatting checks:

```powershell
cmake --preset static-analysis
cmake --build --preset static-analysis
```

Repository script, Markdown-link, workflow, and documentation checks are documented in [Static analysis and repository checks](docs/doxygen/static_analysis.md).

Run the AddressSanitizer workflow from an MSYS2 CLANG64 environment:

```powershell
$env:PATH = "C:\MSYS2\clang64\bin;$env:PATH"
cmake --preset address-sanitizer
cmake --build --preset address-sanitizer
ctest --preset address-sanitizer
```

Run a benchmark registration dry run:

```powershell
cmake --preset validation
cmake --build --preset validation
.\build\validation\GameWIPBenchmarks.exe --benchmark_dry_run
```

The generated project manual documents the full validation, testing, static-analysis, coverage, profiling, and benchmarking workflows.

## Profiling

The profiling preset enables game-owned Tracy instrumentation:

```powershell
cmake --preset profiling
cmake --build --preset profiling
Start-Process .\.tracy\tracy-profiler.exe
.\build\profiling\GameWIP.exe
```

The profiling guide in the generated documentation explains marker ownership, capture expectations, and disabled-build rules.

## Shipping build

The shipping preset excludes validation, benchmarks, TestSupport startup code, assertions, Tracy, and development tools from the game executable:

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
$warningLog = Get-Item .\build\docs\docs\doxygen\doxygen_warnings.log
if ($warningLog.Length -ne 0) {
    Get-Content $warningLog
    throw "Doxygen emitted warnings."
}
```

Generated HTML starts at `build/docs/docs/doxygen/html/index.html`.

Start with these pages when reading the source tree:

- [Project structure](docs/doxygen/project_structure.md)
- [CMake infrastructure](docs/doxygen/cmake_infrastructure.md)
- [Extending the project](docs/doxygen/extending.md)
- [Documentation system](docs/doxygen/documentation.md)
- [Platform backend contract](docs/doxygen/platform_backend_contract.md)
- [Vision](docs/vision.md)
- [Roadmap](docs/roadmap.md)
- [Project decisions](docs/decisions.md)
- [Versioning policy](docs/versioning.md)
- [Contributor workflow](docs/contributing.md)

## Repository layout

```text
foundation/   Reusable IO, Terminal, and FileSystem libraries.
engine/       Input, action, window, and window-management systems.
tools/        Logger, Assert, and TestSupport libraries.
game/         Stable game entry point, runtime facade, and modular validation.
external/     Pinned third-party dependencies.
cmake/        Project orchestration and shared CMake helpers.
docs/         Product direction, roadmap, decisions, versioning, and contributor workflow.
docs/doxygen/ Generated developer-manual pages and documentation infrastructure.
```

Root `README.md`, `CONTRIBUTING.md`, and `SECURITY.md` are short repository entry points. The generated manual and `docs/` pages contain the detailed coder-facing project documentation.
