@page project_command_line_tools Command-line tools

GameWIP has two repository helpers and three executables with public command
lines. This page collects their complete syntax in one place. It also explains
what the project helper adds around the underlying CMake, test, benchmark, Git,
and GitHub commands.

For routine project work, prefer `gamewip.bat`: it validates catalog values,
prints the native commands it runs, preserves logs and run metadata, and gives
consistent failure hints. Direct CMake and executable commands remain supported
when that extra orchestration is not needed. The linked workflow pages explain
the behavior behind each command in more depth.

## Discover the available commands

Run these commands from the repository root in PowerShell:

```powershell
.\setup.bat help
.\gamewip.bat help
.\gamewip.bat list
.\build\dev\GameWIP.exe --help
.\build\test\GameWIPTests.exe --help
.\build\benchmark\GameWIPBenchmarks.exe --help
```

These commands only print information; they do not start the game, run tests,
or collect benchmark timings. An executable must first have been built by its
owning preset. The helper can do that on demand for supported run actions when
`-BuildIfMissing` is present.

## Entry points

| Entry point | Use it for | Deeper behavior |
| --- | --- | --- |
| `setup.bat` | Install, update, repair, verify, or remove the supported development environment. | @ref project_environment_setup |
| `gamewip.bat` | Run repository-local build, validation, maintenance, analysis, and automation workflows. | The action reference below |
| `GameWIP.exe` | Print build identity, show help, run opt-in embedded validation, and start the game runtime. | @ref project_game_executable |
| `GameWIPTests.exe` | Run all or selected correctness modules. | @ref project_validation and @ref project_testing |
| `GameWIPBenchmarks.exe` | Run the Google Benchmark command-line interface directly. | @ref project_benchmarking |

## Project helper syntax

```powershell
.\gamewip.bat [action] [options]
```

With no action, the helper opens its interactive menu. These forms print help without performing work:

```powershell
.\gamewip.bat help
.\gamewip.bat --help
.\gamewip.bat -h
.\gamewip.bat -?
```

`gamewip.bat list` prints the current IDs from `CMakePresets.json` and
`scripts/config/gamewip-commands.psd1`. Use it rather than guessing a preset,
module, project command, bundle, benchmark profile, or guarded workflow name.

## Helper actions

| Action | Default or required selection | Behavior |
| --- | --- | --- |
| `menu` | Default action | Opens the interactive project menu. |
| `doctor` | None | Verifies Git metadata, disk state, and exact UCRT64/CLANG64 tools used by project workflows. |
| `git` | `-GitAction menu` | Opens or runs guarded branch, update, history, publish, and cleanup operations. |
| `workflow` | `-WorkflowAction menu` | Lists, previews, dispatches, and watches approved GitHub workflows. |
| `unicode` | `-UnicodeAction menu` | Shows, verifies, or regenerates the pinned Unicode data table. |
| `format` | `-FormatAction check` | Checks or applies `.clang-format` to maintained C/C++ source roots. |
| `links` | None | Runs the maintained local Markdown-link checker with the project Python toolchain. |
| `configure` | `-Preset test` | Configures one visible CMake preset. |
| `build` | `-Preset test` | Configures a missing tree and builds one preset. |
| `test` | `-Preset test` | Configures and builds a missing tree, then runs one CTest preset. |
| `wizard` | None | Interactively builds and optionally runs a supported `GameWIPTests.exe` command. |
| `module` | `-Module all` | Runs all correctness modules or one named module without a retained report. |
| `stress` | `-Module all`, 32 runs, 8 workers | Repeats a validation selection with bounded parallel workers. |
| `run` | `-ProjectCommand benchmark-dry-run` | Runs one cataloged executable-backed command. |
| `bundle` | `-Bundle quick` | Runs one cataloged multi-step workflow. |
| `docs` | Fixed `docs` preset | Configures and builds the generated manual. |
| `analysis` | Fixed `analyze` preset | Configures and builds static-analysis and formatting checks. |
| `analyze` | Alias for `analysis` | Runs the same analysis workflow. |
| `coverage` | Fixed `coverage` workflow | Builds and runs correctness tests, then generates coverage reports. |
| `asan` | Fixed `asan` workflow | Builds and runs CLANG64 AddressSanitizer validation. |
| `benchmark` | `-BenchmarkAction run`, `-BenchmarkProfile standard` | Builds, lists, validates, measures, or compares benchmarks. |
| `list` | None | Lists actions, presets, modules, project commands, profiles, bundles, and workflows. |
| `help` | None | Prints command syntax and options. |

### Git actions

```powershell
.\gamewip.bat git -GitAction <menu|status|fetch|switch|update|cleanup|create|push|log> `
  [-GitBranch <name>]
