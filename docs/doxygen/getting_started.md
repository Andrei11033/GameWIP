@page project_getting_started Getting started with the project

Follow this page to turn a fresh checkout into a built, runnable, and validated
GameWIP source tree. It gives the commands and the context needed for a first
pass; the linked pages explain each subsystem in depth.

## First checkout setup

Run the repository setup entry point after cloning:

```powershell
.\setup.bat
```

The fresh-machine flow, editor selection, and repair/update actions are
documented in @ref project_environment_setup. Preset and toolchain contracts
are documented in @ref project_build.

VS Code users should open `GameWIP.code-workspace`. The setup utility prepares
the `dev` compilation database used by workspace IntelliSense.

## Configure, build, and run

Configure and build the development preset:

```powershell
cmake --preset dev
cmake --build --preset dev
```

Print runtime version information:

```powershell
.\build\dev\GameWIP.exe --version
```

Run the development executable:

```powershell
.\build\dev\GameWIP.exe
```

The executable reports connected-display modes and HDR/color capabilities,
then opens a borderless-fullscreen window at the desktop resolution. Press
`Alt+F4` to close it. See @ref project_game_executable for the complete runtime
sequence and failure behavior.

The development preset builds the game and compiles embedded tests for explicit `--startup-tests` execution. Release builds intentionally exclude validation, benchmarks, assertions, and Tracy.

Use @ref project_command_line_tools to discover every supported helper and executable command, including safe help invocations that do not start the runtime or test suite.

## Run validation

Use the validation preset for standalone correctness testing:

```powershell
cmake --preset test
cmake --build --preset test
ctest --preset test
```

Run one module directly when investigating a focused area:

```powershell
.\build\test\GameWIPTests.exe --test-module=filesystem
```

The validation architecture, module registration, child-process routing, report paths, and startup behavior are documented in @ref project_validation. Test authoring rules are documented in @ref project_testing.

## Read the source tree

Start with these directories:

| Path | First meaning |
| --- | --- |
| `foundation/` | Low-level reusable libraries such as Unicode, IO, FileSystem, and Terminal. |
| `tools/` | Logger, Assert, TestSupport, diagnostics, and development support libraries. |
| `game/` | Executable entry point, runtime facade, startup validation wiring, and validation runners. |
| `cmake/` | Project-wide CMake helpers, presets, packaging, validation, docs, and analysis wiring. |
| `docs/doxygen/` | Generated manual pages for workflows and contracts. |
| `docs/` | Vision, roadmap, decisions, versioning, and contributor workflow records. |
| `external/` | Pinned third-party dependencies. |

The full ownership map and dependency direction are documented in @ref project_structure.

## Where output goes

CMake presets write build trees under `build/<preset>`.

Common outputs include:

- `build/dev/GameWIP.exe`
- `build/test/GameWIPTests.exe`
- `build/benchmark/GameWIPBenchmarks.exe`
- `build/docs/docs/doxygen/html/index.html`

Generated files and build artifacts should stay in build directories unless a workflow page explicitly documents a retained source artifact.

## What to read next

After the first build and validation run:

- Read @ref project_structure to understand ownership and dependency direction.
- Read @ref project_build before changing presets, options, runtime dependency copying, or build outputs.
- Read @ref project_command_line_tools to find complete helper and executable syntax.
- Read @ref project_validation and @ref project_testing before changing tests or startup validation.
- Read @ref project_reusable_libraries before using or changing a first-party library.
- Read @ref project_extending before adding APIs, libraries, backends, benchmarks, packages, or manual pages.
- Read @ref project_documentation before writing public API comments or manual pages.

Coverage, profiling, benchmarking, and static analysis are documented under @ref project_quality_workflows. Repository and release automation are documented under @ref project_contracts.

## Change workflow

Normal project work uses a short-lived branch, a pull request, concrete validation notes, and a squash merge. Use @ref project_contributing for issue, branch, pull-request, label, automation, and merge-message rules.

When a change modifies behavior, update the owning manual page and run the preset that proves the change. A vague statement such as `tested` is not useful validation evidence; record the exact commands or inspections performed.
