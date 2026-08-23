# Setup implementation

This directory implements the Windows setup experience behind the root
`setup.bat` entry point. Contributors looking to install or repair their
environment should use the generated
[environment setup manual](../../docs/doxygen/environment_setup.md). The
details below are for maintainers changing what setup installs or how it
verifies the machine.

## Layout

- `windows.bat` forwards batch arguments without owning setup behavior.
- `windows.ps1` owns actions, consent, the persistent menu, execution plans,
  and final verification.
- `../lib/ToolRuns.ps1` owns the run-directory, step, output, summary, and
  manifest format shared with the project helper.
- `config/setup.json` owns action metadata and machine-package requirements.
- `config/editors.json` declares selectable editors and their handlers. VS Code
  is the default; Visual Studio is optional.
- `../config/project-tools.json` is the single project-tool and version-policy
  authority consumed by setup and the project helper.
- `lib/` contains focused reusable operations.
- `editor/gamewip-workflows/` is the declarative VS Code workflow extension.

Per-checkout editor selection is stored as disposable advisory state in
`build/gamewip/state/editor-selection.json`.
Noninteractive first use selects the configured default.

## Behavior that setup must preserve

Machine-changing interactive actions require Automatic, Manual, or Cancel
consent. Named actions preserve nonzero exit codes. The menu catches an action
failure, prints its concise cause, and returns to the full action list.

`Invoke-GameWipSetupNative` prints each external command, streams its native output,
and reports successful exit codes. Named setup actions retain the same
action-scoped run structure as the project helper under
`build/gamewip/runs/<timestamp>_setup-<action>/`, including step logs, summaries,
and a manifest. Stages print source and destination paths,
selected options, reasons for skips, and verification results. Do not hide
installer, pacman, Git, compiler, or documentation diagnostics in new code.
Pacman update and install commands make up to three visibly logged attempts,
waiting two seconds between failures, and propagate the final command error if
all attempts fail.

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
`build/gamewip/cache/tracy`; `C:\MSYS2\GameWIPTools\tools\tracy` changes only after complete verification.

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

## Adding or changing setup behavior

Prefer data over orchestration branches:

- Add an ordinary project tool to `../config/project-tools.json` using an
  existing provider.
- Add a justified package to the owning MSYS2 group in `config/setup.json`.
- Add an editor entry with a unique key and handler name; implement a handler
  only when existing behavior cannot support it.
- Add new stages as one library operation, one `config/setup.json` entry, and an
  explicit registration in `windows.ps1`.

Every stage must be rerunnable, scoped to the checkout or selected machine
requirements, explicit about changes, and followed by verification.
