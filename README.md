# GameWIP

[![Latest release](https://img.shields.io/github/v/release/Andrei11033/GameWIP?display_name=tag&sort=semver)](https://github.com/Andrei11033/GameWIP/releases/latest)
[![Validation](https://github.com/Andrei11033/GameWIP/actions/workflows/validation.yml/badge.svg?branch=master)](https://github.com/Andrei11033/GameWIP/actions/workflows/validation.yml)
[![Documentation](https://github.com/Andrei11033/GameWIP/actions/workflows/docs.yml/badge.svg?branch=master)](https://andrei11033.github.io/GameWIP/)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)

GameWIP is an early-stage C++23 sandbox game project about player-built
vehicles, structures, weapons, components, and meaningful destruction.

The game currently targets Windows 11. The helpers can select Linux AMD64 and
ARM64 compiler targets from Windows or Linux; full Linux builds await Linux
platform backends. GameWIP is pre-1.0; active work is tracked in the
[R01 milestone](https://github.com/Andrei11033/GameWIP/milestone/176) and the
[roadmap](docs/roadmap.md).

## Setup

From a fresh checkout or extracted ZIP, run the repository setup utility.
Windows:

```powershell
.\setup.bat
```

Linux:

```bash
.\setup.sh
```

Setup prepares the development environment and pinned dependencies. Windows setup also prepares profiler tools.
Linux setup supports Ubuntu, Debian, and CachyOS. If PowerShell 7 is missing,
`setup.sh` offers to install its verified runtime. See the
[development environment guide](docs/doxygen/environment_setup.md).

The `dev` preset is the main build target.
In PowerShell, the helper defaults to Windows AMD64 and keeps each target in its own build tree:

```powershell
.\gamewip.bat target -TargetPlatform windows -TargetArchitecture amd64
.\gamewip.bat build dev
.\build\windows-amd64\dev\GameWIP.exe
```

Use `arm64` instead of `amd64` to cross-compile for Windows ARM64. Setup installs
the pinned LLVM-MinGW toolchain when needed.

On Linux, setup prepares the host tools and both cross-compilers. To build the
Windows AMD64 target:

```bash
bash ./gamewip.sh target -TargetPlatform windows -TargetArchitecture amd64
bash ./gamewip.sh build dev
```

Or use CMake directly from MSYS2 UCRT64 Bash:

```bash
cmake --preset dev
cmake --build --preset dev
./build/native/dev/GameWIP.exe
```

See the [getting started guide](docs/doxygen/getting_started.md), the
[build guide](docs/doxygen/build.md) (all presets, including `docs`), and the
[command-line tools reference](docs/doxygen/command_line_tools.md).

## Documentation

The API reference and developer manual are on the
[documentation site](https://andrei11033.github.io/GameWIP/). Public headers
carry short IntelliSense contracts; manual pages cover the surrounding design
(see the [documentation guide](docs/doxygen/documentation.md)).

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
engine/       Engine libraries and retained reference code.
tools/        Diagnostics, logging, assertions, and test support.
game/         Game executable and source-tree validation.
cmake/        Project build and validation infrastructure.
external/     CMake integration for optional FetchContent dependencies.
docs/         Product direction and developer documentation.
build/        Setup-managed dependency caches and generated build output.
```

`engine/desktop` and `engine/input` are the supported, maintained engine
libraries. `engine/action` and `engine/window_manager` remain reference sources
excluded from normal builds and are not supported consumer APIs. See the
[engine boundary in the developer manual](docs/doxygen/project_structure.md#engine-libraries-and-reference-code).

## License

GameWIP first-party source code and documentation are licensed under the
[Apache License 2.0](LICENSE). See [NOTICE](NOTICE) for project attribution.
Setup-managed third-party dependency sources retain their own licenses and
notices in the local dependency cache.
