@page project_getting_started Getting started with the project

Use this page to take a fresh checkout through setup, its first build, and an
initial validation run. The linked pages hold the detailed contracts and
workflow reference.

## First checkout setup

Run the repository setup entry point after cloning. The fresh-machine flow,
editor selection, repair and update actions, and command options are documented
in @ref project_environment_setup.

```powershell
.\setup.bat
```

VS Code users should open `GameWIP.code-workspace`. The setup utility prepares
the `dev` compilation database used by workspace IntelliSense.

## Configure, build, and run

From PowerShell, use the helper to build the `dev` preset with the configured
toolchain (see @ref project_build for all presets), then run the executable:

```powershell
.\gamewip.bat build dev
.\build\dev\GameWIP.exe
```

From MSYS2 UCRT64 Bash, the equivalent direct commands are:

```bash
cmake --preset dev
cmake --build --preset dev
./build/dev/GameWIP.exe
```

The executable's `--version` option prints the generated build identity. Run it
without arguments to open the development window.

The executable reports connected-display modes and HDR/color capabilities,
then opens a borderless-fullscreen window at the desktop resolution. Press
`Alt+F4` to close it. See @ref project_game_executable for the complete runtime
sequence and failure behavior.

The `dev` preset also compiles embedded tests for `--startup-tests`. Release
builds exclude validation, benchmarks, assertions, and Tracy. All commands,
including safe `--help` forms, are in @ref project_command_line_tools.

## Run validation

In PowerShell:

```powershell
.\gamewip.bat test test
```

In MSYS2 UCRT64 Bash:

```bash
cmake --preset test
cmake --build --preset test
ctest --preset test
```

The validation architecture, module registration, child-process routing, report
paths, and startup behavior are documented in @ref project_validation. Test
authoring rules are documented in @ref project_testing.

## What to read next

After the first build and validation run, read @ref project_structure before
moving code between areas. @ref project_quality_workflows and @ref
project_contracts cover quality, repository, and release workflows; @ref
project_contributing covers merge guidance.
