# GameWIP

GameWIP is a work-in-progress C++23 sandbox game project focused on player-built vehicles, structures, weapons, components, and meaningful destruction.

The project is private and early-stage. The current work is mostly foundation and engine infrastructure: reusable libraries, Windows platform backends, test support, documentation, and the first pieces of the engine layer. It is not a playable game yet.

## Current Status

Current target:

- Windows-first development.
- C++23.
- MSYS2 UCRT64 g++ toolchain.
- CMake-based build.
- Ninja recommended for local builds.
- Visual Studio Code workspace support.

Implemented foundation and tooling currently includes:

- CMake project wiring and library targets.
- IO foundation library.
- Terminal foundation library.
- FileSystem foundation library in active development.
- Logger library.
- Assert/debug diagnostics library.
- TestSupport library for project-native tests.
- Engine scaffolding for input, actions, windows, and window management.
- Tracy integration.
- Doxygen documentation infrastructure.

The current active foundation phase is FileSystem. The public contract is larger than the implemented runtime surface, so check `docs/implementation_checklist.txt` and `docs/testing_checklist.txt` before assuming an API is complete.

## Quick Start

Install the expected Windows toolchain first:

- Git
- CMake 3.20 or newer
- Ninja
- MSYS2 UCRT64 toolchain with g++
- Visual Studio Code, optional

Clone dependencies:

```powershell
git submodule update --init --recursive
```

Run CMake from an environment where the MSYS2 UCRT64 `g++` is first on `PATH`. If CMake selects the wrong compiler, clear the build directory and configure again from the correct shell/environment.

Configure and build:

```powershell
cmake -S . -B build -G Ninja `
  -DASSERTS_ENABLED=ON `
  -DENABLE_LIBRARY_COVERAGE=OFF `
  -DBUILD_LIBRARY_DOCS=OFF

cmake --build build
```

Run the normal automated test pass:

```powershell
ctest --test-dir build --output-on-failure
```

You can also run the test executable directly:

```powershell
.\build\GameWIP.exe --no-manual-ui
```

For the current FileSystem phase:

```powershell
.\build\GameWIP.exe --filesystem-only
```

Test reports are written under:

```text
logs/tests/latest_test_report.txt
```

## Documentation

Start here when joining the project:

- `docs/vision.txt` - what the game is meant to become.
- `docs/roadmap.txt` - planned development order toward V1.
- `docs/decisions.txt` - stable architecture, tooling, naming, and workflow decisions.
- `docs/implementation_checklist.txt` - what code exists.
- `docs/testing_checklist.txt` - what behavior has been validated.
- `docs/platform_backend_contract.txt` - platform backend rules.

Generated Doxygen docs are optional:

```powershell
cmake -S . -B build-docs -G Ninja -DBUILD_LIBRARY_DOCS=ON
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

Keep implementation and validation separate:

- Update `docs/implementation_checklist.txt` when code exists.
- Update `docs/testing_checklist.txt` only when behavior is tested, manually checked, measured, or inspected.
- Put long-term architecture decisions in `docs/decisions.txt`.
- Put roadmap direction in `docs/roadmap.txt`.

Keep platform-specific code behind internal backend headers. Windows backend code should use explicit Unicode Win32 APIs and avoid generic A/W macro-mapped calls.

Commit messages should follow the project style from `docs/decisions.txt`:

```text
area: imperative summary
```

Examples:

```text
filesystem: add strict symlink entry queries
docs: add private onboarding README
tests: cover filesystem symlink policies
```
