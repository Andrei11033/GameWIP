@page project_environment_setup Development environment setup

This manual covers the repository-owned Windows 11 bootstrap utility. Use it
after obtaining a Git checkout of GameWIP; it installs the selected development
environment, initializes pinned dependencies, prepares editor workflows, builds
the profiler tools and manual, and verifies the result.

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
the project-required `4.4.x` line. Pacman installs only packages required by a
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
| `Esc` | Exit. |

An interactive action returns to the complete menu after either success or a
concise failure. The same actions are available directly from a terminal, which
is useful for repeat runs and automation:

```powershell
.\setup.bat check
.\setup.bat update
.\setup.bat repair -SkipDocs
.\setup.bat full -NonInteractive
```

| Command | Result |
| --- | --- |
| `setup.bat` or `setup.bat menu` | Open the persistent interactive menu. |
| `setup.bat full` | Install or repair every required component and verify the checkout. |
| `setup.bat check` | Verify required tools and project state without changing the machine. |
| `setup.bat update` | Update compatible tools and rebuild repository-owned integrations. |
| `setup.bat repair` | Reapply the complete required state without requesting ordinary upgrades. |
| `setup.bat editor` | Choose editors interactively and install their GameWIP integration. |
| `setup.bat tools` | Install the common WinGet-managed command-line tools. |
| `setup.bat visual-studio` | Install or repair Visual Studio Community from `.vsconfig`. |
| `setup.bat msys2` | Install or repair the declared UCRT64 and CLANG64 packages. |
| `setup.bat repository` | Initialize pinned submodules and configure the `dev` build tree. |
| `setup.bat profiler` | Rebuild the matching Tracy tools from the pinned source revision. |
| `setup.bat docs` | Build, verify, and open the generated manual. |
| `setup.bat help` | Print the command-line usage summary. |

Named actions return a nonzero exit code when they fail. Add `-NonInteractive`
to approve automatic installation and use the saved or default editor choice.
Add `-SkipDocs` to `full`, `update`, or `repair` when documentation is not
needed for that run. Options may follow the action as shown above.

Without `-NonInteractive`, machine-changing actions offer Automatic, Manual
instructions, or Cancel before making changes.

## Daily project commands

After setup has prepared the machine and checkout, use the root project helper
for ordinary build, validation, benchmark, documentation, and stress workflows:

```powershell
.\gamewip.bat
.\gamewip.bat list
.\gamewip.bat build -Preset test
.\gamewip.bat wizard
.\gamewip.bat stress -Module logger -Count 100 -Parallel 16 -BuildIfMissing
```

`setup.bat` owns environment installation and repair. `gamewip.bat` owns
repository-local project commands, streams native output live, helps assemble
validation executable arguments, reports stress-run progress, offers follow-up
actions after configure and build steps, and stores run logs under
`build/tool-runs/`. Its interactive selections use one keypress with Enter for
defaults, matching the setup menu style. When a project command fails, the tool
prints the failed action, retained log location, and focused next-step guidance.
The command catalog lives in `scripts/config/gamewip-commands.psd1` so project
commands and bundles can be extended without turning the helper into a generic
shell launcher.

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

Setup does not pull or switch the current branch. Ordinary setup/update also
does not advance submodule pins; changing a pin is a reviewed source update.

## Update and repair rules

`update` applies the newest compatible WinGet and pacman releases, refreshes
selected editor integration, reuses Tracy executables when their complete set
and recorded pinned source version match, rebuilds them otherwise, rebuilds
documentation, and verifies the environment. CMake remains on
`4.4.x`, and submodules return to their committed revisions.

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
| `Alt+F5` | Configure, build, and run `dev-no-tools`. |
| `F6` | Configure, build, run embedded correctness tests, and start `dev` when they pass. |
| `F7` | Configure, build, and run correctness tests. |
| `Alt+F7` | Configure, build, and run benchmarks. |
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

- `tools.psd1` owns ordinary machine tools.
- `msys2-packages.psd1` owns project-required pacman packages.
- `editors.psd1` owns selectable editors, display keys, packages, and handlers.

Add reusable operations under `scripts/setup/lib/` and register a focused
stage in `scripts/setup/windows.ps1`. A new editor normally requires one config
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
