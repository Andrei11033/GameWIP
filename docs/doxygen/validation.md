@page project_validation Validation architecture

GameWIP validation is modular. The same correctness-test and benchmark modules can be built as standalone executables for CI and focused local work, or compiled into the development game executable as startup validation.

Validation is not linked into shipping builds.

## Scope

This page documents validation build controls, source layout, module registration, runner command-line behavior, startup validation, child-process routing, report ownership, and module lifecycle.

Correctness-test authoring rules are documented in @ref project_testing. Benchmark authoring rules are documented in @ref project_benchmarking.

## Build controls

| Option | Purpose | Source default |
| --- | --- | --- |
| `GAMEWIP_BUILD_TESTS` | Builds `GameWIPTests` and CTest entries. | `ON` |
| `GAMEWIP_BUILD_BENCHMARKS` | Builds `GameWIPBenchmarks`. | `OFF` |
| `GAMEWIP_RUN_TESTS_AT_STARTUP` | Links tests into `GameWIP` and runs them before the game. | `ON` |
| `GAMEWIP_RUN_BENCHMARKS_AT_STARTUP` | Links benchmarks into `GameWIP` and runs them after tests. | `OFF` |

Tests stop startup when they fail. Child-process validation invocations return directly without entering benchmarks or game code. Benchmarks report measurements and registration errors, but they do not enforce performance thresholds.

When both build and startup options for a validation kind are disabled, its modules are not compiled or linked into the game target. Google Benchmark is added only when benchmark targets or startup benchmarks are required.

## Source layout

```text
game/validation/
  validation.h
  tests/
    runner.h
    runner.cpp
    registry.h
    registry.cpp
    internal/
      runner_test_hooks.h
    <module>/
      CMakeLists.txt
      module.cpp
      <module>_test.cpp
  benchmarks/
    runner.cpp
    <module>/
      CMakeLists.txt
      <module>_benchmark.cpp
```

The `tests/` and `benchmarks/` parent directories discover immediate child directories containing `CMakeLists.txt`. Source discovery stops there. Each module CMake file must explicitly list its sources and linked libraries.

`tests/internal/` is not a validation module. It contains source-tree-only seams for testing the runner implementation. Ordinary modules should use `runner.h` and `registry.h` and must not depend on internal runner hooks.

## Runtime flow

`game/main.cpp` follows this sequence:

1. Run compiled-in correctness tests when startup tests are enabled.
2. Return immediately when startup tests fail or when a child-process validation route is selected.
3. Run compiled-in benchmarks when startup benchmarks are enabled.
4. Enter `GameWIP::Game::run()`.

`validation/validation.h` supplies successful inline no-op functions when startup validation is disabled. This keeps `main.cpp` stable without retaining validation dependencies in disabled builds.

## Module registration

A test module registers one static `Registration` record in its `module.cpp` file. The registration provides:

- Stable module name.
- Deterministic module order.
- Run callback.
- Optional child-argument matcher.

Example:

```cpp
const GameWIP::Validation::Tests::Registration registration({
    .name = "filesystem",
    .order = 20,
    .run = run,
});
```

The C++ registration name must match the CMake module name. The runner validates registrations and duplicate names before executing modules.

Current correctness modules include `assert`, `filesystem`, `io`, `logger`, `runner`, `terminal`, and `test_support`.

## Command-line interface

The shared runner consumes project-level validation arguments before invoking a module.

| Argument | Behavior |
| --- | --- |
| `--test-module=<name>` | Runs one registered module. An unknown name is an error. |
| `--test-report=<path>` | Selects the aggregate report path. Relative paths resolve under `%TEMP%/GameWIP` on Windows. |
| `--no-test-report` | Disables the retained report without disabling console outcomes. |
| `--verbose-tests` | Mirrors every TestSupport category to stdout. |
| `--manual-ui` | Enables human-interactive validation checks. |
| `--logger-popup` | Enables the Logger fatal-popup validation check. |
| `--no-test-support-child-process` | Skips TestSupport process-launch scenarios. |