```

`switch` and `create` accept `-GitBranch`. The interactive menu can fetch and prune references, switch or create branches, fast-forward the current branch, publish or push it, show recent history, and delete eligible local branches. Protected integration branches cannot be deleted. A branch whose upstream disappeared after a squash merge requires separate force-delete confirmation rather than being assumed safe.

### Guarded workflow actions

```powershell
.\gamewip.bat workflow -WorkflowAction <menu|list|status|run> `
  [-Workflow <id>] `
  [-WorkflowKind <all|issue|pull_request>] `
  [-WorkflowNumber <number>] `
  [-ReleaseCommit <40-character-sha>] `
  [-Preview]
```

`run` requires a cataloged `-Workflow`. `-WorkflowKind` and `-WorkflowNumber` narrow project reconciliation. Release finalization uses `-ReleaseCommit`. `-Preview` validates and prints the dispatch, watch, and verification commands without contacting GitHub. Mutating operations retain their typed local confirmation and protected-environment approval requirements. Workflow-specific contracts remain on @ref project_repository_automation, @ref project_release_automation, and @ref project_documentation.

### Unicode actions

```powershell
.\gamewip.bat unicode -UnicodeAction <menu|status|verify|regenerate> `
  [-RefreshUnicodeData] `
  [-UnicodeDataRoot <path>] `
  [-PythonPath <path>] `
  [-ClangFormatPath <path>]
```

`verify` reproduces the checked-in table from the pinned Unicode release and compares it byte-for-byte. `regenerate` replaces the tracked table only when generated output differs. `-RefreshUnicodeData` downloads and extracts a fresh archive. `-UnicodeDataRoot`, environment variable `GAMEWIP_UNICODE_DATA_ROOT`, and the configured cache path select the source-data root in that precedence order. Explicit Python and clang-format paths override environment/configured tool resolution for the action.

### Formatting and Markdown links

```powershell
.\gamewip.bat format -FormatAction <check|apply> [-ClangFormatPath <path>]
.\gamewip.bat links [-PythonPath <path>]
```

Formatting covers maintained C/C++ files under `foundation/`, `tools/`, `engine/`, and `game/`. Check mode is the default and does not rewrite files. Link validation checks maintained relative Markdown targets and returns nonzero when a target is missing. Repository and documentation standards beyond links remain documented on @ref project_static_analysis.

### Configure, build, and CTest

```powershell
.\gamewip.bat configure -Preset <name>
.\gamewip.bat build -Preset <name>
.\gamewip.bat test -Preset <name>
```

The default is `test`. Configure and build accept every visible configure/build preset. Test accepts `test`, `coverage`, and `asan`. Build and test create a missing configure tree automatically. Preset composition and artifacts are owned by @ref project_build.

### Modules and stress runs

```powershell
.\gamewip.bat module -Module <name> [-ExtraArgs <arguments>] [-BuildIfMissing]
.\gamewip.bat stress -Module <name> [-Count <1..100000>] [-Parallel <1..256>] `
  [-ExtraArgs <arguments>] [-BuildIfMissing]
```

The default module is `all`. `module` forwards `--no-test-report` plus `-ExtraArgs`. `stress` defaults to 32 runs and 8 workers, retains one stdout/stderr log pair per run, and reports launched, active, completed, and failed counts. `-BuildIfMissing` configures/builds the test executable when necessary. Public runner options are listed under @ref project_validation; internal child protocol selectors are not user-facing commands.

### Project commands

```powershell
.\gamewip.bat run -ProjectCommand <id> [-ExtraArgs <arguments>] [-BuildIfMissing]
```

| ID | Fixed invocation | Extra arguments | Workspace temp |
| --- | --- | --- | --- |
| `test-all` | `GameWIPTests.exe --no-test-report` | Accepted | Enabled |
| `benchmark-dry-run` | `GameWIPBenchmarks.exe --benchmark_dry_run` | Accepted | Enabled |
| `dev-version` | Development `GameWIP.exe --version` | Rejected | Unchanged |
| `release-version` | Release `GameWIP.exe --version` | Rejected | Unchanged |

The default project command is `benchmark-dry-run`. `-BuildIfMissing` configures and builds the command's owning preset before execution.

### Bundles

```powershell
.\gamewip.bat bundle -Bundle <id>
```

| ID | Steps |
| --- | --- |
| `quick` | Configure, build, and run CTest for `test`. |
| `sanitizer` | Configure, build, and run CTest for `asan`. |
| `local-release-check` | Test; coverage build, tests, and report; analysis; documentation; benchmark registration dry run; release-version check. |

The default bundle is `quick`. Bundle definitions may compose configure, build, build-target, CTest, project-command, benchmark, and acyclic nested-bundle steps.

### Benchmarks

```powershell
.\gamewip.bat benchmark `
  [-BenchmarkAction <run|dry-run|list|compare>] `
  [-BenchmarkProfile <quick|standard|stable>] `
  [-Filter <regex>] `
  [-Repetitions <count>] `
  [-MinTime <time>] `
  [-AggregatesOnly] `
  [-Output <path>] `
  [-OutputFormat <json|csv>] `
  [-NoBuild] `
  [-ExtraArgs <arguments>] `
  [-Baseline <before.json>] `
  [-Candidate <after.json>]
