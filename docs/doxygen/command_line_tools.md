@page project_command_line_tools Command-line tools

GameWIP exposes one repository helper, `gamewip.bat`, plus the Windows environment bootstrap `setup.bat`.
The project helper uses positional command words for selection and shared switches for execution policy. With no action it
opens the interactive menu.

`gamewip.bat --help`, `gamewip.bat -h`, and `gamewip.bat -?` are help aliases.

Both interactive menus render declared key/label entries through the same
shared console primitive. The project menu hierarchy lives in
`scripts/config/commands.json`; setup menu entries live in
`scripts/setup/config/setup.json`. Their schemas and runtime checks reject
duplicate keys, unknown handlers, and incomplete menu catalogs before use.

## Common syntax

```powershell
.\gamewip.bat <action> [command] [target] [options]
```

Examples:

```powershell
.\gamewip.bat build test
.\gamewip.bat test test
.\gamewip.bat module unicode
.\gamewip.bat quality check
.\gamewip.bat tools status
.\gamewip.bat tools ensure all
.\gamewip.bat workflow run release-check -Preview
.\gamewip.bat runs show latest
```

Selection words are positional. Do not use the retired nested selector switches such as `-Preset`, `-Module`, or action-specific `-*Action` options.

## Actions

| Action | Purpose |
| --- | --- |
| `menu` | Open the interactive project menu. |
| `doctor` | Check complete project readiness. |
| `git` | Run guarded repository operations such as status, fetch, switch, update, cleanup, create, push, and log. |
| `workflow` | List, inspect, preview, or dispatch approved GitHub workflows. |
| `unicode` | Inspect, verify, or regenerate pinned Unicode data. |
| `format` | Check or apply maintained C/C++ formatting. |
| `quality` | Run, fix, or summarize the repository quality policy. |
| `tools` | List tools, report status, check upstream versions, ensure declared versions, or update reviewed pins. |
| `links` | Run the maintained Markdown-link checker. |
| `configure` | Configure one visible CMake preset. |
| `build` | Ensure configuration and build one preset. |
| `test` | Ensure prerequisites and run one CTest preset. |
| `module` | Run all correctness modules or one named module. |
| `wizard` | Interactively assemble a supported validation invocation. |
| `stress` | Repeat a validation module with bounded parallelism. |
| `run` | Run one declarative project command. |
| `bundle` | Run one declarative multi-step bundle. |
| `docs` | Build generated documentation. |
| `analyze` | Run the supported C++ static-analysis preset. |
| `coverage` | Run the coverage validation workflow. |
| `asan` | Run the CLANG64 AddressSanitizer workflow. |
| `benchmark` | Measure, dry-run, list, or compare benchmarks. |
| `runs` | List, inspect, or clean owned helper run history. |
| `list` | Print the current action/catalog values. |
| `help` | Print helper usage. |

## Subcommands and targets

The second positional word is the action-specific command or selection; the third is used only when another selector is required.

```powershell
.\gamewip.bat git status
.\gamewip.bat git switch feature/example
.\gamewip.bat workflow list
.\gamewip.bat workflow run release-check
.\gamewip.bat unicode verify
.\gamewip.bat format check
.\gamewip.bat quality check
.\gamewip.bat quality status
.\gamewip.bat tools status
.\gamewip.bat tools ensure quality
.\gamewip.bat tools update all -Preview
.\gamewip.bat configure test
.\gamewip.bat build test
.\gamewip.bat test test
.\gamewip.bat test test -Fresh
.\gamewip.bat module filesystem
.\gamewip.bat stress logger
.\gamewip.bat run benchmark-dry-run
.\gamewip.bat bundle quick
.\gamewip.bat benchmark dry-run
.\gamewip.bat benchmark compare -Baseline before.json -Candidate after.json
.\gamewip.bat runs list
.\gamewip.bat runs list all
.\gamewip.bat runs show latest
.\gamewip.bat runs clean all
```

Use `gamewip.bat list` for current presets, modules, project commands, bundles, benchmark profiles, and guarded workflows.

## Shared options

