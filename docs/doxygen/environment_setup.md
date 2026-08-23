@page project_environment_setup Development environment setup

This manual covers the repository-owned Windows 11 bootstrap utility. Use it
from either a Git checkout or an extracted GitHub ZIP of GameWIP. It installs
the selected development environment, initializes pinned dependencies, prepares
editor workflows, builds the profiler tools and manual, and verifies the result.

## Fresh-machine quick start

From the repository root:

```powershell
.\setup.bat
```

1. Press `1` for complete setup.
2. Press `A` to allow automatic installation.
3. In the editor selector, keep Visual Studio Code selected, optionally toggle
   Visual Studio Community, and press `S`.
4. Accept Windows elevation prompts from the requested installers.
5. Open `GameWIP.code-workspace` and reload VS Code once if it was already
   running while setup installed the local workflow extension.

Visual Studio Code is the default and recommended editor. Visual Studio is an
optional additional IDE; it is neither installed nor required unless selected.
The per-checkout choice is stored in ignored `.gamewip-setup.json`. Delete that
file or run the editor action to choose again.

The reproducible compiler environment is MSYS2: UCRT64 GCC drives normal
development, and CLANG64 drives AddressSanitizer validation. CMake remains on
the project-required `4.4.2` or newer 4.4 release line. Pacman installs only packages required by a
documented GameWIP workflow plus their dependencies.

## Interactive menu

Menu and consent choices take effect on one keypress; Enter is not required.

| Key | Action |
| --- | --- |
| `1` | Install missing requirements and prepare the complete checkout. |
| `2` | Check the selected environment without changing it. |
| `3` | Update applicable tools, rebuild project-owned integrations, and verify. |
| `4` | Reapply required state and install anything reported missing. |
| `5` | Change and configure the editor/IDE selection. |
| `6` | Install or repair the declared MSYS2 packages. |
| `7` | Initialize pinned submodules and configure `dev`. |
| `8` | Install common machine tools. |
| `9` | Build the manual, reject warnings, and open it in the default browser. |
| `0` | Rebuild the pinned Tracy tools under `.tracy`. |
| `U` | Remove GameWIP-owned integrations and software installed by setup. |
| `Esc` | Exit. |

An interactive action returns to the complete menu after either success or a
concise failure. The same actions are available directly from a terminal, which
is useful for repeat runs and automation:

```powershell
.\setup.bat check
.\setup.bat update
.\setup.bat repair -SkipDocs
.\setup.bat full -NonInteractive
.\setup.bat full -Branch release/example
```

| Command | Result |
| --- | --- |
| `setup.bat` or `setup.bat menu` | Open the persistent interactive menu. |
| `setup.bat full` | Install or repair every required component, refresh MSYS2, and verify the checkout. |
| `setup.bat check` | Verify required tools and project state without changing the machine. |
| `setup.bat update` | Update compatible tools and rebuild repository-owned integrations. |
| `setup.bat repair` | Reapply the complete required state without requesting ordinary upgrades. |
| `setup.bat uninstall` | Remove setup-owned software, integrations, Tracy, and setup-generated `dev`/documentation build trees while preserving pre-existing software and the checkout. |
| `setup.bat editor` | Choose editors interactively and install their GameWIP integration. |
| `setup.bat tools` | Install the common WinGet-managed command-line tools. |
| `setup.bat visual-studio` | Install or repair Visual Studio Community from `.vsconfig`. |
| `setup.bat msys2` | Install or repair the declared UCRT64 and CLANG64 packages. |
| `setup.bat repository` | Initialize pinned submodules and configure the `dev` build tree. |
| `setup.bat profiler` | Rebuild the matching Tracy tools from the pinned source revision. |
| `setup.bat docs` | Build, verify, and open the generated manual. |
| `setup.bat list` | List every supported setup action from the setup action catalog. |
| `setup.bat help` | Print the command-line usage summary. |

