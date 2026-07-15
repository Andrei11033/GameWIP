@page project_getting_started Getting started with the project

This page is the first manual page for a developer who has the repository but does not yet know how GameWIP is arranged. It gives the shortest complete path from a fresh checkout to a built, runnable, validated source tree.

Use the linked pages for the full contracts. This page intentionally summarizes the first session instead of replacing the detailed build, validation, structure, and contribution pages.

## Prerequisites

GameWIP is currently Windows-first. Normal development uses:

- Windows.
- MSYS2 UCRT64 GCC.
- CMake 4.4.x.
- Ninja.
- Git submodules.

AddressSanitizer validation uses the MSYS2 CLANG64 environment. Keep UCRT64 and CLANG64 builds in separate preset build trees and put the matching MSYS2 `bin` directory first on `PATH` before configuring that preset.

The full preset and toolchain details are documented in @ref project_build.

## First checkout setup

Initialize third-party dependencies after cloning:

```powershell
git submodule update --init --recursive
```

For normal development, ensure `C:\MSYS2\ucrt64\bin` appears before other MinGW or MSYS2 environments on `PATH`.

VS Code users can open `GameWIP.code-workspace`. Configure the `development` preset before relying on IntelliSense because the workspace reads `build/development/compile_commands.json`.

## Configure, build, and run

Configure and build the development preset:

```powershell
cmake --preset development
cmake --build --preset development
```

Print runtime version information:

```powershell
.\build\development\GameWIP.exe --version
```

Run the development executable:

```powershell
.\build\development\GameWIP.exe
```

The development preset builds the game and runs configured startup validation before entering the runtime facade. Shipping builds intentionally exclude validation, benchmarks, assertions, Tracy, and development tools from the game executable.

## Run validation

Use the validation preset for standalone correctness testing:

```powershell
cmake --preset validation
cmake --build --preset validation
ctest --preset validation
```

Run one module directly when investigating a focused area:

```powershell
.\build\validation\GameWIPTests.exe --test-module=filesystem
```

The validation architecture, module registration, child-process routing, report paths, and startup behavior are documented in @ref project_validation. Test authoring rules are documented in @ref project_testing.

## Read the source tree

Start with these directories:

| Path | First meaning |
| --- | --- |
| `foundation/` | Low-level reusable libraries such as IO, FileSystem, and Terminal. |
| `tools/` | Logger, Assert, TestSupport, diagnostics, and development support libraries. |
| `game/` | Executable entry point, runtime facade, startup validation wiring, and validation runners. |
| `cmake/` | Project-wide CMake helpers, presets, packaging, validation, docs, and analysis wiring. |
| `docs/doxygen/` | Generated manual pages for workflows and contracts. |
| `docs/` | Vision, roadmap, decisions, versioning, and contributor workflow records. |
| `external/` | Pinned third-party dependencies. |

The full ownership map and dependency direction are documented in @ref project_structure.

## Where output goes

CMake presets write build trees under `build/<preset>`.

Common outputs include:

- `build/development/GameWIP.exe`
- `build/validation/GameWIPTests.exe`
- `build/validation/GameWIPBenchmarks.exe`
- `build/docs/docs/doxygen/html/index.html`

Generated files and build artifacts should stay in build directories unless a workflow page explicitly documents a retained source artifact.

## What to read next

After the first build and validation run:

- Read @ref project_structure to understand ownership and dependency direction.
- Read @ref project_build before changing presets, options, runtime dependency copying, or build outputs.
- Read @ref project_validation and @ref project_testing before changing tests or startup validation.
- Read @ref project_reusable_libraries before using or changing a first-party library.
- Read @ref project_extending before adding APIs, libraries, backends, benchmarks, packages, or manual pages.
- Read @ref project_documentation before writing public API comments or manual pages.

Specialized workflows such as coverage, profiling, benchmarking, static analysis, repository automation, and release automation are documented under @ref project_quality_workflows and @ref project_contracts. Use those pages when the work actually touches those systems.

## Change workflow

Normal project work uses a short-lived branch, a pull request, concrete validation notes, and a squash merge. Use @ref project_contributing for issue, branch, pull-request, label, automation, and merge-message rules.

When a change modifies behavior, update the owning manual page and run the preset that proves the change. A vague statement such as `tested` is not useful validation evidence; record the exact commands or inspections performed.
