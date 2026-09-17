@page project_tools Project development tools

GameWIP keeps its project-tool policy in `scripts/config/project-tools.json`.
The registry declares providers, versions, package metadata, detection, update
capabilities, and repository references. Setup and the project helper both use
it, so adding a tool with an existing provider or adding another supported
reference means updating the registry rather than adding a separate
orchestration path.

## Provider and version policy

The supported providers are MSYS2, npm, Python, PowerShell Gallery, verified
GitHub releases, WinGet, Git submodules, and external/manual state. Provider
selection follows this order when the tool is available from more than one
source:

1. An official MSYS2/pacman package.
2. The tool's native ecosystem under the persistent GameWIP tool root.
3. A checksum-verified standalone release.
4. Explicit external/manual ownership.

`exact` pins version-sensitive tools. `minimum` accepts the declared version or
newer. `managed` lets the owning package manager select a compatible version.
`informational` reports externally owned state without taking over its updates.

CMake is a `minimum` tool and its registry version must equal the root
`cmake_minimum_required()` value. Newer CMake release lines are accepted.

MSYS2 package requirements come from provider metadata in
`project-tools.json`, including UCRT64 and CLANG64 companion packages and
package-only dependencies. Gersemi uses verified upstream release assets rather
than the MSYS2 Python environment because Python-extension ABI differences can
otherwise force source builds on Windows. The registry owns the upstream tag,
platform asset names, and SHA-256 digests. `scripts/setup/config/setup.json`
owns setup-action metadata and the IDs needed to bootstrap a provider host.

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
inside it. An existing non-empty tree without valid ownership proof must never
be silently adopted or recursively removed. Interactive setup may show its
top-level contents and ask whether the
user wants to adopt it, with No as the default. Adopted ownership is recorded
separately from setup-created ownership. Noninteractive setup remains
fail-closed, and `setup.bat check` reports missing ownership proof without
changing it.

`C:\MSYS2\.gamewip-managed.json` is a different marker. It records proven
GameWIP ownership of the MSYS2 installation itself when setup created it.
Ownership evidence never bypasses recursive-deletion path safety. Setup still
preserves the MSYS2 root for manual review because users can add files or
packages after installation.

## Tool commands

`gamewip.bat tools list` and `gamewip.bat tools status` are offline. `list` reports
registry policy. `status` reports the selected executable/module, required and
installed versions, compatibility, provider, and additional discovered copies.
Selection follows a fixed order: the declared managed provider location wins on
the Windows development environment, followed by other GameWIP-managed
locations and then `PATH`. A repository-owned executable participates only when
the registry explicitly declares its repository path.

`gamewip.bat tools check-updates` is online and read-only. It resolves
all requested latest versions, including versioned provider dependencies,
without changing tracked files or installed software.

`gamewip.bat tools update <id|all>` discovers upstream state and builds the plan
before persistent mutation. `-Preview` runs discovery, planning, tracked
staging, and staged validation, then prints exact registry fields and declared
references without applying tracked or machine changes. A real update requires
a clean tracked tree, requests consent for that validated plan, applies it,
verifies the planned declaration, and runs `quality check`.

Interactive multi-selection stages all selected tools together and writes shared
files once. It does not start a new update against the dirty tree produced by an
earlier selected tool.

Reference behavior comes entirely from each declaration:

- `path` records an informational association and is never rewritten.
- `text` is a live exact-text reference. Its `pattern` is a literal template,
  not a regular expression, and contains exactly one `{version}` token.
- `cmakeMinimum` is a live semantic CMake minimum-version reference.

Live references expect one occurrence by default and fail closed when the
declared count does not match. Several updates that target one file are composed
in staged content, then the file is written once. Historical documents remain
unchanged because they use informational `path` references; no directory name
has special behavior.

Registry updates use compare-and-set mutations. Each exact JSON string target
includes its expected old value and planned new value. The source-preserving
mutator replaces only those scalar tokens, leaving property order, tool and
dependency order, whitespace, escapes, and unrelated Unicode unchanged.
Repository-owned configuration and reference text is read as strict UTF-8.
Malformed input fails, and writes remain UTF-8 without a byte-order mark.

Provider adapters own provider-specific queries, installation, and rollback.
The shared engine owns discovery, planning, staging, validation, preview,
consent, application, verification, and quality. Verified GitHub-release
providers download and checksum a candidate, verify it before replacement, and
restore the previous managed tool and shim if replacement fails. If every
selected declaration and installation is current, the update is a true no-op:
it neither stages tracked files nor reinstalls an identical tool. The command
never commits or pushes.

`setup.bat update` has a different role. It performs the MSYS2 `pacman -Syu`
environment update, updates other compatible package-manager software and
integrations, and restores compliance with versions already declared by the
checkout. It does not advance exact project pins. Use **Tools and environment**
in the interactive project menu for tool status, check, and update workflows;
use **Quality** for quality and formatting.

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
- `hygiene.json`

The project helper passes these paths explicitly, so their location is not a
hidden discovery dependency. `.clang-format`, `.clang-tidy`, and
`.editorconfig` intentionally remain at repository root because editor and tool
upward discovery is useful for C++ and basic text settings.

`hygiene.json` declares optional audit profiles, provider-backed checks,
confidence levels, and reviewed explanations. Providers may use clang-tidy rules
or compiler warning flags such as `-Wunreachable-code`; the selected provider
owns how each rule is executed and normalized. Schema and semantic validation
reject duplicate IDs and unknown references. The registry does not change the
required repository quality gate or the root `.clang-tidy` policy.

The ownership registry gives every maintained worktree file one quality policy.
Full quality includes tracked files and non-ignored untracked first-party files,
so new project files can be checked and formatted before they are staged.
Language and structured-data sources use their declared formatter and parser or
linter. Tool-owned metadata such as `CODEOWNERS` and `prettier.ignore` is not
reformatted, but remains subject to its owning tool, repository checks, and
generic text rules. Windows manifests are parsed as XML and validated again by
resource compilation. Generated, third-party, historical, and binary artifacts
stay outside maintained formatting through explicit policy boundaries.

`gamewip quality check` performs deterministic format checks, language linters,
schema and semantic validation, workflow validation, documentation checks, and
link validation. `fix` runs deterministic formatters only and then executes the
same check. It does not rewrite prose, workflow behavior, or semantic CMake
policy.

## Troubleshooting

Use `gamewip tools status` to inspect tool selection and competing
copies. Use `gamewip doctor` to verify the complete declared development
environment. Use `setup.bat repair` when provider-owned software is missing or
incompatible.

If advisory setup state is missing or corrupt, setup reports that condition and
reconstructs what it safely can from persistent evidence. Unknown resources are
preserved. If an existing `GameWIPTools` tree has no ownership marker, review it
manually instead of forcing uninstall.

Persistent tools belong outside `build/`, unmanaged files do not belong in
pacman-owned `bin` directories, and `build/gamewip/state` is not durable machine
ownership evidence.
