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
concise failure. Named actions return a nonzero exit code for automation:

```powershell
.\setup.bat check
.\setup.bat update
.\setup.bat repair -SkipDocs
.\setup.bat full -NonInteractive
```

`-NonInteractive` selects automatic installation and the default editor when
no preference exists. Machine-changing interactive actions instead offer
Automatic, Manual instructions, or Cancel.

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
selected editor integration, rebuilds the Tracy executables from the pinned
client, rebuilds documentation, and verifies the environment. CMake remains on
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
| `F6` | Configure, build, and run `dev`. |
| `Ctrl+F6` | Configure and build `dev` without launching it. |
| `Alt+F6` | Configure, build, and run `dev-no-tools`. |
| `F7` | Configure, build, and run correctness tests. |
| `Alt+F7` | Configure, build, and run benchmarks. |
| `F8` | Build, verify, and open the generated manual. |
| `Alt+F8` | Configure and run repository C++ analysis. |
| `F9` | Build the profile game, start Tracy, and run the game. |
| `Alt+F9` | Start Tracy and run profiled startup tests. |
| `F10` | Build and run tests, generate coverage, and open the report. |
| `F11` | Build and run CLANG64 AddressSanitizer tests. |
| `F12` | Configure, build, and run the release game. |

`F5` and `Ctrl+F5` retain their normal VS Code behavior. The selected launch
configuration builds `dev` before starting GDB.

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
| MSYS2 tries to use `C:\msys64`. | Stop and use the repository setup, which owns `C:\MSYS2`. Do not delete either root without reviewing its contents. |
| Tracy rebuild is quiet or slow on first use. | Allow the large profiler dependency build to finish; command output identifies active work and failures. |
| Direct automation fails. | Use the printed failing command and native diagnostic; named actions intentionally preserve a nonzero exit code. |

## Related pages

- @ref project_getting_started
- @ref project_build
- @ref project_profiling
- @ref project_documentation
