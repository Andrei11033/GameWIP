# GameWIP

[![Latest release](https://img.shields.io/github/v/release/Andrei11033/GameWIP?display_name=tag&sort=semver)](https://github.com/Andrei11033/GameWIP/releases/latest)
[![Validation](https://github.com/Andrei11033/GameWIP/actions/workflows/validation.yml/badge.svg?branch=master)](https://github.com/Andrei11033/GameWIP/actions/workflows/validation.yml)
[![Documentation](https://github.com/Andrei11033/GameWIP/actions/workflows/docs.yml/badge.svg?branch=master)](https://andrei11033.github.io/GameWIP/)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

GameWIP is an early-stage C++23 sandbox game project about player-built
vehicles, structures, weapons, components, and meaningful destruction.

I use the project to learn C++ and systems programming while keeping API
contracts, ownership, builds, and validation deliberately strict.

GameWIP supports Windows 11 and is pre-1.0. Active work is tracked
in the [R01 milestone](https://github.com/Andrei11033/GameWIP/milestone/176)
and the [roadmap](docs/roadmap.md).

## Development approach

I write the implementation myself and docs. I use AI to help with research, review,
tests and maintenance.

## Start here

From a fresh checkout or extracted ZIP, run the repository bootstrap utility:

```powershell
.\setup.bat
```

Setup prepares the development environment, pinned dependencies, and profiler
tools, and handles updates, repair, and uninstall. See the
[development environment guide](docs/doxygen/environment_setup.md).

Open `GameWIP.code-workspace`, then from an MSYS2 UCRT64 shell build and run the
`dev` preset:

```powershell
cmake --preset dev
cmake --build --preset dev
.\build\dev\GameWIP.exe
```

See the [getting started guide](docs/doxygen/getting_started.md), the
[build guide](docs/doxygen/build.md) (all presets, including `docs`), and the
[command-line tools reference](docs/doxygen/command_line_tools.md).

## Documentation

The API reference and developer manual are on the
[documentation site](https://andrei11033.github.io/GameWIP/); the
[manual index](docs/doxygen/index.md) is the entry point. Public headers carry
short contracts for IntelliSense; manual pages cover the surrounding design (see
the [documentation guide](docs/doxygen/documentation.md)).

## Project links

- [Developer manual](https://andrei11033.github.io/GameWIP/)
- [Contributing guide](CONTRIBUTING.md)
- [Roadmap](docs/roadmap.md)
- [Issues](https://github.com/Andrei11033/GameWIP/issues)
- [Security policy](SECURITY.md)
- [Releases](https://github.com/Andrei11033/GameWIP/releases)

## Repository layout

```text
foundation/   Reusable low-level libraries.
engine/       Desktop (supported). Input, Action, WindowManager are legacy reference code; do not use.
tools/        Diagnostics, logging, assertions, and test support.
game/         Game executable and source-tree validation.
cmake/        Project build and validation infrastructure.
external/     CMake integration for optional FetchContent dependencies.
docs/         Product direction and developer documentation.
build/        Setup-managed dependency caches and generated build output.
```

`engine/desktop` is the supported, maintained engine library. `engine/input` and
`engine/action` are retained reference targets, and `engine/window_manager` is
retained reference source that is not compiled. None of these legacy targets
should be used for new code; see the [engine boundary in the developer manual](docs/doxygen/project_structure.md#engine-libraries-and-reference-code).

The root entry points stay short; detailed subsystem contracts and workflows
live in the [developer manual](docs/doxygen/index.md).

## License

GameWIP first-party source code and documentation are licensed under the
[Apache License 2.0](LICENSE). See [NOTICE](NOTICE) for project attribution.
Setup-managed third-party dependency sources retain their own licenses and
notices in the local dependency cache.
