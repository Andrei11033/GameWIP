@page project_tools Project development tools

GameWIP declares development-tool policy in
`scripts/config/project-tools.json`. Its schema fixes the supported provider
kinds, version policies, detection commands, capabilities, download metadata,
and live version references. Provider behavior lives in
`scripts/lib/Providers/`; adding a tool that uses an existing provider requires
registry data, not a tool-specific branch.

## Versions and providers

The supported providers are MSYS2, npm, Python, PowerShell Gallery, verified
GitHub releases, winget, Git submodules, and external/manual tools. Prefer a
suitable pacman package, then the tool's native ecosystem, then a checksum-
verified GitHub release, and finally an explicit external dependency.

`exact` pins version-sensitive quality tools. `minimum` accepts that version or
newer. `managed` delegates version selection to the environment's package
manager. `informational` reports externally owned state without taking over its
updates. Unicode data, GitHub Actions, Tracy, the editor extension, and the
runtime version retain their specialized authorities.

Non-pacman tools persist under `C:\MSYS2\GameWIPTools`, using its `bin`,
`tools`, `npm`, `python`, and `powershell` children. Standalone releases use
`tools/<tool>/<version>` and stable shims in `bin`. This tree is marked as
GameWIP-managed but remains distinct from the MSYS2 installation ownership
marker. Setup and uninstall require both ownership evidence and a safe resolved
path before deleting anything.

## Commands

`gamewip tools -ToolsAction list` and `status` are offline. `list` describes
registry policy and capabilities; `status` also detects installed versions,
compatibility, and locations. Tools that cannot check or update still appear.

`gamewip tools -ToolsAction check-updates` explicitly uses the network and only
reports current, installed, and latest versions. It changes neither the checkout
nor installed tools.

`gamewip tools -ToolsAction update -Tool <id|all>` first builds a complete
online plan. `-Preview` never mutates the repository or machine. A real update
requires a clean working tree, updates the central pin and integrity/provider
metadata, installs the managed version, synchronizes unambiguous live
references, and runs affected validation plus `quality check`. It never commits,
pushes, or destructively rolls back a failed sequence.

`setup.bat update` has different ownership: it updates package-manager software
and restores compliance with the current checkout. It never advances exact
project pins. Historical files under `docs/releases/` are not rewritten during
tool updates; every current reference must agree with the registry.

## Disposable repository storage

All helper-owned mutable repository data lives below `build/gamewip/`:

- `cache/` contains reproducible, self-validating downloads and query data.
- `state/` contains advisory convenience state, never sole ownership evidence.
- `temp/<operation-id>/` isolates marked operation data and is removed in
  `finally`; stale cleanup only removes verified GameWIP-owned directories.
- `runs/<run-id>/artifacts/` retains manifests, logs, summaries, and diagnostics.

Deleting `build/`, any individual storage child, or corrupt cache metadata is a
supported recovery operation. Setup checks, doctor, tool status, and quality
recreate needed directories without reinstalling persistent tools or inferring
unsafe ownership from missing advisory state.

## Troubleshooting

Run `gamewip tools -ToolsAction status` to compare the checkout with the local
machine, and `gamewip doctor` to test whether the complete development
environment is usable. Run `setup.bat repair` for missing prerequisites, broken
managed installations, or incompatible exact pins. Use update preview before a
pin change, and resolve a dirty working tree or ambiguous live reference before
retrying a real update.

Do not place unmanaged binaries in pacman-owned directories, create repository-
local default virtual environments, or treat `build/gamewip/state` as durable
machine ownership evidence.