`setup.bat --help`, `setup.bat -h`, and `setup.bat -?` are aliases for the help
action and make no environment changes.

Named actions return a nonzero exit code when they fail. Add `-NonInteractive`
to approve automatic installation and use the saved or default editor choice.
Add `-SkipDocs` to `full`, `update`, or `repair` when documentation is not
needed for that run. Add `-Branch <name>` to select a fetched branch explicitly.
Options may follow the action as shown above.

Without `-NonInteractive`, machine-changing actions offer Automatic, Manual
instructions, or Cancel before making changes.

Complete setup, repair, and update show conservative download, installed-disk,
temporary-build, and peak-memory estimates before requesting consent. Actual
usage depends on the chosen editor and packages already present.

`update` also updates the main repository. It fetches the configured upstream
and permits only a fast-forward, then synchronizes the pinned submodules. Commit
or stash tracked changes first; setup never discards or automatically merges
local work.

Extracted GitHub ZIPs are supported directly. Repository setup initializes Git
metadata in the existing folder, connects the official remote and its default
branch, compares the extracted files with fetched branches, and recommends the
closest match. Interactive setup asks which branch to track; noninteractive
setup uses the detected match unless `-Branch` is supplied. It never replaces
the extracted working files. If those files differ from the selected branch,
they remain visible as local changes; commit or stash them before using
`setup.bat update`.

For an existing interactive checkout, complete setup, repair, update, and the
repository action fetch the branch list and offer the current branch as the
default. Keeping it does not rewrite the working tree. Switching requires a
clean tracked tree. `-NonInteractive` keeps the current branch unless `-Branch`
requests another one.

## Daily project commands

After setup has prepared the machine and checkout, use the root project helper
for ordinary build, validation, benchmark, documentation, and stress workflows:

```powershell
.\gamewip.bat
.\gamewip.bat list
.\gamewip.bat build -Preset test
.\gamewip.bat wizard
.\gamewip.bat stress -Module logger -Count 100 -Parallel 16 -BuildIfMissing
.\gamewip.bat benchmark -BenchmarkProfile standard
.\gamewip.bat workflow -WorkflowAction list
.\gamewip.bat workflow -WorkflowAction run -Workflow release-check -Preview
```

The complete action, option, default, catalog, output, and failure reference is
owned by @ref project_command_line_tools.

`setup.bat` owns environment installation and repair. `gamewip.bat` owns
repository-local project commands, streams native output live, helps assemble
validation executable arguments, reports stress-run progress, offers follow-up
actions after configure and build steps, and stores run logs under
`build/tool-runs/<timestamp>_<action>/`. Setup actions use the same run layout.
Each run has dedicated `logs/` and `artifacts/` directories plus human-readable,
JSON, and manifest summaries. Its interactive selections use one keypress with Enter for
defaults, matching the setup menu style. When a project command fails, the tool
prints the failed action, retained log location, and focused next-step guidance.
The command catalog lives in `scripts/config/gamewip-commands.psd1` so project
commands and bundles can be extended without turning the helper into a generic
shell launcher.

The project helper validates that catalog against visible CMake presets,
discovered correctness modules, guarded workflow files, benchmark profiles,
project-command schemas, and bundle references before executing an action.
Bundles may compose supported build/test operations, project commands,
benchmarks, and other acyclic bundles. Project commands explicitly opt into
additional arguments, keeping extension flexible without becoming an arbitrary
shell launcher.

`gamewip.bat workflow` opens the guarded GitHub workflow menu. Supported
dispatches are declared beside the other project commands, always target
`master`, and are shown in full before execution. `-Preview` prints the
dispatch, watch, and verification commands without contacting GitHub.
Non-mutating checks use an ordinary confirmation. Shared-state writes,
documentation deployment, and release finalization require an exact typed
phrase and then any protected-environment approval configured on GitHub.

