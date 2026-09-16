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

Configure and build the development preset, then run the executable:

```powershell
cmake --preset dev
cmake --build --preset dev
.\build\dev\GameWIP.exe
```

The executable's `--version` option checks the generated build identity. Run it
without arguments to open the development window.

The executable reports connected-display modes and HDR/color capabilities,
then opens a borderless-fullscreen window at the desktop resolution. Press
`Alt+F4` to close it. See @ref project_game_executable for the complete runtime
sequence and failure behavior.

The development preset builds the game and compiles embedded tests for explicit
`--startup-tests` execution. Release builds exclude validation, benchmarks,
assertions, and Tracy.

Use @ref project_command_line_tools to discover every supported helper and executable command, including safe help invocations that do not start the
runtime or test suite.

## Run validation

Configure and build the test preset, then run the registered validation suite:

```powershell
cmake --preset test
cmake --build --preset test
ctest --preset test
```

The validation architecture, module registration, child-process routing, report
paths, and startup behavior are documented in @ref project_validation. Test
authoring rules are documented in @ref project_testing.

## Read the source tree

The ownership map and dependency direction are documented in @ref
project_structure. Start there before moving code between source areas.

## What to read next

After the first build and validation run, @ref project_structure explains
ownership and dependency direction. The build, command-line, validation, testing,
library, extension, and documentation pages cover the corresponding work in
more detail. The @ref project_quality_workflows and @ref project_contracts pages
collect the quality, repository, and release workflows. Contribution and merge
guidance lives in @ref project_contributing.
