@page project_structure Project structure and runtime flow

GameWIP is one product repository with reusable C++ libraries around a small executable shell. Project code owns composition and policy; each library owns its public contract, implementation, platform backends, package, tests, and manual.

## Repository map

| Path | Ownership |
| --- | --- |
| `foundation/` | Low-level reusable IO, filesystem, and terminal libraries. |
| `tools/` | Reusable diagnostics and validation-support libraries. |
| `engine/` | Engine systems. This section is developed and reviewed separately from the foundation described here. |
| `game/` | Process entry point, runtime composition, and modular validation executables. |
| `cmake/` | Repository-wide build, platform, validation, coverage, documentation, and analysis helpers. |
| `docs/doxygen/` | Project-wide generated manual pages. |
| `docs/` | Vision, decisions, roadmap, contribution policy, contracts, and milestone checklists. These are ordinary repository Markdown, not generated-manual inputs. |
| `.github/` | Pull-request policy, CI, documentation publishing, and project automation. |
| `external/` | Pinned third-party dependencies; project checks do not rewrite or analyze their sources. |

Build output belongs only in `build-<preset>/` directories. Runtime tests use scoped operating-system temporary directories and retain only their requested aggregate reports.

## Dependency direction

The intended reusable dependency flow is:

```text
IO
  -> FileSystem
  -> Terminal
IO + FileSystem + Terminal
  -> Logger
Logger
  -> Assert runtime
TestSupport
  -> validation modules
libraries + optional validation modules
  -> GameWIP executable
```

Arrows mean “is consumed by.” Lower-level libraries must not depend on the game executable or validation runner. Tests may include explicitly enabled internal hooks, but installed consumers cannot see those headers.

## Process startup

`game/main.cpp` is deliberately stable. It runs compiled-in correctness tests, returns immediately for a test child process or failure, optionally runs benchmarks, and then calls `GameWIP::Game::run()`. Disabled startup validation becomes inline no-op code, so shipping builds do not retain test or benchmark dependencies.

The current game runtime is intentionally thin. Most implemented behavior is in reusable libraries and their validation modules; future game and engine composition should remain behind the runtime facade rather than accumulating in `main.cpp`.

## Public and internal boundaries

Every installable library declares a CMake `FILE_SET` containing its public headers. Installation exports only that file set and generated shared-library export headers. Internal platform contracts and test-hook headers are not installed. Validation builds compile every public entry header independently, inspect shared-library export allowlists, and build a separate consumer against a clean install prefix.

Public templates, pImpl ownership, and assertion macros require a few declarations in `Detail` namespaces. These declarations are implementation bridges, not an independently supported API. External callers receive the minimum header set needed to compile the supported API, but C++ cannot make declarations in a public header literally invisible.

Inside this source tree, target include roots make owning-library `internal/` headers physically reachable. Only the owning implementation and explicit validation tests may include them. The install boundary is mechanically enforced; the source-tree rule is an architectural convention reviewed in changes.

## What “standalone library” means here

The libraries are standalone at the installed-package boundary: a clean external CMake project can use `find_package()` and link a canonical `GameWIP::` imported target without source-tree paths or the game executable. They are not currently independent top-level source distributions; their CMake files use repository helpers, the root version, and the shared platform selection.

`GameWIP::` is intentional ownership, not unwanted coupling. Generic package names such as `Logger` or `IO` are acceptable while packages ship together inside this pre-1.0 repository, but a future public distribution should either use one `GameWIP` package with components or rename packages to globally distinctive names before compatibility is promised.

See @ref project_build for presets, @ref library_compatibility for package contracts, and @ref project_extending for adding systems.