The workflow helper requires an authenticated GitHub CLI. Maintainers using a
classic token need `repo` and `workflow` scopes, plus `project` for project
reconciliation. The helper never requests, stores, or compares a project
password. Repository secrets remain workflow credentials; GitHub environment
review is the remote authorization gate.

Mutating workflows also require an environment-scoped protection marker and
fail closed when it is absent. The marker is enabled only after the associated
environment has a required reviewer; it is not a substitute for that reviewer.

### Public-repository workflow protection

GameWIP is a public repository. Manually dispatched project writes, release
writes, and Pages deployments use the `maintainer-write`,
`release-production`, and `github-pages` environments respectively. Each
environment must restrict deployments to `master` and require maintainer review
before its protection marker is enabled.

The environment-scoped marker documented by the owning automation page records
that the matching reviewer rule has been configured; it does not replace that
rule. Keep a marker unset whenever its reviewer or branch restriction is absent.
Read-only previews and dry runs remain available without a write marker.

After changing an environment rule, run the helper preview or dry-run path,
dispatch one guarded operation, and verify that GitHub pauses for approval
before the write job starts.

## What setup owns

Complete setup prepares these project requirements:

- Git and the submodule revisions recorded by the checkout.
- MSYS2 at `C:\MSYS2`, including the declared UCRT64 and CLANG64 packages.
- The selected editors: VS Code integration by default, optional Visual Studio
  Community through `.vsconfig`, or both.
- The configured `dev` tree and warning-free generated manual.
- The six Tracy tools and required UCRT runtime DLLs under `.tracy`.

Every external command is printed before execution, its native output remains
visible, and successful exit codes are reported. Complete actions print their
ordered execution plan, selected editors, source/build/staging destinations,
verification results, and reasons for skipped work. A failing command names the
command and retains its native diagnostic.

Repository preparation never performs an implicit merge or discards tracked
work. `update` may fast-forward the configured upstream, and an explicitly
selected branch may be checked out only when tracked files are clean. Setup,
repair, and update synchronize submodules to the revisions committed by the
selected branch; advancing a submodule pin remains a reviewed source change.

## Update and repair rules

`update` applies the newest compatible WinGet and pacman releases, refreshes
selected editor integration, reuses Tracy executables when their complete set
and recorded pinned source version match, rebuilds them otherwise, rebuilds
documentation, and verifies the environment. CMake remains on
`4.4.2` or newer on the 4.4 release line, and submodules return to their committed revisions.

An existing `C:\MSYS2` root is updated in place with complete `pacman -Syu`
passes. WinGet is used for MSYS2 only when that root is absent, preventing a
duplicate installation at `C:\msys64`. A command-line tool present outside
WinGet ownership is reported and left in place rather than duplicated.

Use `repair` for missing or damaged required state. Use `check` when no changes
are wanted.

## VS Code workflows

Setup installs the local workflow extension and generates a marked block at the
end of the user's `keybindings.json`. User rules normally override extension
defaults; placing the managed rules last gives GameWIP priority. Each rule is
guarded by `config.gamewip.keybindings.enabled`, which the GameWIP workspace
enables, so the shortcuts remain inactive in other repositories.

Before the first managed write, setup preserves the original file as
`keybindings.json.gamewip-backup`. Later runs replace only the marked GameWIP
block. The extension's `package.json` is the source for both the managed rules
and the following reference:

| Shortcut | End-to-end workflow |
| --- | --- |
| `F5` | Configure, build, and run `dev`. |
| `Ctrl+F5` | Configure and build `dev` without launching it. |
| `F6` | Configure, build, run embedded correctness tests, and start `dev` when they pass. |
| `F7` | Configure, build, and run correctness tests. |
| `Alt+F7` | Run the standard optimized benchmark profile and retain its tool-run results. |
| `F8` | Build, verify, and open the generated manual. |
| `Alt+F8` | Configure and run repository C++ analysis. |
| `F9` | Build the profile game, start Tracy, and run the game. |
| `Alt+F9` | Start Tracy and run profiled startup tests. |
| `F10` | Build and run tests, generate coverage, and open the report. |
| `F11` | Build and run CLANG64 AddressSanitizer tests. |
| `F12` | Configure, build, and run the release game. |

