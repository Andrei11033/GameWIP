# GameWIP

[![Latest release](https://img.shields.io/github/v/release/Andrei11033/GameWIP?display_name=tag&sort=semver)](https://github.com/Andrei11033/GameWIP/releases/latest)
[![Validation](https://github.com/Andrei11033/GameWIP/actions/workflows/validation.yml/badge.svg?branch=master)](https://github.com/Andrei11033/GameWIP/actions/workflows/validation.yml)
[![Documentation](https://github.com/Andrei11033/GameWIP/actions/workflows/docs.yml/badge.svg?branch=master)](https://andrei11033.github.io/GameWIP/)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

GameWIP is an early-stage C++23 sandbox game project focused on player-built vehicles, structures, weapons, components, and meaningful destruction.

The repository provides reusable foundation libraries, Windows platform
backends, diagnostics, modular validation, benchmark registration, generated
documentation, and engine-system foundations.

GameWIP is pre-1.0. Active work is tracked in
the [R01 milestone](https://github.com/Andrei11033/GameWIP/milestone/176) and the
[roadmap](docs/roadmap.md); the latest published baseline is
[v0.0.1](docs/releases/v0.0.1.md).

## Project links

- [Developer manual](https://andrei11033.github.io/GameWIP/)
- [Contributing](CONTRIBUTING.md)
- [Code of conduct](CODE_OF_CONDUCT.md)
- [Roadmap](docs/roadmap.md)
- [Discussions](https://github.com/Andrei11033/GameWIP/discussions)
- [Issue tracker](https://github.com/Andrei11033/GameWIP/issues)
- [License](LICENSE)
- [Releases](https://github.com/Andrei11033/GameWIP/releases)
- [Security policy](SECURITY.md)

## Setup

GameWIP supports Windows 11. From a fresh checkout or extracted ZIP, run the
repository bootstrap utility:

```powershell
.\setup.bat
```

Choose Visual Studio Code, Visual Studio Community, or both. Setup
installs the selected environment, prepares pinned dependencies and profiler
tools, builds the manual, and verifies the checkout. It is also the supported
update, repair, and ownership-aware uninstall entry point. See the
[development environment manual](docs/doxygen/environment_setup.md) for actions,
update boundaries, visible command output, and the repository-only workflow key
map.

Setup asks which fetched branch an extracted ZIP should track; automation can
provide the same choice with `-Branch <name>`.

After setup, open `GameWIP.code-workspace`. The repository-scoped shortcuts
installed by setup cover development builds, tests, benchmarks, analysis,
documentation, profiling, coverage, AddressSanitizer, and release runs; all are
also available as `GameWIP: ...` entries under **Terminal > Run Task**.

## Quick start

Configure, build, and run the development preset with tools:

```powershell
cmake --preset dev
cmake --build --preset dev
.\build\dev\GameWIP.exe --version
.\build\dev\GameWIP.exe
```

Use `dev-no-tools` to verify that the runtime does not depend on optional development tooling. Both development presets compile embedded tests but run them only when explicitly requested with `GameWIP.exe --startup-tests`.

## Validation

Run the standalone correctness-test workflow:

```powershell
cmake --preset test
cmake --build --preset test
ctest --preset test
```

Run one validation module directly:

```powershell
.\build\test\GameWIPTests.exe --test-module=filesystem
```

Run C++ static-analysis and formatting checks:

```powershell
cmake --preset analyze
cmake --build --preset analyze
```

Repository script, Markdown-link, workflow, and documentation checks are documented in [Static analysis and repository checks](docs/doxygen/static_analysis.md).

Run the AddressSanitizer workflow from an MSYS2 CLANG64 environment:

```powershell
$env:PATH = "C:\MSYS2\clang64\bin;$env:PATH"
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

Run a benchmark registration dry run:

```powershell
cmake --preset benchmark
cmake --build --preset benchmark
.\build\benchmark\GameWIPBenchmarks.exe --benchmark_dry_run
```

The generated project manual documents the full validation, testing, static-analysis, coverage, profiling, and benchmarking workflows.

## Profiling

The profiling preset enables game-owned Tracy instrumentation. Install the
official Windows profiler tools matching the Tracy client pinned by the current
checkout:

```powershell
.\setup.bat profiler
```

```powershell
cmake --preset profile
cmake --build --preset profile
Start-Process .\.tracy\tracy-profiler.exe
.\build\profile\GameWIP.exe
```

The profiling preset skips startup tests unless they are explicitly requested:

```powershell
.\build\profile\GameWIP.exe --startup-tests
```

The profiling guide in the generated documentation explains marker ownership, capture expectations, and disabled-build rules.

## Release build

The release preset enables supported whole-program optimization and excludes validation, benchmarks, assertions, Tracy, and development tools from the game executable:

```powershell
cmake --preset release
cmake --build --preset release
```

## Documentation

Generated API documentation and the developer manual are published at the [GameWIP documentation site](https://andrei11033.github.io/GameWIP/).

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
- [Development environment setup](docs/doxygen/environment_setup.md)
- [Extending the project](docs/doxygen/extending.md)
- [Documentation system](docs/doxygen/documentation.md)
- [Public API contract](docs/doxygen/project_public_api_contract.md)
- [Platform backend contract](docs/doxygen/platform_backend_contract.md)
- [Vision](docs/vision.md)
- [Roadmap](docs/roadmap.md)
- [Project decisions](docs/decisions.md)
- [Versioning policy](docs/versioning.md)
- [Contributor workflow](docs/contributing.md)
- [Repository maintenance policy](docs/doxygen/repository_maintenance.md)

## Repository layout

```text
foundation/   Reusable Unicode, IO, FileSystem, and Terminal libraries.
engine/       Input, action, window, and window-management systems.
tools/        Logger, Assert, and TestSupport libraries.
game/         Stable game entry point, runtime facade, modular validation, and its local orientation guide.
external/     Pinned third-party dependencies.
cmake/        Project orchestration and shared CMake helpers.
docs/         Product direction, roadmap, decisions, versioning, and contributor workflow.
docs/doxygen/ Generated developer-manual pages and documentation infrastructure.
```

Root `README.md`, `CONTRIBUTING.md`, and `SECURITY.md` are short repository entry points. The generated manual and `docs/` pages contain the detailed developer documentation.

## License

GameWIP first-party source code and documentation are licensed under the
[Apache License 2.0](LICENSE). See [NOTICE](NOTICE) for project attribution.
Third-party dependencies under `external/` remain under their own licenses and
notices. A non-code asset may declare a separate license when its distribution
requirements differ from the source repository.
