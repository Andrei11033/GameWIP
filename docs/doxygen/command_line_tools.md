@page project_command_line_tools Command-line tools

GameWIP exposes the repository helper `gamewip.bat` and the Windows environment
bootstrap `setup.bat`. The project helper uses positional command words for
selection and shared switches for execution policy. With no action, it opens
the interactive project menu.

`gamewip.bat --help`, `gamewip.bat -h`, and `gamewip.bat -?` are help aliases.

The project menu hierarchy lives in `scripts/config/commands.json`; setup menu
entries live in `scripts/setup/config/setup.json`. Their schemas and runtime
checks reject duplicate keys, unknown handlers, and incomplete menu catalogs
before use. The shared menu behavior is described in @ref
project_environment_setup.

Indexed tool selections accept one number or a comma-separated list such as
`2,3,5,6`. Tool previews and updates use one plan, consent decision, and
validation boundary for the whole selection. Press Enter to use the displayed
default, or `q` or Esc to cancel.

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
.\gamewip.bat quality hygiene status
.\gamewip.bat tool status
.\gamewip.bat tool ensure all
.\gamewip.bat workflow run release-check -Preview
.\gamewip.bat history show latest
```

Selection words are positional. The retired nested selector switches, such as
`-Preset`, `-Module`, and action-specific `-*Action` options, are no longer
supported.

## Actions

| Action | Purpose |
| --- | --- |
| `menu` | Open the interactive project menu. |
| `ready` | Check complete project readiness. |
| `git` | Run guarded repository operations such as status, fetch, switch, update, cleanup, create, push, and log. |
| `workflow` | List, inspect, preview, or dispatch approved GitHub workflows. |
| `unicode` | Inspect, verify, or regenerate pinned Unicode data. |
| `format` | Check or apply maintained C/C++ formatting. |
| `quality` | Run, fix, or summarize the repository quality policy. |
| `tool` | List tools, report status, check upstream versions, install or repair declared versions, or update reviewed pins. |
| `links` | Run the maintained Markdown-link checker. |
| `deps` | Check or prepare the shared pinned dependency cache. |
| `config` | Configure one visible CMake preset. |
| `build` | Ensure configuration and build one preset. |
| `test` | Ensure prerequisites and run one CTest preset. |
| `module` | Run all correctness modules or one named module. |
| `wizard` | Interactively assemble a supported validation invocation. |
| `stress` | Repeat a validation module with bounded parallelism. |
| `run` | Run one declarative project command. |
| `bundle` | Run one declarative multi-step bundle. |
| `doc` | Build generated documentation. |
| `analyze` | Run the supported C++ static-analysis preset. |
| `cov` | Run the coverage validation workflow. |
| `asan` | Run the CLANG64 AddressSanitizer workflow. |
| `ubsan` | Run the CLANG64 UndefinedBehaviorSanitizer workflow. |
| `bench` | Measure, dry-run, list, or compare benchmarks. |
| `history` | List, inspect, or clean owned helper run history. |
| `list` | Print the current action/catalog values. |
| `help` | Print helper usage. |

Canonical names are used throughout this manual. Compatibility aliases remain
accepted for existing scripts: `configure`/`cfg`/`c` for `config`, `b` for
`build`, `t` for `test`, `q` for `quality`, `fmt` for `format`, `dependencies`
for `deps`, `benchmark` for `bench`, `mod` for `module`, `wf` for `workflow`,
`ucd` for `unicode`, `g` for `git`, `tools` for `tool`, `docs` for `doc`,
`tidy` for `analyze`, `coverage` for `cov`, `runs`/`hist` for `history`,
`exec` for `run`, `ls` for `list`, and `h` for `help`. The retired `doctor`
command is not accepted; use `ready`.

## Subcommands and targets

The second positional word is the action-specific command or selection. The
third is used only when another selector is required.

The quality-hygiene selector may be `standard`, `deep`, a configured check ID,
`list`, or `status`.

```powershell
.\gamewip.bat git status
.\gamewip.bat git switch feature/example
.\gamewip.bat workflow list
.\gamewip.bat workflow run release-check
.\gamewip.bat unicode verify
.\gamewip.bat format check
.\gamewip.bat quality check
.\gamewip.bat quality status
.\gamewip.bat quality hygiene
.\gamewip.bat quality hygiene standard
.\gamewip.bat quality hygiene deep
.\gamewip.bat quality hygiene unused-includes
.\gamewip.bat quality hygiene list
.\gamewip.bat tool status
.\gamewip.bat tool ensure quality
.\gamewip.bat tool update all -Preview
.\gamewip.bat deps check
.\gamewip.bat deps prepare
.\gamewip.bat config test
.\gamewip.bat build test
.\gamewip.bat config test -Offline
.\gamewip.bat build test -Offline
.\gamewip.bat test test
.\gamewip.bat test test -CleanBuild
.\gamewip.bat module filesystem
.\gamewip.bat stress logger
.\gamewip.bat run benchmark-dry-run
.\gamewip.bat bundle quick
.\gamewip.bat bench dry-run
.\gamewip.bat bench compare -BaselinePath before.json -CandidatePath after.json
.\gamewip.bat history list
.\gamewip.bat history list all
.\gamewip.bat history show latest
.\gamewip.bat history clean all
```

Use `gamewip.bat list` for current presets, modules, project commands, bundles,
benchmark profiles, and guarded workflows. The `wizard` composes common
validation options and applicable module-specific options declared by the helper
catalog. Focused module runs expose those options automatically; all-module runs
offer them only when selected, and skipped modules are not offered. `gamewip.bat
list` shows their option IDs. This metadata controls composition and
presentation. Argument semantics remain owned by the validation runner and
module implementation.

## Shared options

| Option | Purpose |
| --- | --- |
| `-Command <value>` | Explicitly bind the second positional selector when scripting. |
| `-Target <value>` | Explicitly bind the third positional selector when scripting. |
| `-PythonPath <path>` | Override Python resolution for supported maintenance work. |
| `-PythonHostPath <path>` | Override the native Python host used to provision managed Python tools. |
| `-ClangFormatPath <path>` | Override clang-format resolution. |
| `-UnicodeDataPath <path>` | Override the pinned Unicode source-data directory. |
| `-RefreshUnicodeData` | Refresh official Unicode data before verification/regeneration. |
| `-WorkflowKind <all\|issue\|pull_request>` | Narrow project-reconciliation workflow scope. |
| `-ItemNumber <number>` | Select the issue or pull request for a narrowed workflow. |
| `-ReleaseCommit <sha>` | Supply the exact release-finalization commit. |
| `-BenchmarkProfile <quick\|standard\|stable>` | Select benchmark policy. |
| `-NameFilter <regex>` | Select benchmark names. |
| `-Repetitions <count>` | Override benchmark repetitions. |
| `-MinimumTime <value>` | Override benchmark minimum measurement time. |
| `-OutputPath <path>` | Select an explicit retained benchmark/comparison output. |
| `-OutputFormat <json\|csv>` | Select benchmark result format. |
| `-AggregatesOnly` | Request aggregate benchmark rows only. |
| `-BaselinePath <path>` | Select the comparison baseline JSON. |
| `-CandidatePath <path>` | Select the comparison candidate JSON. |
| `-RunCount <count>` | Select stress-run count. |
| `-WorkerCount <count>` | Select stress worker count. |
| `-PassThroughArgs <arguments>` | Forward arguments only where the selected declarative command permits them. |
| `-SkipBuild` | Do not build prerequisites automatically; require existing usable build state and fail when it is absent. |
| `-CleanBuild` | Before `configure`, `build`, `test`, or `bundle`, remove each selected preset's complete `build/<preset>` tree and recreate it. Cannot be combined with `-SkipBuild`. |
| `-FailFast` | Stop the quality gate at the first failed check instead of aggregating independent failures. |
| `-ChangedOnly` | Restrict supported quality work to ordinary changed maintained files. A changed quality policy expands to the complete maintained scope it can affect. |
| `-FailOnFindings` | Fail an optional hygiene audit when it produces a `PROVEN` finding. Likely, informational, and centrally explained findings remain report-only. |
| `-Json` | Emit the final structured operation result as JSON. |
| `-UseCallerTemp` | Keep the caller's TEMP/TMP instead of using operation-owned helper temp. Validation and benchmark executables still scope their own fixtures beneath the active preset tree. |
| `-Preview` | Print the planned scope and perform only action-specific read-only discovery or preflight. Do not apply the requested local, tracked, machine, or remote mutation; diagnostic run logs and receipts are still retained under `build/gamewip/runs/`. |
| `-NonInteractive` | Disable prompts. This never grants mutation consent at any risk class. |
| `-Yes` | Approve the printed mutation plan for non-interactive execution. |
| `-Quiet` | Suppress ordinary console presentation while retaining result/log data. |
| `-NoColor` | Disable color-only presentation. Status text remains explicit. |
| `-OutputMode <Summary\|Stream\|LogOnly>` | Select native-process presentation policy. `Stream` is the default, so compiler, linker, test, linter, and installer output remains visible. |

PowerShell common `-Verbose` and `-Debug` behavior remains available. Verbose
mode is the normal way to expose additional helper or native detail.

Semantic presentation uses cyan for accents and progress, green for success and
ready states, yellow for warnings and ensure actions, red for failures, and
dark gray for paths and secondary details. `-NoColor` changes only color; the
explicit text and status labels remain unchanged.

## Execution model

Named operations follow the same basic lifecycle:

1. Discover the requested state.
2. Build the complete plan and preflight it before mutation.
3. Explain network, risk, and mutation scope.
4. Request consent only when required by the action's declared risk.
5. Execute with operation-scoped cancellation, process ownership, logging, and temporary storage.
6. Verify the resulting state.
7. Emit a final receipt with `passed`, `failed`, or `cancelled` status and an
   independent mutation state.

Only one setup or project-helper operation may run at a time. A second command
fails immediately with an explicit `operation-in-progress` diagnostic instead
of reading or changing partially updated tools, build trees, or retained state.

`-NonInteractive` changes prompting only. Every mutating non-interactive
command, including `local` build-tree work, still requires `-Yes` unless a
higher-level caller has already granted consent. `-Preview` never performs the
mutation.

Low-level configure, build, test, and ordinary bundle commands remain
incremental unless `-CleanBuild` is supplied. The high-level `coverage`, `asan`, and
`ubsan` actions always recreate their preset trees so stale instrumentation or
runtime artifacts cannot affect the result. The `local-release-check` and
`sanitizer` bundles declare the same policy in the bundle catalog. `sanitizer`
runs AddressSanitizer and then UndefinedBehaviorSanitizer using fresh
`build/asan` and `build/ubsan` trees. `quick` remains incremental. Fresh
recreation is limited to known direct children of the repository `build`
directory and refuses reparse points.

## Quality and tool policy

`gamewip quality check` is the local repository quality gate.
Full quality covers maintained tracked files and non-ignored untracked
first-party files, while preserving the documented generated, historical, and
third-party exclusions. Independent checks aggregate by default; use
`-FailFast` only for focused diagnosis. `gamewip quality fix` follows the
formatter and text-normalization workflow described in @ref project_static_analysis,
then runs the same gate.
`gamewip quality status` reports maintained-file quality ownership.

`gamewip quality hygiene [standard|deep|check-id]` is a separate optional
C/C++ investigation. It configures the `analyze` compilation database, runs
only the selected hygiene rules, and retains normalized evidence as
`artifacts/hygiene-report.json`. It is not part of normal builds, `quality
check`, `analyze`, AddressSanitizer, UndefinedBehaviorSanitizer, or CI. Report
mode succeeds when it finds review candidates. Add `-FailOnFindings` when a caller
intentionally wants
proven findings to fail the operation. `list` describes configured providers,
and `status` performs read-only configuration and tool discovery.

`gamewip tool ensure <id|category|all>` installs or repairs exactly the
versions already declared by the checkout and does not advance pins. `gamewip
tools update <id|all>` is the reviewed pin-advancement workflow and requires a
clean tracked tree. Its preview performs discovery, source-preserving tracked
staging, and staged validation, then reports exact registry fields and declared
live references without applying them. A current selection is a true no-op. See
@ref project_tools for the registry and provider contracts.

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

Use `gamewip history list` for the newest 25 receipts, `gamewip history list all`
for complete history, `gamewip history show latest`, and
`gamewip history clean <selector>` to inspect or clean that owned history.

## Related pages

- @ref project_environment_setup
- @ref project_build
- @ref project_validation
- @ref project_static_analysis
- @ref project_benchmarking
- @ref project_repository_automation
