# Setup implementation

This directory implements the Windows setup experience behind the root
`setup.bat` entry point. Contributors looking to install or repair their
environment should use the generated
[environment setup manual](../../docs/doxygen/environment_setup.md). The
details below are for maintainers changing what setup installs or how it
verifies the machine.

## Layout

- `windows.bat` forwards batch arguments without owning setup behavior.
- `Windows.ps1` owns action dispatch, consent, execution plans, and final
  verification. The persistent menu is rendered by the shared console backend.
- `../lib/Operation.ps1`, `../lib/Process.ps1`, and `../lib/Runs.ps1` own the
  operation lifecycle, native execution, retained output, receipts, and run
  layout shared with the project helper.
- `config/setup.json` owns setup action/menu metadata and provider-host bootstrap
  IDs only; it does not duplicate machine packages or tool versions.
- `config/editors.json` declares selectable editors and their handlers. VS Code
  is the default; Visual Studio is optional.
- `../config/project-tools.json` is the single project-tool, provider-package,
  dependency, detection, capability, version-policy, and repository-reference
  authority consumed by setup and the project helper. Existing-provider tools
  and references are added as data, without tool-specific update branches.
- `lib/` contains focused reusable operations.
- `editor/gamewip-workflows/` is the declarative VS Code workflow extension.

Per-checkout editor selection is stored as disposable advisory state in
`build/gamewip/state/editor-selection.json`.
Noninteractive first use selects the configured default.

## Behavior that setup must preserve

Machine-changing interactive actions require Automatic, Manual, or Cancel
consent. Named actions preserve nonzero exit codes. The menu catches an action
failure, prints its concise cause, and returns to the full action list.

`Invoke-GameWipSetupNative` uses the shared native-process runner, inherits the selected `Stream`, `Summary`, or `LogOnly` output policy,
and reports successful exit codes. Named setup actions retain the same
action-scoped run structure as the project helper under
`build/gamewip/runs/<timestamp>_setup-<action>/`, including step logs, summaries,
and a manifest. Stages print source and destination paths,
selected options, reasons for skips, and verification results. Do not hide
installer, pacman, Git, compiler, or documentation diagnostics in new code.
Pacman update and install commands make up to three visibly logged attempts,
waiting two seconds between failures, and propagate the final command error if
all attempts fail.

Full setup installs missing state and refreshes MSYS2 before installing its
declared packages. Repair reapplies missing state without requesting ordinary
upgrades. Update fetches and fast-forwards the current branch from its configured
upstream, performs complete `pacman -Syu` passes, and applies the newest
compatible environment/provider releases while keeping CMake at the declared
minimum `4.4.2` or newer and retaining submodule revisions recorded by the
updated checkout. It refuses dirty trees, missing upstreams, and
non-fast-forward merges. WinGet is used for MSYS2 only for the first installation
at the explicit `C:\MSYS2` root.

`setup.bat update` owns environment and package-manager updates. The separate
`gamewip tools update` workflow advances reviewed project tool/version policy
and exact pins through a staged, source-preserving compare-and-set plan; setup
does not silently advance those pins.

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
pinned submodule using UCRT64 GCC/Ninja. Reproducible CMake/CPM build state lives
under `build/gamewip/cache/tracy`, while the candidate executable/DLL set stages
only under the current `build/gamewip/temp/<operation-id>/tracy-stage`. Generated
compatibility adjustments provide the POSIX `memmem` operation missing from
UCRT64 and remove incompatible COFF LTO flags without modifying the submodule.
The required Windows security library is linked explicitly.
`C:\MSYS2\GameWIPTools\tools\tracy` changes only after the complete candidate
is verified, using a same-volume directory swap so a failed replacement can
preserve or restore the previous verified set instead of mixing old and new
files.

Focused `docs` builds, verifies, and opens the generated manual after normal
local-mutation consent. `-Preview` prints that plan without configuring,
building, or opening anything. Complete
setup/update/repair builds the same manual without launching a browser.

An extracted GitHub ZIP is supported. After Git is available, repository setup
initializes metadata in place, connects the official remote, compares fetched
branches with the extracted files, and asks which branch to track. Existing
interactive checkouts also offer a branch choice; `-Branch` supplies it for
automation. Branch switches refuse tracked local changes. Extracted files remain
untouched while pinned submodules become available.

Persistent directly managed tools live under
`C:\MSYS2\GameWIPTools`. A non-empty root without valid ownership proof is never
silently adopted. Interactive machine-changing setup may show its top-level
contents and ask, defaulting to No, whether the user explicitly adopts that
root. Noninteractive setup remains fail-closed. The read-only check reports the
condition without writing ownership state.

Uninstall removes the repository-owned VS Code workflow extension/keybinding
block, proven GameWIPTools state, recorded setup-owned applications/extensions,
the Tracy cache, and setup-generated `build/dev` and `build/docs` trees. It
preserves software that existed beforehand, the checkout, other build trees,
user files, ownership-unknown GameWIPTools content, and an MSYS2 tree that may
contain later user data.

## Adding or changing setup behavior

Prefer data over orchestration branches:

- Add an ordinary project tool or justified MSYS2 package/dependency to
  `../config/project-tools.json` using the owning provider metadata.
- Add only action/bootstrap metadata to `config/setup.json`; do not recreate a
  second package or version catalog there.
- Add an editor entry with a unique key and handler name; implement a handler
  only when existing behavior cannot support it.
- Add new stages as one library operation, one `config/setup.json` entry, and an
  explicit registration in `Windows.ps1`.

Every stage must be rerunnable, scoped to the checkout or selected machine
requirements, explicit about changes, and followed by verification.
