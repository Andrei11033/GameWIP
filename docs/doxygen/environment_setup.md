@page project_environment_setup Development environment setup

GameWIP's Windows 11 environment bootstrap is `setup.bat`. It owns machine
preparation, repair, environment and package-manager updates, pinned dependency
cache preparation, editor integration, profiler-tool preparation,
ownership-aware uninstall, and complete environment verification.

## Quick start

```powershell
.\setup.bat
```

The default action opens the persistent setup menu. The same actions can be run
directly:

```powershell
.\setup.bat check
.\setup.bat repair
.\setup.bat full
.\setup.bat deps
```

Both interactive menus render declared key and label entries through the same
console primitive. The project menu hierarchy lives in
`scripts/config/commands.json`, and setup menu entries live in
`scripts/setup/config/setup.json`. Their schemas and runtime checks reject
duplicate keys, unknown handlers, and incomplete menu catalogs before use.

## Setup actions

| Command | Result |
| --- | --- |
| `setup.bat` or `setup.bat menu` | Open the interactive setup menu. |
| `setup.bat full` | Install or repair the complete declared environment, prepare the checkout, and verify it. |
| `setup.bat check` | Verify the selected environment without mutation. |
| `setup.bat update` | Update compatible environment software and the checkout without advancing exact project pins, then verify. |
| `setup.bat repair` | Reapply declared required state without requesting ordinary upgrades. |
| `setup.bat deps` | Fetch or reuse dependency sources at the commits recorded by the checkout. |
| `setup.bat editor` | Choose editors/IDEs and apply their GameWIP integration. |
| `setup.bat msys2` | Install or repair declared UCRT64 and CLANG64 packages. |
| `setup.bat repo` | Prepare the Git checkout and repository-local development state. |
| `setup.bat tool` | Install or repair project tools at versions already declared by the checkout. |
| `setup.bat doc` | Build and verify generated documentation. |
| `setup.bat profiler` | Reuse the locked Tracy source and build/install matching profiler tools. |
| `setup.bat uninstall` | Inventory and remove only resources with sufficient GameWIP ownership evidence. |
| `setup.bat vs` | Install or repair Visual Studio Community using the repository configuration. |
| `setup.bat list` | Print the setup action catalog. |
| `setup.bat help` | Print setup usage. |

`setup.bat --help`, `setup.bat -h`, and `setup.bat -?` are help aliases.
The canonical short setup names are `repo`, `tool`, `deps`, `doc`, and `vs`;
the compatibility aliases `repository`, `tools`, `dependencies`, `docs`, and
`visual-studio` remain accepted. `ls` aliases `list`, and `h` aliases `help`.

## Dependency cache and offline builds

The setup utility and project helper share the repository-local dependency cache
under `build/gamewip/cache/dependencies`. The cache is controlled by
`scripts/config/dependencies.json`, which records each repository and exact
commit. Preparation reuses a matching source tree and only fetches a missing or
out-of-date commit.

Use the project helper for focused cache operations:

```powershell
.\gamewip.bat deps check
.\gamewip.bat deps prepare
```

After the cache is ready, configure and build without allowing dependency
downloads:

```powershell
.\gamewip.bat config profile -Offline
.\gamewip.bat build profile -Offline
```

Offline mode requires the prepared sources and verifies their exact Git commits;
it fails with a repair instruction instead of downloading anything. The setup
`check` action verifies the cache but does not prepare or modify it.

## Controls

```powershell
.\setup.bat repair -Preview
.\setup.bat repair -NonInteractive -Yes
.\setup.bat full -Branch feature/example
.\setup.bat repair -SkipDocs
```

- `-Preview` prints the planned scope and performs only the action's read-only
  discovery or preflight. It does not apply local, tracked, or machine changes;
  diagnostic run logs and receipts are still retained. A focused `doc` preview
  therefore does not configure, build, or open the manual.
- `-NonInteractive` never prompts. This does not grant consent for any mutation risk.
- `-Yes` grants consent after the operation plan is known.
- `-Branch <name>` selects an explicit fetched branch where the action supports
  repository preparation.
- `-SkipDocs` skips documentation during complete/update/repair runs.
- `-Json`, `-Quiet`, `-NoColor`, and `-OutputMode Summary|Stream|LogOnly` use the
  same operation and result presentation model as the project helper. `Stream`
  is the default, so successful installer and build output remains visible.
- `-Verbose` uses the PowerShell common parameter for detailed progress.

Semantic presentation uses cyan for accents and progress, green for success and
ready states, yellow for warnings and ensure actions, red for failures, and
dark gray for paths and secondary details. `-NoColor` changes only color; the
explicit text and status labels remain unchanged.

Machine-changing interactive actions present the complete plan before consent.
Non-interactive mutation fails closed when `-Yes` is absent. Read-only actions
do not ask for consent.

Setup installs the GitHub CLI used by guarded workflow commands. Authentication
remains user-owned. Run `gh auth login` before querying or dispatching workflows.

## Lifecycle and failure model

Setup uses the same operation model as `gamewip.bat`:

1. Discover current machine and checkout state.
2. Build and preflight the whole plan.
3. Explain changes, preserved state, network use, and risk.
4. Obtain consent when required.
5. Execute with owned native processes, cancellation, logs, and operation temp.
6. Verify resulting state.
7. Emit a receipt describing status, changes, preserved resources, and next actions.

A failure after mutation begins is reported as a failed operation with a
truthful partial-mutation state. A failure before the first mutation remains
`none`. Setup does not claim a generic rollback. Rerunning `setup.bat repair`
restores the declared desired state.

## Environment ownership

Persistent directly managed tools live under `C:\MSYS2\GameWIPTools`. Existing
non-empty roots without valid GameWIP ownership proof are preserved. Interactive
setup may explicitly adopt an existing root. Non-interactive setup refuses
unknown ownership.

Uninstall removes only resources with sufficient GameWIP ownership evidence. It
preserves pre-existing software, the checkout, unrelated build trees, and
content with unknown ownership.

## Project tools and environment updates

`setup.bat update` owns environment and package-manager updates while preserving
exact project pins. Use:

```powershell
.\gamewip.bat tool ensure all
```

to repair or install the versions already declared by the checkout. Use:

```powershell
.\gamewip.bat tool update all -Preview
```

to review an intentional project-pin advancement. See @ref
project_command_line_tools for the complete project-helper grammar and daily
commands.

## Related pages

- @ref project_command_line_tools
- @ref project_tools
- @ref project_build
- @ref project_repository_maintenance
