@page project_validation Validation architecture

GameWIP uses the same modular validation sources in two launch modes:

- Standalone `GameWIPTests` and `GameWIPBenchmarks` executables for CI and focused local work
- Optional startup validation compiled into `GameWIP` for the normal development workflow

Validation is not linked into shipping builds.

## Build controls

| Option | Purpose | Default |
| --- | --- | --- |
| `GAMEWIP_BUILD_TESTS` | Build `GameWIPTests` and CTest entries. | `ON` |
| `GAMEWIP_BUILD_BENCHMARKS` | Build `GameWIPBenchmarks`. | `OFF` |
| `GAMEWIP_RUN_TESTS_AT_STARTUP` | Link tests into `GameWIP` and run them before the game. | `ON` |
| `GAMEWIP_RUN_BENCHMARKS_AT_STARTUP` | Link benchmarks into `GameWIP` and run them after tests. | `OFF` |

Tests stop startup when they fail. Child-process test invocations return directly without entering benchmarks or game code. Benchmarks report measurements but do not enforce performance thresholds.

When both build and startup options for a validation kind are off, its modules are not compiled or linked into the game target. Google Benchmark is added only when a benchmark target is required.

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

The tests and benchmarks parent directories discover immediate child directories containing `CMakeLists.txt`. Source discovery stops there: each module CMake file must explicitly list its sources and linked libraries.

`tests/internal/` is not a module and is not discovered by CMake. It contains source-tree-only validation implementation seams. Ordinary modules should use `runner.h` and `registry.h`; they must not depend on internal runner testing interfaces.

## Runtime flow

`game/main.cpp` has a stable sequence:

1. Run compiled-in correctness tests.
2. Return a failing or child-process exit code when required.
3. Run compiled-in benchmarks.
4. Enter `GameWIP::Game::run()`.

`validation/validation.h` supplies successful/no-op inline functions when startup validation is disabled. This keeps `main.cpp` stable without retaining validation dependencies.

## Module registration

Test modules register a name, deterministic order, run callback, and optional child-argument matcher. The shared runner performs child routing before ordinary selection, supports `--test-module=<name>`, and appends module output to one startup report.

CTest invokes the standalone runner once per module and gives every module its own report path. New modules therefore appear as focused CTest failures without changing `main.cpp` or the shared runner.

Relative report paths resolve beneath the GameWIP directory in the operating-system temporary root. Module workspaces use scoped TestSupport cleanup, so only final text reports remain after validation. The runner prints each module outcome, the absolute report path, and the aggregate result to stdout.

### Command-line ownership

The shared runner consumes project-level validation arguments before invoking a module:

| Argument | Behavior |
| --- | --- |
| `--test-module=<name>` | Runs one registered module. An unknown name is an error. |
| `--test-report=<path>` | Selects the aggregate report. Relative paths resolve under `%TEMP%/GameWIP`. |
| `--no-test-report` | Disables the retained report without disabling console outcomes. |
| `--verbose-tests` | Mirrors every TestSupport category to stdout. |
| `--manual-ui` | Enables human-interactive validation checks. |
| `--logger-popup` | Enables the Logger fatal-popup check. |
| `--no-test-support-child-process` | Skips TestSupport process-launch scenarios. |

Legacy focused aliases such as `--filesystem-only` remain supported, but new tooling should use `--test-module=<name>` because it scales without adding runner code.

`--manual-ui` and `--logger-popup` are independent capability switches: they do not select or exclude modules. For example, `--test-module=test_support --manual-ui` runs TestSupport human prompts, `--test-module=terminal --manual-ui` runs the real-console Terminal suite, and `--test-module=logger --logger-popup` runs the Logger fatal-popup check. Without the positive flags, validation remains unattended.

Module-owned child arguments are matched before normal selection. One owning module receives the original process arguments, and its exact exit code is returned directly. No match continues to ordinary selection; multiple matches are an ambiguity error. This prevents a crash-test child from recursively running the full validation set or entering game startup.

### Output responsibilities

| Output | Owner | Normal content |
| --- | --- | --- |
| Console suite detail | TestSupport | Failures, skips, and manual instructions only. |
| Console module result | Validation runner | One PASS/FAIL line and exact module exit code. |
| Console aggregate | Validation runner | Selected-module count and failed-module count. |
| Retained report | TestSupport modules | Complete INFO, PASS, FAIL, SKIP, MANUAL, METRIC, STRESS, RESULT, and SUMMARY detail. |

This split keeps successful runs scannable without discarding evidence. `--verbose-tests` changes only console mirroring; it does not change execution, counts, exit codes, or retained report content.

### Module lifecycle

1. A module's `module.cpp` creates one static `Registration` record.
2. The runner copies registrations and sorts by order, then name.
3. Registration validity and duplicate names are checked before any module runs.
4. Child-argument ownership is checked before ordinary module selection; multiple owners fail explicitly.
5. The runner resolves the report path once and passes shared policy through `ModuleInvocation`.
6. Each adapter maps shared policy into its library-specific options and executes its suite; an escaped exception becomes a failed module result.
7. The runner records the module exit code, then continues to the next selected module.

An invalid report path disables only file reporting. An invalid registration, unknown module selection, selected-and-excluded module, ambiguous child route, escaped module exception, or failed module produces a nonzero validation result.

### Runner test seam

The public `run()` entry point always executes the modules returned by `registeredModules()`. Its implementation delegates to the same `Detail::runWithModules()` path used by the `runner` correctness-test module.

`runWithModules()` accepts an explicit module span so runner tests can supply isolated probe modules and verify parsing, selection, option propagation, ordering, and default behavior without mutating the process-wide registry. Its declaration lives in `tests/internal/runner_test_hooks.h`; it is source-tree-only, is not installed, and is not supported application or module API.

The `tests/runner/` directory is different from `tests/internal/`: it is a normal registered validation module with its own focused CTest entry. It uses the internal seam only to test the shared runner implementation.

## Presets

- `development`: startup tests enabled.
- `validation`: standalone tests and benchmark dry-run target available.
- `benchmark`: optimized standalone benchmarks only.
- `shipping`: game only; validation and assertions disabled.
- `coverage`: standalone tests with coverage instrumentation.
- `docs`: Doxygen only.

See @ref project_testing and @ref project_benchmarking for module standards and commands.
