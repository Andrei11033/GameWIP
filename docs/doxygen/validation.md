@page project_validation Validation architecture

GameWIP uses the same modular validation sources in two launch modes:

- standalone `GameWIPTests` and `GameWIPBenchmarks` executables for CI and focused local work;
- optional startup validation compiled into `GameWIP` for the normal development workflow.

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
    runner.cpp
    registry.cpp
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

## Presets

- `development`: startup tests enabled.
- `validation`: standalone tests and benchmark dry-run target available.
- `benchmark`: optimized standalone benchmarks only.
- `shipping`: game only; validation and assertions disabled.
- `coverage`: standalone tests with coverage instrumentation.
- `docs`: Doxygen only.

See @ref library_testing and @ref project_benchmarking for module standards and commands.
