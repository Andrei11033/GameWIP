# GameWIP

[![Latest release](https://img.shields.io/github/v/release/Andrei11033/GameWIP?display_name=tag&sort=semver)](https://github.com/Andrei11033/GameWIP/releases/latest)
[![Validation](https://github.com/Andrei11033/GameWIP/actions/workflows/validation.yml/badge.svg?branch=master)](https://github.com/Andrei11033/GameWIP/actions/workflows/validation.yml)
[![Documentation](https://github.com/Andrei11033/GameWIP/actions/workflows/docs.yml/badge.svg?branch=master)](https://andrei11033.github.io/GameWIP/)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

GameWIP is an early-stage C++23 sandbox game project about player-built
vehicles, structures, weapons, components, and meaningful destruction.

I use the project to learn C++ and systems programming while keeping API
contracts, ownership, builds, and validation deliberately strict.

GameWIP supports Windows 11 and is currently pre-1.0. Active work is tracked
in the [R01 milestone](https://github.com/Andrei11033/GameWIP/milestone/176)
and the [roadmap](docs/roadmap.md). The latest published baseline is
[v0.0.1](docs/releases/v0.0.1.md).

## Development approach

I write the implementation myself. I only use AI for learning,
research, review and to help me make shure the documentation and comments are correct and worded nicely.

## Start here

From a fresh checkout or extracted ZIP, run the repository bootstrap utility:

```powershell
.\setup.bat
```

Setup prepares the selected development environment, pinned dependencies, and
profiler tools. It is also the supported update, repair, and ownership-aware
uninstall entry point. See the [development environment guide](docs/doxygen/environment_setup.md)
for the setup actions and their boundaries.

Open `GameWIP.code-workspace`, then configure, build, and run the development
preset:

```powershell
cmake --preset dev
cmake --build --preset dev
.\build\dev\GameWIP.exe --version
.\build\dev\GameWIP.exe
```

The [getting started guide](docs/doxygen/getting_started.md) explains the
first-checkout path. Use the [command-line tools reference](docs/doxygen/command_line_tools.md)
for setup, project-helper, game, test, and benchmark commands.

## Documentation

Generated API documentation and the developer manual are published at the
[GameWIP documentation site](https://andrei11033.github.io/GameWIP/). Start
with the [manual index](docs/doxygen/index.md) for architecture,
subsystem workflows, API contracts, and engineering decisions.

Public headers keep important contracts discoverable through IntelliSense;
manual pages explain how the surrounding subsystem fits together. The
[documentation system guide](docs/doxygen/documentation.md) defines that
division of responsibility.

To build the manual locally, use the `docs` preset described in the
[build guide](docs/doxygen/build.md).

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
engine/       Supported Desktop plus deprecated Input, Action, and WindowManager code.
tools/        Diagnostics, logging, assertions, and test support.
game/         Game executable and source-tree validation.
cmake/        Project build and validation infrastructure.
docs/         Product direction and developer documentation.
external/     Pinned third-party dependencies.
```

The root entry points stay short; detailed subsystem contracts and workflows
live in the [developer manual](docs/doxygen/index.md).

## License

GameWIP first-party source code and documentation are licensed under the
[Apache License 2.0](LICENSE). See [NOTICE](NOTICE) for project attribution.
Third-party dependencies under `external/` retain their own licenses and
notices.