Legacy focused aliases such as `--filesystem-only` remain supported for compatibility. New tooling should use `--test-module=<name>` because it scales without adding runner arguments.

Capability switches do not select modules by themselves. Combine them with `--test-module=<name>` for focused manual validation:

```powershell
.\build\validation\GameWIPTests.exe --test-module=test_support --manual-ui
.\build\validation\GameWIPTests.exe --test-module=terminal --manual-ui
.\build\validation\GameWIPTests.exe --test-module=logger --logger-popup
```

Without positive capability flags, validation remains unattended.

## Child-process routing

Module-owned child arguments are matched before normal module selection.

- No child match continues to ordinary selection.
- One match routes the original process arguments to the owning module and returns the module's exact exit code.
- Multiple matches are an ambiguity error.

This prevents crash-test, fatal-test, and child-process scenarios from recursively running the full validation set or entering game startup.

## Output responsibilities

| Output | Owner | Normal content |
| --- | --- | --- |
| Console suite detail | TestSupport | Failures, skips, and manual instructions only. |
| Console module result | Validation runner | One module PASS or FAIL line and exact module exit code. |
| Console aggregate | Validation runner | Selected-module count and failed-module count. |
| Retained report | TestSupport modules | Complete INFO, PASS, FAIL, SKIP, MANUAL, METRIC, STRESS, RESULT, and SUMMARY detail. |

This split keeps successful runs scannable without discarding evidence. `--verbose-tests` changes only console mirroring; it does not change execution, counts, exit codes, or retained report content.

## Report paths

Relative report paths resolve beneath the GameWIP directory in the operating-system temporary root. On Windows, this is `%TEMP%/GameWIP`.

CTest invokes the standalone runner once per module and gives each module a stable report path. The runner prints each module outcome, the absolute report path, and the aggregate result to stdout.

## Module lifecycle

1. The runner copies registrations and sorts them by order, then name.
2. Registration validity and duplicate names are checked before any module runs.
3. Child-argument ownership is checked before ordinary module selection.
4. The runner resolves report policy once and passes shared policy through `ModuleInvocation`.
5. Each adapter maps shared policy into library-specific options and executes its suite.
6. An escaped exception becomes a failed module result.
7. The runner records the module exit code and continues to the next selected module.

An invalid registration, unknown module selection, selected-and-excluded module, ambiguous child route, escaped module exception, or failed module produces a nonzero validation result. An invalid report path disables only file reporting and does not hide console results.

## Runner test seam

The public `run()` entry point always executes modules returned by `registeredModules()`. Its implementation delegates to the same `Detail::runWithModules()` path used by the `runner` correctness-test module.

`runWithModules()` accepts an explicit module span so runner tests can supply isolated probe modules and verify parsing, selection, option propagation, ordering, and default behavior without mutating the process-wide registry. Its declaration lives in `tests/internal/runner_test_hooks.h`; it is source-tree-only, is not installed, and is not application or module API.

The `tests/runner/` directory is a normal registered validation module. It uses the internal seam only to test the shared runner implementation.

## Preset behavior

| Preset | Validation behavior |
| --- | --- |
| `development` | Startup correctness tests enabled. |
| `validation` | Standalone tests enabled and benchmark dry-run target available. |
| `benchmark` | Optimized standalone benchmarks only. |
| `shipping` | Validation and assertions disabled. |
| `coverage` | Standalone correctness tests with coverage instrumentation. |
| `address-sanitizer` | Standalone correctness tests with AddressSanitizer instrumentation. |
| `docs` | Validation disabled; Doxygen only. |

## Maintainer notes

When adding validation behavior:

- Add correctness coverage before benchmark coverage.
- Give every module a stable lowercase name.
- Keep runtime child-process protocols owned by exactly one module.
- Keep source-tree-only seams under `internal/` and out of installed packages.
- Document any module-specific manual UI, fatal popup, or child-process behavior.
- Record focused validation commands in the pull request.

## Related pages

- @ref project_testing
- @ref project_benchmarking
- @ref project_build
- @ref project_extending
