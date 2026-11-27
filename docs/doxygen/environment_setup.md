@page project_environment_setup Development environment setup

GameWIP setup supports Windows and Linux. Windows uses `setup.bat`; Linux uses
`setup.sh` on Ubuntu, Debian, and CachyOS. Both require PowerShell 7.7 or newer
and dispatch to the same helper. If no compatible version is found, setup asks
before installing the verified 7.7.0 runtime under
`<managed-tools-root>/tools/powershell`. Preview does not install it.

Linux setup prepares host packages, pinned CMake, LLVM-MinGW for Windows
cross-builds, and Zig for Linux targets. An exact compatible system Zig is
reused; otherwise setup installs the pinned archive.

## Storage and tool locations

Helper data defaults to `build/gamewip/`: caches, state, temporary files, and
run receipts. PowerShell and managed tools default under
`build/gamewip/tools/managed/`. Preset build trees and the default install
prefix also remain under `build/`.

Set these environment variables before running setup or the project helper.
Pass the same names as CMake cache variables when configuring directly:

| Variable | Default | Rules |
| --- | --- | --- |
| `GAMEWIP_STORAGE_ROOT` | `build/gamewip` | Absolute or checkout-relative path beneath the checkout's `build/` directory. |
| `GAMEWIP_MSYS2_ROOT` | `<storage-root>/tools/msys2` | New installs stay under `build/`; an external value must name an existing installation. |
| `GAMEWIP_MANAGED_TOOLS_ROOT` | `<storage-root>/tools/managed` | New installs stay under `build/`; an external value must name an existing directory. |

For example, to reuse an existing Windows MSYS2 installation and choose a
separate managed-tools directory:

```powershell
$env:GAMEWIP_MSYS2_ROOT = 'C:\MSYS2'
$env:GAMEWIP_MANAGED_TOOLS_ROOT = 'C:\MSYS2\GameWIPTools'
```

Set `GAMEWIP_STORAGE_ROOT` to another directory inside the checkout's `build/`
tree to relocate helper storage. An explicitly selected existing installation
may be outside it; new managed roots may not. On Linux, set the same variables
with `export`; for example, `GAMEWIP_MANAGED_TOOLS_ROOT="$HOME/GameWIPTools"`
selects an existing tools directory. Use absolute managed-root paths for direct
shell commands. VS Code must inherit the same variables to use the selected
compiler and debugger paths. The roots must stay separate; `GameWIPTools` may be
a direct child of the MSYS2 root. Setup asks before using a discovered external
MSYS2 root or adopting an unowned managed-tools directory.

APT, pacman, and WinGet keep host packages outside the checkout. Setup lists
Linux package changes before confirmation and records packages that were
missing before installation for optional removal. CachyOS uses `pacman -Syu`,
which performs a full system upgrade. VS Code extensions and keybindings use
the editor's user directories. Helper caches, state, run data, and new managed
installs remain under `build/`. Setup does not install or update VS Code; if it
is missing, its integration is skipped. Windows setup asks before installing
Visual Studio when selected.

## Quick start

```powershell
.\setup.bat
```

On Linux, run:

