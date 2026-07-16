# Setup implementation

The root `setup.bat` is the supported Windows 11 entry point. This directory
owns its PowerShell orchestration, declarative requirements, editor assets, and
maintainer documentation. Contributor usage belongs in the generated
[environment setup manual](../../docs/doxygen/environment_setup.md).

## Layout

- `windows.bat` forwards batch arguments without owning setup behavior.
- `windows.ps1` owns actions, consent, the persistent menu, execution plans,
  and final verification.
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
and reports successful exit codes. Stages print source and destination paths,
selected options, reasons for skips, and verification results. Do not hide
installer, pacman, Git, compiler, or documentation diagnostics in new code.

Setup and repair install missing state. Update applies newest compatible
WinGet/pacman releases but retains CMake `4.4.x` and the submodule revisions
recorded by the checkout. Existing `C:\MSYS2` installations update through
complete `pacman -Syu` passes; WinGet is used only for the first installation at
that explicit root.

The editor stage installs only selected editors. VS Code integration installs
Microsoft C++/CMake extensions and the local workflow extension. It also writes
a marked, repository-guarded block at the end of the user's
`keybindings.json`, after preserving a one-time `.gamewip-backup`; later runs
replace only that block. This is required because VS Code user rules outrank
extension defaults.

Tracy rebuilds five upstream CMake projects from the pinned submodule using
UCRT64 GCC/Ninja. Generated compatibility adjustments remove upstream
MSVC-only `/MP` and incompatible COFF LTO flags without modifying the
submodule. All six EXEs and recursively discovered UCRT DLLs stage under
`build/setup/tracy`; `.tracy` changes only after complete verification.

Focused `docs` builds, verifies, and opens the generated manual. Complete
setup/update/repair builds the same manual without launching a browser.

## Extension rules

Prefer data over orchestration branches:

- Add an ordinary tool to `tools.psd1`.
- Add a justified package to the owning MSYS2 group.
- Add an editor entry with a unique key and handler name; implement a handler
  only when existing behavior cannot support it.
- Add new stages as one library operation plus explicit registration in
  `windows.ps1`.

Every stage must be rerunnable, scoped to the checkout or selected machine
requirements, explicit about changes, and followed by verification.
