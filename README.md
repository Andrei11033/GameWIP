# GameWIP

GameWIP is an early-stage C++23 sandbox game project focused on player-built vehicles, structures, weapons, components, and meaningful destruction.

The current codebase is infrastructure-focused rather than a playable release. Active work is centered on reusable foundation libraries, Windows platform backends, project-native test support, generated documentation, and the first engine systems.

## Current Status

Primary target:

- Windows-first development
- C++23
- MSYS2 UCRT64 g++ toolchain
- CMake build system
- Ninja for local builds
- Visual Studio Code workspace support

Implemented foundation and tooling:

- CMake project wiring and library targets
- IO foundation library
- Terminal foundation library
- FileSystem foundation library
- Logger library
- Assert/debug diagnostics library
- TestSupport library for project-native tests
- Engine scaffolding for input, actions, windows, and window management
- Tracy integration
- Doxygen documentation infrastructure

The current foundation layer includes IO, Terminal, FileSystem, Logger, Assert, and TestSupport. Use the implementation and testing checklists as the source of truth for validation status and remaining polish work.

## Prerequisites

Install the expected Windows toolchain:

- Git
- CMake 3.20 or newer
- Ninja
- MSYS2 UCRT64 toolchain with g++
- Visual Studio Code, optional

## Build

Initialize submodules:

```powershell
git submodule update --init --recursive
```

Run CMake from an environment where the MSYS2 UCRT64 `g++` is first on `PATH`. If CMake selects the wrong compiler, clear the build directory and configure again from the correct shell.

Configure and build:

```powershell
cmake -S . -B build -G Ninja `
  -DASSERTS_ENABLED=ON `
  -DENABLE_LIBRARY_COVERAGE=OFF `
  -DBUILD_DOCS=OFF

cmake --build build
```

## Test

Run the normal automated test pass:

```powershell
ctest --test-dir build --output-on-failure
```

You can also run the test executable directly:

```powershell
.\build\GameWIP.exe --no-manual-ui
```

To run only the FileSystem-focused tests:

```powershell
.\build\GameWIP.exe --filesystem-only
```

Test reports are written under:

```text
logs/tests/latest_test_report.txt
```

## Documentation

Generated API and guide documentation is published through GitHub Pages:

- [GameWIP Doxygen Documentation](https://andrei11033.github.io/GameWIP/)

Project planning and validation notes:

- [docs/vision.txt](docs/vision.txt) - what the game is meant to become
- [docs/roadmap.txt](docs/roadmap.txt) - planned development order toward V1
- [docs/decisions.txt](docs/decisions.txt) - stable architecture, tooling, naming, and workflow decisions
- [docs/implementation_checklist.txt](docs/implementation_checklist.txt) - what code exists
- [docs/testing_checklist.txt](docs/testing_checklist.txt) - what behavior has been validated
- [docs/platform_backend_contract.txt](docs/platform_backend_contract.txt) - platform backend rules

Build the generated documentation locally:

```powershell
cmake -S . -B build-docs -G Ninja -DBUILD_DOCS=ON
cmake --build build-docs --target docs
```

Generated HTML is written to:

```text
build-docs/docs/doxygen/html/index.html
```

## Repository Layout

```text
foundation/   Reusable low-level libraries such as IO, Terminal, and FileSystem.
engine/       Engine-facing systems such as input, actions, windows, and window management.
tools/        Development libraries such as Logger, Assert, and TestSupport.
game/         Game executable entry point and current test-suite dispatcher.
docs/         Vision, roadmap, decisions, checklists, and generated-doc source pages.
cmake/        Shared CMake helpers.
external/     Third-party dependencies, currently including Tracy.
assets/       Project asset folder.
logs/         Local runtime and test output.
```

## Development Rules

Keep implementation and validation tracking separate:

- Update [docs/implementation_checklist.txt](docs/implementation_checklist.txt) when code exists.
- Update [docs/testing_checklist.txt](docs/testing_checklist.txt) only when behavior is tested, manually checked, measured, or inspected.
- Put long-term architecture decisions in [docs/decisions.txt](docs/decisions.txt).
- Put roadmap direction in [docs/roadmap.txt](docs/roadmap.txt).

Keep platform-specific code behind internal backend headers. Windows backend code should use explicit Unicode Win32 APIs and avoid generic A/W macro-mapped calls.

Commit messages follow the project style from [docs/decisions.txt](docs/decisions.txt):

```text
area: imperative summary
```

Examples:

```text
filesystem: add strict symlink entry queries
docs: add private onboarding README
tests: cover filesystem symlink policies
```