```bash
bash ./setup.sh
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

| Action | Windows | Linux |
| --- | --- | --- |
| `menu` | `setup.bat menu` | `bash ./setup.sh menu` |
| `full` | `setup.bat full` | `bash ./setup.sh full` |
| `check` | `setup.bat check` | `bash ./setup.sh check` |
| `update` | `setup.bat update` | `bash ./setup.sh update` |
| `repair` | `setup.bat repair` | `bash ./setup.sh repair` |
| `deps` | `setup.bat deps` | `bash ./setup.sh deps` |
| `editor` | `setup.bat editor` | `bash ./setup.sh editor` |
| `msys2` | `setup.bat msys2` | — |
| `system` | — | `bash ./setup.sh system` |
| `repo` | `setup.bat repo` | `bash ./setup.sh repo` |
| `tool` | `setup.bat tool` | `bash ./setup.sh tool` |
| `doc` | `setup.bat doc` | `bash ./setup.sh doc` |
| `profiler` | `setup.bat profiler` | — |
| `uninstall` | `setup.bat uninstall` | `bash ./setup.sh uninstall` |
| `vs` | `setup.bat vs` | — |
| `list` | `setup.bat list` | `bash ./setup.sh list` |
| `help` | `setup.bat help` | `bash ./setup.sh help` |

On Linux, `system`, `full`, `repair`, and `update` install host packages through
apt or pacman. On CachyOS,
pacman performs a full system upgrade as part of each of those actions.

`setup.bat --help`, `setup.bat -h`, and `setup.bat -?` are Windows help
aliases.

Linux uses the same help aliases with `bash ./setup.sh`. Setup can integrate an
existing VS Code installation but does not install VS Code.

The project tool registry pins LLVM-MinGW for Windows x64 and ARM64 builds and
Zig for Linux x64 and ARM64 targets. Windows setup stores MSYS2 and managed
tools under the configured roots; Linux stores verified CMake, LLVM-MinGW, and
Zig tools under the managed-tools root.

Select a Windows build target on Linux, then configure and build it:

```bash
bash ./gamewip.sh target -TargetPlatform windows -TargetArchitecture x64
bash ./gamewip.sh config dev
bash ./gamewip.sh build dev
```

Choose `arm64` to produce Windows ARM64 binaries.

To select a Linux target, use `linux` with either `x64` or `arm64`. The helper
uses Zig on Windows and Linux; full project configuration waits for Linux
platform backends.

## Dependency cache and offline builds

The setup utility and project helper share the dependency cache under
`<storage-root>/cache/dependencies` (default `build/gamewip/cache/dependencies`).
Direct CMake users can set `GAMEWIP_DEPENDENCY_CACHE_DIR` to another path. The
cache is controlled by
`scripts/config/dependencies.json`, which records each repository and exact
commit. Preparation reuses a matching source tree and only fetches a missing or
out-of-date commit.
Concurrent preparation requests serialize on an exclusive cache lock; a
process interruption releases the lock through the operating system without
requiring manual deletion of the lock file.

On Linux, prefix these commands with `bash ./gamewip.sh`.

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

Offline mode is represented by `GAMEWIP_DEPENDENCIES_OFFLINE=ON` in direct CMake
invocations. It requires the prepared sources and verifies their exact Git
commits; it fails with a repair instruction instead of downloading anything.
The setup `check` action verifies the cache but does not prepare or modify it.

## Controls

```powershell
.\setup.bat repair -Preview
.\setup.bat repair -NonInteractive -Yes
.\setup.bat full -Branch feature/example
.\setup.bat repair -SkipDocs
```

On Linux, run the same actions with `bash ./setup.sh`:

```bash
bash ./setup.sh repair -Preview
bash ./setup.sh repair -NonInteractive -Yes
bash ./setup.sh full -Branch feature/example
bash ./setup.sh repair -SkipDocs
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

Linux package installation uses `sudo`; `-NonInteractive` requires root or
cached sudo credentials.

Setup installs the GitHub CLI used by guarded workflow commands. Authentication
remains user-owned. Run `gh auth login` before querying or dispatching workflows.

## Lifecycle and failure model

Setup uses the same operation model as the project helper:

1. Discover current machine and checkout state.
2. Build and preflight the whole plan.
3. Explain changes, preserved state, network use, and risk.
4. Obtain consent when required.
5. Execute with owned native processes, cancellation, logs, and operation temp.
6. Verify resulting state.
7. Emit a receipt describing status, changes, preserved resources, and next actions.

A failure after mutation begins is reported as a failed operation with a
truthful partial-mutation state. A failure before the first mutation remains
`none`. Setup does not claim a generic rollback. Rerun `setup.bat repair` on
Windows or `bash ./setup.sh repair` on Linux to restore declared state.

## Environment ownership

With the default storage root, Windows-managed tools use
`build/gamewip/tools/msys2` and `build/gamewip/tools/managed`; Linux-managed
tools use `build/gamewip/tools/managed`. The root variables above can select
existing installations. On Windows, setup preserves non-empty roots without
valid GameWIP ownership proof and asks before adopting them. Without an external
override, managed tool roots remain inside the checkout build tree.

Uninstall removes the checkout's entire `build/` directory after setup exits,
plus recorded setup-owned extensions and APT or pacman packages. It preserves
checkout sources and configuration, unrecorded external resources, and existing
installations outside `build/`. Run uninstall through `setup.bat` or `setup.sh`
so the launcher can remove the active helper runtime and its receipts safely.

## Project tools and environment updates

Setup update commands own environment and package-manager updates while
preserving exact project pins. Use:

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