| Option | Purpose |
| --- | --- |
| `-Command <value>` | Explicitly bind the second positional selector when scripting. |
| `-Target <value>` | Explicitly bind the third positional selector when scripting. |
| `-PythonPath <path>` | Override Python resolution for supported maintenance work. |
| `-PythonProviderHostPath <path>` | Override the Python host used to provision managed Python tools. |
| `-ClangFormatPath <path>` | Override clang-format resolution. |
| `-UnicodeDataRoot <path>` | Override the pinned Unicode source-data root. |
| `-RefreshUnicodeData` | Refresh official Unicode data before verification/regeneration. |
| `-WorkflowKind <all\|issue\|pull_request>` | Narrow project-reconciliation workflow scope. |
| `-WorkflowNumber <number>` | Select the issue or pull request for a narrowed workflow. |
| `-ReleaseCommit <sha>` | Supply the exact release-finalization commit. |
| `-BenchmarkProfile <quick\|standard\|stable>` | Select benchmark policy. |
| `-Filter <regex>` | Select benchmark names. |
| `-Repetitions <count>` | Override benchmark repetitions. |
| `-MinTime <value>` | Override benchmark minimum measurement time. |
| `-Output <path>` | Select an explicit retained benchmark/comparison output. |
| `-OutputFormat <json\|csv>` | Select benchmark result format. |
| `-AggregatesOnly` | Request aggregate benchmark rows only. |
| `-Baseline <path>` | Select the comparison baseline JSON. |
| `-Candidate <path>` | Select the comparison candidate JSON. |
| `-Count <count>` | Select stress-run count. |
| `-Parallel <count>` | Select stress worker count. |
| `-ExtraArgs <arguments>` | Forward arguments only where the selected declarative command permits them. |
| `-NoBuild` | Do not build prerequisites automatically; require existing usable build state and fail when it is absent. |
| `-Fresh` | Before `configure`, `build`, `test`, or `bundle`, remove each selected preset's complete `build/<preset>` tree and recreate it. Cannot be combined with `-NoBuild`. |
| `-StopOnFailure` | Stop launching new stress work after the first failure. |
| `-FailFast` | Stop the quality gate at the first failed check instead of aggregating independent failures. |
| `-Changed` | Restrict supported quality work to ordinary changed maintained files. A changed quality policy expands to the complete maintained scope it can affect. |
| `-Json` | Emit the final structured operation result as JSON. |
| `-NoWorkspaceTemp` | Keep the caller's TEMP/TMP instead of using operation-owned helper temp. Validation and benchmark executables still scope their own fixtures beneath the active preset tree. |
| `-Preview` | Print the planned scope and perform only action-specific read-only discovery or preflight. Do not apply the requested local, tracked, machine, or remote mutation; diagnostic run logs and receipts are still retained under `build/gamewip/runs/`. |
| `-NonInteractive` | Disable prompts. This never grants mutation consent at any risk class. |
| `-Yes` | Approve the printed mutation plan for non-interactive execution. |
| `-Quiet` | Suppress ordinary console presentation while retaining result/log data. |
| `-NoColor` | Disable color-only presentation. Status text remains explicit. |
| `-OutputMode <Summary\|Stream\|LogOnly>` | Select native-process presentation policy. `Stream` is the default, so compiler, linker, test, linter, and installer output remains visible. |

PowerShell common `-Verbose` and `-Debug` behavior remains available. Verbose mode is the normal way to expose additional helper/native detail.

Semantic presentation uses cyan for accents and progress, green for success and
ready states, yellow for warnings and ensure actions, red for failures, and
dark gray for paths and secondary details. `-NoColor` changes only color; the
explicit text and status labels remain unchanged.

## Execution model

Each named operation follows one lifecycle:

1. Discover the requested state.
2. Build the complete plan and preflight it before mutation.
3. Explain network, risk, and mutation scope.
4. Request consent only when required by the action's declared risk.
5. Execute with operation-scoped cancellation, process ownership, logging, and temporary storage.
6. Verify the resulting state.
7. Emit a final receipt with `passed`, `failed`, or `cancelled` status and independent mutation state.

Only one setup or project-helper operation may run at a time. A second command
fails immediately with an explicit `operation-in-progress` diagnostic instead
of reading or changing partially updated tools, build trees, or retained state.

`-NonInteractive` changes prompting only. Every mutating non-interactive command, including `local` build-tree work, still requires `-Yes` unless a higher-level
caller has already granted consent. `-Preview` never performs the mutation.

Low-level configure, build, test, and ordinary bundle commands remain
incremental unless `-Fresh` is supplied. The high-level `coverage` and `asan`
actions always recreate their complete preset trees so stale instrumentation or
runtime artifacts cannot affect authoritative results. The
`local-release-check` and `sanitizer` bundles declare the same policy in the
bundle catalog; `quick` remains incremental. Fresh recreation is deliberately
limited to known, direct children of the repository `build` directory and
refuses reparse points.

## Quality and tool policy

`gamewip quality check` is the authoritative local repository quality gate.
Full quality covers maintained tracked files and non-ignored untracked
first-party files, while preserving the documented generated, historical, and
third-party exclusions. Independent checks aggregate by default; use
`-FailFast` only for focused diagnosis. `gamewip quality fix` applies deterministic formatters and then runs the same gate.
`gamewip quality status` reports maintained-file quality ownership.

`gamewip tools ensure <id|category|all>` installs or repairs exactly the
versions already declared by the checkout and does not advance pins. `gamewip
tools update <id|all>` is the reviewed pin-advancement workflow and requires a
clean tracked tree. Its preview performs complete discovery, source-preserving
tracked staging, and staged validation, then reports exact registry fields and
declared live references without applying them. A current selection is a true
no-op. See @ref project_tools for the registry and provider contracts.

## Run history

Each operation retains its owned evidence under:

```text
build/gamewip/runs/<operation-id>/
  logs/
  artifacts/
  manifest.json
  summary.json
  summary.txt
```

Use `gamewip runs list` for the newest 25 receipts, `gamewip runs list all`
for complete history, `gamewip runs show latest`, and
`gamewip runs clean <selector>` to inspect or clean that owned history.

## Related pages

- @ref project_environment_setup
- @ref project_build
- @ref project_validation
- @ref project_static_analysis
- @ref project_benchmarking
- @ref project_repository_automation