```

`compare` requires both baseline and candidate JSON files. `-ExtraArgs` is for Google Benchmark options not already managed by the helper; it rejects helper-owned output, format, filter, repetitions, minimum-time, aggregate, random-interleaving, and dry-run prefixes so one invocation cannot provide conflicting values. Profile definitions, measurement rules, retained results, and comparison behavior are owned by @ref project_benchmarking.

## Catalog values

### Presets

Configure/build presets are `dev`, `test`, `benchmark`, `profile`, `release`, `coverage`, `asan`, `analyze`, and `docs`. CTest presets are `test`, `coverage`, and `asan`.

### Validation modules

Current modules are `assert`, `base`, `filesystem`, `io`, `logger`, `runner`, `terminal`, `test_support`, `unicode`, and `window`. Use `all` with
helper module/stress actions to select the complete set. Runtime execution uses the stable order `base`, `runner`, `io`, `unicode`, `filesystem`,
`terminal`, `window`, `test_support`, `logger`, and `assert`.

### Benchmark profiles

| Profile | Repetitions | Minimum time | Aggregate-only | Random interleaving |
| --- | ---: | --- | --- | --- |
| `quick` | 1 | `0.05s` | No | No |
| `standard` | 5 | `0.2s` | Yes | No |
| `stable` | 10 | `1s` | Yes | Yes |

### Manual workflows

Cataloged workflow IDs are `validation`, `project-dry-run`, `project-write`, `release-check`, `release-prepare`, `release-finalize-dry-run`, `release-finalize`, and `docs-deploy`. `gamewip.bat workflow -WorkflowAction list` prints their current safety class.

## Global execution behavior

`-NoWorkspaceTemp` prevents actions from replacing `TEMP` and `TMP` with `build/gamewip-temp`. By default, validation-oriented helper actions use that workspace-owned temporary root so local OS-temp permissions do not make FileSystem, Logger, or process tests fail spuriously.

Every native step prints its exact command, streams output, and records a run under:

```text
build/tool-runs/<timestamp>_<action>/
  logs/
  artifacts/
  manifest.json
  summary.json
  summary.txt
```

The manifest retains individual native exit codes, command lines, durations, logs, and outputs. A noninteractive helper failure prints focused recovery hints and exits `1`; successful actions exit `0`. Interactive menu actions catch and display a failed selection so the menu can continue. Cancelled interactive choices do not run the selected operation.

## Executable commands

### Game runtime

```powershell
.\build\dev\GameWIP.exe
.\build\dev\GameWIP.exe --version
.\build\dev\GameWIP.exe --help
.\build\dev\GameWIP.exe --startup-tests [GameWIPTests options]
```

`--help`, `-h`, and `-?` print help without initializing validation, logging, Window, or runtime services. `--version` is also utility-only. Startup validation is build-dependent and opt-in. See @ref project_game_executable.

### Correctness runner

```powershell
.\build\test\GameWIPTests.exe --help
.\build\test\GameWIPTests.exe
.\build\test\GameWIPTests.exe --test-module=filesystem --no-test-report
```

Help exits successfully without running a module or creating a report. No options runs every registered module and writes the default report. See @ref project_validation for all public flags and exact selection, reporting, child-routing, and exit behavior.

### Benchmark runner

```powershell
.\build\benchmark\GameWIPBenchmarks.exe --help
.\build\benchmark\GameWIPBenchmarks.exe --benchmark_dry_run
.\build\benchmark\GameWIPBenchmarks.exe --benchmark_filter=BM_Unicode
```

The standalone executable exposes the pinned Google Benchmark command line. Prefer `gamewip.bat benchmark` for repeatable local measurements and retained metadata. See @ref project_benchmarking.

## Maintainer rules

`scripts/GameWIP.ps1` owns helper parsing and execution. `scripts/config/gamewip-commands.psd1` owns project commands, bundles, benchmark profiles, modules, and guarded workflows. Keep `gamewip.bat` as a small Windows launcher and help-alias adapter.

When changing a public command:

- Update parsing, help, this page, and the nearest task-oriented summary together.
- Keep one authoritative behavior owner and link to it instead of copying complete contracts.
- Add or update regression coverage for help, defaults, validation, and failure behavior.
- Keep arbitrary command, workflow, ref, and input forwarding disabled unless the project deliberately expands that safety boundary.
- Preserve exact native commands and exit evidence in retained run manifests.

## Related pages

- @ref project_getting_started
- @ref project_environment_setup
- @ref project_build
- @ref project_game_executable
- @ref project_validation
- @ref project_testing
- @ref project_benchmarking
- @ref project_static_analysis
