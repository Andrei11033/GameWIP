# Setup implementation

The root `setup.bat` is the supported Windows 11 entry point. This directory
owns its PowerShell orchestration, declarative requirements, editor assets, and
maintainer documentation. Contributor usage belongs in the generated
[environment setup manual](../../docs/doxygen/environment_setup.md).

## Layout

- `windows.bat` forwards batch arguments without owning setup behavior.
- `windows.ps1` owns actions, consent, the persistent menu, execution plans,
  and final verification.
- `../common/ToolRuns.ps1` owns the run-directory, step, output, summary, and
  manifest format shared with the project helper.
- `config/actions.psd1` owns action names, menu keys, descriptions, and
  machine-change classification used by menus, listing, help, and consent.
- `config/tools.psd1` lists ordinary WinGet-managed tools.
- `config/msys2-packages.psd1` lists only packages required by documented
  project workflows; pacman owns their dependencies.
- `config/editors.psd1` declares selectable editors and their handlers. VS Code
  is the default; Visual Studio is optional.
- `lib/` contains focused reusable operations.
- `editor/gamewip-workflows/` is the declarative VS Code workflow extension.

Per-checkout editor selection is stored in ignored `.gamewip-setup.json`.
Noninteractive first use selects the configured default.

## Operational contracts

Machine-changing interactive actions require Automatic, Manual, or Cancel
consent. Named actions preserve nonzero exit codes. The menu catches an action
failure, prints its concise cause, and returns to the full action list.

`Invoke-SetupNative` prints each external command, streams its native output,
and reports successful exit codes. Named setup actions retain the same
action-scoped run structure as the project helper under
`build/tool-runs/<timestamp>_setup-<action>/`, including step logs, summaries,
and a manifest. Stages print source and destination paths,
selected options, reasons for skips, and verification results. Do not hide
installer, pacman, Git, compiler, or documentation diagnostics in new code.

Full setup installs missing state and refreshes MSYS2 before installing its declared packages.
Repair reapplies missing state without requesting ordinary upgrades. Update fetches and fast-forwards
the current branch from its configured upstream and applies newest compatible
WinGet/pacman releases while retaining CMake `4.4.2` or newer on the 4.4 release line and the submodule revisions
recorded by the updated checkout. It refuses dirty trees, missing upstreams, and
non-fast-forward merges. Existing `C:\MSYS2` installations update through
complete `pacman -Syu` passes; WinGet is used only for the first installation at
that explicit root.

The editor stage installs only selected editors. VS Code integration installs
Microsoft C++/CMake extensions and packages the local workflow extension as a
VSIX so the VS Code CLI installs and reports it like any other extension. It
also writes
a marked, repository-guarded block at the end of the user's
`keybindings.json`, after preserving a one-time `.gamewip-backup`; later runs
replace only that block. This is required because VS Code user rules outrank
extension defaults.

Tracy checks the complete installed executable set and its recorded pinned
source version before doing build work. When they match, setup reuses the
installed tools; otherwise it rebuilds five upstream CMake projects from the
pinned submodule using UCRT64 GCC/Ninja. Generated compatibility adjustments
provide the POSIX `memmem` operation missing from UCRT64 and remove incompatible
COFF LTO flags without modifying the submodule. The required Windows security
library is linked explicitly. All six EXEs and recursively discovered UCRT DLLs stage under
`build/setup/tracy`; `.tracy` changes only after complete verification.

Focused `docs` builds, verifies, and opens the generated manual. Complete
setup/update/repair builds the same manual without launching a browser.

An extracted GitHub ZIP is supported. After Git is available, repository setup
initializes metadata in place, connects the official remote, compares fetched
branches with the extracted files, and asks which branch to track. Existing
interactive checkouts also offer a branch choice; `-Branch` supplies it for
automation. Branch switches refuse tracked local changes. Extracted files remain
untouched while pinned submodules become available.

Uninstall removes repository-owned integrations and artifacts plus only the
WinGet applications recorded as newly installed by setup. It preserves software
that existed beforehand, the checkout, user files, and an MSYS2 tree that may
contain later user data.

## Extension rules

Prefer data over orchestration branches:

- Add an ordinary tool to `tools.psd1`.
- Add a justified package to the owning MSYS2 group.
- Add an editor entry with a unique key and handler name; implement a handler
  only when existing behavior cannot support it.
- Add new stages as one library operation, one `actions.psd1` entry, and an
  explicit registration in `windows.ps1`.

Every stage must be rerunnable, scoped to the checkout or selected machine
requirements, explicit about changes, and followed by verification.
