@page project_environment_setup Development environment setup

GameWIP's Windows 11 environment bootstrap is `setup.bat`. It is the supported owner for machine preparation, repair,
environment/package-manager updates, editor integration, profiler-tool preparation, ownership-aware uninstall, and complete
environment verification.

## Quick start

```powershell
.\setup.bat
```

The default action opens the persistent menu. The same actions can be run directly:

```powershell
.\setup.bat check
.\setup.bat repair
.\setup.bat full
```

## Setup actions

| Command | Result |
| --- | --- |
| `setup.bat` or `setup.bat menu` | Open the interactive setup menu. |
| `setup.bat full` | Install or repair the complete declared environment, prepare the checkout, and verify it. |
| `setup.bat check` | Verify the selected environment without mutation. |
| `setup.bat update` | Update compatible environment software and the checkout without advancing exact project pins, then verify. |
| `setup.bat repair` | Reapply declared required state without requesting ordinary upgrades. |
| `setup.bat editor` | Choose editors/IDEs and apply their GameWIP integration. |
| `setup.bat msys2` | Install or repair declared UCRT64 and CLANG64 packages. |
| `setup.bat repository` | Prepare Git/submodules and repository-local development state. |
| `setup.bat tools` | Ensure project tools at versions already declared by the checkout. |
| `setup.bat docs` | Build and verify generated documentation. |
| `setup.bat profiler` | Build/install Tracy tools matching the pinned client. |
| `setup.bat uninstall` | Inventory and remove only resources with sufficient GameWIP ownership evidence. |
| `setup.bat visual-studio` | Install or repair Visual Studio Community using the repository configuration. |
| `setup.bat list` | Print the setup action catalog. |
| `setup.bat help` | Print setup usage. |

`setup.bat --help`, `setup.bat -h`, and `setup.bat -?` are help aliases.

## Controls

```powershell
.\setup.bat repair -Preview
.\setup.bat repair -NonInteractive -Yes
.\setup.bat full -Branch feature/example
.\setup.bat repair -SkipDocs
```

- `-Preview` prints the planned scope and performs only the action's read-only
  discovery or preflight. It never mutates tracked or machine state.
- `-NonInteractive` suppresses prompts; it does not approve mutation.
- `-Yes` grants the printed setup mutation plan for non-interactive use.
- `-Branch <name>` selects an explicit fetched branch where the action supports repository preparation.
- `-SkipDocs` skips documentation during complete/update/repair runs.
- `-Json`, `-Quiet`, `-NoColor`, and `-OutputMode Summary|Stream|LogOnly` use the same operation/result presentation model as
  the project helper. `Stream` is the default, so successful installer and build output remains visible.

Machine-changing interactive actions present the complete plan before consent. Non-interactive mutation fails closed when
`-Yes` is absent. Read-only actions do not ask for consent.

Setup installs the GitHub CLI used by guarded workflow commands. Authentication remains user-owned; run `gh auth login` before
querying or dispatching workflows.

## Lifecycle and failure model

Setup uses the same operation model as `gamewip.bat`:

1. Discover current machine and checkout state.
2. Build and preflight the whole plan.
3. Explain changes, preserved state, network use, and risk.
4. Obtain consent when required.
5. Execute with owned native processes, cancellation, logs, and operation temp.
6. Verify resulting state.
7. Emit a receipt describing status, changes, preserved resources, and next actions.

A failure after some machine mutation is reported as a failed operation with a truthful partial mutation state. Setup does not
claim a generic rollback; rerunnable `setup.bat repair` restores declared desired state.

## Environment ownership

Persistent directly managed tools live under `C:\MSYS2\GameWIPTools`. Existing non-empty roots without valid GameWIP ownership
proof are preserved. Interactive setup may explicitly adopt an existing root; non-interactive setup refuses unknown ownership.

Uninstall removes only resources with sufficient GameWIP ownership evidence and preserves pre-existing software, the checkout,
unrelated build trees, and ownership-unknown content.

## Project tools versus environment updates

`setup.bat update` owns environment and package-manager updates while preserving exact project pins. Use:

```powershell
.\gamewip.bat tools ensure all
```

to repair/install the versions already declared by the checkout, and:

```powershell
.\gamewip.bat tools update all -Preview
```

to review an intentional project pin advancement.

## Daily project commands

After setup succeeds, use `gamewip.bat` for repository-local work:

```powershell
.\gamewip.bat build dev
.\gamewip.bat test test
.\gamewip.bat module unicode
.\gamewip.bat quality check
.\gamewip.bat tools status
.\gamewip.bat benchmark run
.\gamewip.bat workflow list
```

See @ref project_command_line_tools for the complete project-helper grammar.

## Related pages

- @ref project_command_line_tools
- @ref project_tools
- @ref project_build
- @ref project_repository_maintenance