In the GameWIP workspace, `F5` and `Ctrl+F5` intentionally run repository tasks
instead of VS Code's default launch commands. Use the **Run and Debug** view to
start the configured GDB session when interactive debugging is needed.

Open `GameWIP.code-workspace`, rather than only the repository folder, to enable
the shortcuts. They invoke the matching `GameWIP: ...` tasks from
`.vscode/tasks.json`; every visible workflow can also be started without a
shortcut from **Terminal > Run Task** or the **Tasks: Run Task** command in the
Command Palette. The task terminal shows each configure, build, test, or run
failure and stops the remaining steps when a prerequisite fails.

Game run tasks use a dedicated terminal for executable output. Their CMake
configure and build prerequisites remain in the shared task terminal, keeping
build diagnostics separate from the running game's output.

To temporarily turn off every GameWIP shortcut in the current workspace, set
`gamewip.keybindings.enabled` to `false`. To change an individual shortcut,
edit the GameWIP-managed rule in the VS Code Keyboard Shortcuts JSON. Rerunning
`setup.bat editor`, `setup.bat update`, or `setup.bat repair` regenerates that
managed block from the extension and therefore replaces local edits inside it;
put durable personal bindings after the managed block or invoke the task from
the Command Palette. The one-time `.gamewip-backup` remains available beside
`keybindings.json` if the pre-setup bindings need to be consulted or restored
manually.

## Tracy and documentation outputs

Tracy builds use the pinned source under `external/tracy`, UCRT64 GCC/Ninja,
and clean generated trees under `build/setup/tracy/ucrt64`. All six executables
and discovered UCRT DLL dependencies are collected under
`build/setup/tracy/stage`. Only a complete verified set replaces `.tracy`, so a
failed rebuild leaves the previous tools intact. See @ref project_profiling for
the capture workflow and executable list.

Focused `docs` or menu `9` opens
`build/docs/docs/doxygen/html/index.html` after a warning-free build. Complete
setup, repair, and update build the same manual without opening a browser.

## Extending setup

Declarative requirements live under `scripts/setup/config/`:

- `actions.psd1` owns action names, menu keys, descriptions, and machine-change classification.
- `tools.psd1` owns ordinary machine tools.
- `msys2-packages.psd1` owns project-required pacman packages.
- `editors.psd1` owns selectable editors, display keys, packages, and handlers.

Add reusable operations under `scripts/setup/lib/`, declare their user-facing
action metadata in `actions.psd1`, and register a focused stage in
`scripts/setup/windows.ps1`. A new editor normally requires one config
entry and one handler. Keep stages rerunnable, explicit about external changes,
and verification-driven.

## Troubleshooting

| Symptom | Action |
| --- | --- |
| Check reports missing state. | Run repair (`4`) and choose Automatic. Check itself is read-only. |
| A shortcut still runs a personal command. | Rerun editor setup (`5`), then reload the VS Code window. |
| No GameWIP shortcuts are active. | Open `GameWIP.code-workspace`, confirm `gamewip.keybindings.enabled` is `true`, and reload VS Code after editor setup. |
| A function key is handled by the laptop or operating system. | Use the keyboard's `Fn` modifier, or run the same `GameWIP: ...` task from **Terminal > Run Task**. |
| MSYS2 tries to use `C:\msys64`. | Stop and use the repository setup, which owns `C:\MSYS2`. Do not delete either root without reviewing its contents. |
| Tracy rebuild is quiet or slow on first use. | Allow the large profiler dependency build to finish; command output identifies active work and failures. |
| Direct automation fails. | Use the printed failing command and native diagnostic; named actions intentionally preserve a nonzero exit code. |

## Related pages

- @ref project_getting_started
- @ref project_build
- @ref project_profiling
- @ref project_documentation
