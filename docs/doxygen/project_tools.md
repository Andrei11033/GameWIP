@page project_tools Project development tools

GameWIP has one tracked authority for project development tools:
`scripts/config/project-tools.json`. It owns provider selection, version policy,
provider package metadata, detection, update capabilities, and precise live
version references. Setup consumes that registry; it does not keep a second
package or version catalog.

## Provider and version policy

The supported providers are MSYS2, npm, Python, PowerShell Gallery, verified
GitHub releases, WinGet, Git submodules, and external/manual state. Provider
selection follows this order when the tool is available and compatible:

1. An official MSYS2/pacman package.
2. The tool's native ecosystem under the persistent GameWIP tool root.
3. A checksum-verified standalone release.
4. Explicit external/manual ownership.

`exact` pins version-sensitive tools. `minimum` accepts the declared version or
newer. `managed` lets the owning package manager select a compatible version.
`informational` reports externally owned state without taking over its update.

CMake is a `minimum` tool and its registry version must equal the root
`cmake_minimum_required()` value. Newer CMake release lines are accepted.

MSYS2 package requirements are derived from provider metadata in
`project-tools.json`, including UCRT64/CLANG64 companion packages and package-only
dependencies. Gersemi uses the upstream verified standalone release assets
rather than the MSYS2 Python environment because native Python-extension ABI
compatibility can otherwise force source builds on Windows. The exact upstream
release tag, platform asset names, and SHA-256 digests are owned by
`project-tools.json`. `scripts/setup/config/setup.json` owns only setup action
metadata and the IDs needed to bootstrap a provider host.

The GitHub CLI is a managed WinGet tool because the workflow status and dispatch
commands depend on it. Its sign-in state is intentionally not managed by setup.

## Persistent tool ownership

MSYS2 lives at `C:\MSYS2`. Pacman-owned files remain in the standard `usr`,
`ucrt64`, and `clang64` trees. GameWIP never copies unmanaged binaries into
those package-manager directories.

Tools managed directly by GameWIP persist under:

```text
C:\MSYS2\GameWIPTools\
  bin\
  tools\
  npm\
  python\
  powershell\
```

The directory is separate from repository build output and survives deleting
`build/`. When GameWIP creates this tree it writes `.gamewip-managed.json`
inside it. An existing non-empty tree without valid ownership proof is never
silently adopted or recursively removed. Interactive setup may display the
top-level contents and ask, defaulting to No, whether the user explicitly wants
to adopt that root. Adopted ownership is recorded distinctly from
setup-created ownership. Noninteractive setup remains fail-closed, and
`setup.bat check` only diagnoses missing ownership proof without mutating it.

`C:\MSYS2\.gamewip-managed.json` is a different marker. It records proven
GameWIP ownership of the MSYS2 installation itself when setup created it.
Ownership evidence never bypasses recursive-deletion path safety, and setup
still preserves the MSYS2 root for manual review because users can add files or
packages after installation.

## Tool commands

`gamewip tools list` and `gamewip tools status` are offline. `list` reports
registry policy. `status` reports the selected executable/module, required and
installed versions, compatibility, provider, and additional discovered copies.
Selection is deterministic: the declared managed provider location wins on the
Windows development environment, then other GameWIP-managed locations, then
PATH. A repository-owned executable participates only when the registry
explicitly declares its repository path.

`gamewip tools check-updates` is online and read-only. It resolves
all requested latest versions, including versioned provider dependencies,
without changing tracked files or installed software.

`gamewip tools update <id|all>` resolves the complete online
plan before the first mutation. `-Preview` performs that read-only resolution
and prints the plan without changing tracked or machine state. A real update
requires a clean tracked tree, updates structured registry fields and precise
declared live references, updates integrity/tag metadata when applicable,
installs the managed version, and runs `quality check`. Verified downloads are
completed through temporary files before replacing cache destinations, and
archive-backed managed tools stage and verify candidates before the live
version directory changes. Ambiguous or missing
live references fail closed. Historical files under `docs/releases/` are never
rewritten. The command never commits, pushes, or destructively rolls back a
failed sequence.

`setup.bat update` has different semantics: it performs the complete MSYS2
`pacman -Syu` environment update, updates other compatible package-manager
software/integrations, and restores compliance with versions already declared
by the checkout. It does not advance exact project pins. Use **Tools and
environment** in the interactive project menu for tool status/check/update
workflows, and **Quality** for the complete quality/formatting workflows.

## Repository-local mutable storage

All helper-owned mutable repository data is disposable and lives under:

```text
build/gamewip/
  cache/
  state/
  temp/
  runs/
```

`cache/` contains reproducible data. `state/` is advisory and never sole
ownership evidence. Missing or corrupt advisory editor/setup state is reported
and safely reconstructed from configured defaults or persistent evidence rather
than making the helper unusable. `temp/<operation-id>/` is operation-owned and
marked with the owning process identity. Stale cleanup removes an old directory
only when GameWIP ownership is valid and the recorded owner is confirmed
inactive; active, malformed, or ambiguous ownership is preserved. `runs/`
retains logs, manifests, summaries, and artifacts.

Deleting `build/` or any storage child is a supported recovery operation.
Doctor, status, quality, and setup recreate the directories they need without
reinstalling persistent tools.

## Quality configuration

Explicit formatter and linter policy is grouped under `config/quality/`:

- `ruff.toml`
- `eslint.config.js`
- `prettier.json` and `prettier.ignore`
- `gersemi.yml`
- `yamllint.yml`
- `markdownlint-cli2.jsonc`
- `psscriptanalyzer.psd1`
- `file-ownership.json`

The project helper passes these paths explicitly, so their location is not a
hidden discovery dependency. `.clang-format`, `.clang-tidy`, and
`.editorconfig` intentionally remain at repository root because editor and tool
upward discovery is useful for C++ and basic text settings.

The ownership registry gives every maintained worktree file one quality policy.
Full quality includes tracked files and non-ignored untracked first-party files,
so new project files are checkable and formattable before they are staged.
Language and structured-data sources use their declared formatter
and parser or linter. Tool-owned metadata such as `CODEOWNERS` and `prettier.ignore` is intentionally not reformatted but remains subject to its
owning tool, repository checks, and generic text rules. Windows manifests are parsed as XML and validated again by resource compilation. Generated,
third-party, historical, and binary artifacts stay outside maintained formatting only through explicit policy boundaries.

`gamewip quality check` performs deterministic format checks,
language linters, schema/semantic validation, workflow validation,
documentation checks, and link validation. `fix` runs deterministic formatters
only and then executes the same check. It does not auto-rewrite prose, workflow
behavior, or semantic CMake policy.

## Troubleshooting

Use `gamewip tools status` to inspect tool selection and competing
copies. Use `gamewip doctor` to verify the complete declared development
environment. Use `setup.bat repair` when provider-owned software is missing or
incompatible.

If advisory setup state is missing or corrupt, setup reports that condition and
reconstructs what it safely can from persistent evidence. Unknown resources are
preserved. If an existing `GameWIPTools` tree has no ownership marker, review it
manually instead of forcing uninstall.

Do not put persistent tools under `build/`, place unmanaged files in
pacman-owned `bin` directories, or treat `build/gamewip/state` as durable
machine ownership evidence.
