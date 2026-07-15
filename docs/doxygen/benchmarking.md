@page project_benchmarking Benchmarking

GameWIP uses Google Benchmark for performance measurement. Google Benchmark owns iteration control, calibration, timing, repetitions, statistics, and benchmark filtering.

Benchmarks are not correctness tests and do not define merge-gating performance thresholds.

## Scope

This page owns benchmark-runner integration, module structure, measurement rules, result interpretation, current scenarios, and retained output policy.

Correctness behavior must be covered through @ref project_testing before performance coverage is added. Startup ordering is documented in @ref project_game_executable.

## Common workflow

Build and run optimized benchmarks:

```powershell
cmake --preset benchmark
cmake --build --preset benchmark
.\build\benchmark\GameWIPBenchmarks.exe
```

Validate registration without collecting meaningful timings:

```powershell
.\build\benchmark\GameWIPBenchmarks.exe --benchmark_dry_run
```

CI performs registration dry runs only. Machine-dependent timings are not merge gates.

## Runner source API

Use @ref GameWIP::Validation::Benchmarks and @ref GameWIP::Validation::BenchmarkResult for the generated source reference.

`Benchmarks::run(int, char **, bool embedded)` performs one Google Benchmark lifecycle:

1. Build an argv view with owned string storage.
2. Initialize Google Benchmark.
3. Reject unrecognized forwarded arguments.
4. Run selected benchmark registrations.
5. Shut Google Benchmark down.

The runner is intended for one benchmark invocation at a time in a process because Google Benchmark owns process-global registration and runtime state.

The runner is not an exception boundary. Allocation failures while it copies arguments, and any exception that escapes Google Benchmark initialization or execution, propagate to the caller. `BenchmarkResult` describes only a normally completed invocation. The current standalone benchmark entry point and embedded game entry point do not catch such exceptions, so an exception that reaches `main()` follows the language runtime's uncaught-exception behavior.

### Standalone argument behavior

With `embedded == false`, every original argument is forwarded. Google Benchmark reports and rejects arguments it does not recognize.

### Embedded argument behavior

With `embedded == true`, the runner forwards only:

- `--help`.
- Arguments beginning with `--benchmark_`.
- Arguments beginning with `--v=`.

GameWIP startup, validation, and runtime arguments are not passed to Google Benchmark. The original process arguments remain unchanged for later executable stages.

### `BenchmarkResult`

| Field | Contract |
| --- | --- |
| `benchmarksRun` | Number returned by `benchmark::RunSpecifiedBenchmarks()`. Zero selected benchmarks is not by itself a runner failure. |
| `argumentsValid` | False only when Google Benchmark reports unrecognized forwarded arguments. |

`ok()` reflects argument validity only. It does not encode performance thresholds, propagated exceptions, or every per-scenario `SkipWithError()` diagnostic. Inspect Google Benchmark output and retained results for scenario-level setup errors.

The standalone benchmark executable returns failure only when `ok()` is false. Startup benchmarks use the same rule before entering game runtime code.

## Commands

Run a focused family:

```powershell
.\build\benchmark\GameWIPBenchmarks.exe --benchmark_filter=BM_Logger
```

Run repetitions and save JSON:

```powershell
.\build\benchmark\GameWIPBenchmarks.exe `
  --benchmark_filter=BM_Logger `
  --benchmark_repetitions=5 `
  --benchmark_out=build/benchmark/logger_results.json `
  --benchmark_out_format=json
```

Dry-run registration:

```powershell
.\build\benchmark\GameWIPBenchmarks.exe --benchmark_dry_run
```

## Module standard

Each benchmark module owns:

```text
game/validation/benchmarks/<module>/
  CMakeLists.txt
  <module>_benchmark.cpp
```

Register it with:

```cmake
gamewip_add_benchmark_module(
    NAME logger
    SOURCES
        logger_benchmark.cpp
    LINK_LIBRARIES
        Logger
)
```

Use stable `BM_<Module>_<Scenario>` names. The parent directory discovers immediate module directories containing `CMakeLists.txt`; each module still lists sources and dependencies explicitly.

## Measurement rules

Benchmarks must:

- Use the optimized benchmark preset for meaningful measurements.
- Let Google Benchmark control iteration and timing.
- Keep setup and teardown outside measured loops when possible.
- State whether the scenario measures CPU time or real elapsed time.
- Use `UseRealTime()` for asynchronous user-visible producer latency.
- Use `DoNotOptimize()` and `ClobberMemory()` only when optimizer removal would invalidate the scenario.
- Flush asynchronous work and report drop/error counters during teardown.
- Keep workloads and parameters stable enough for comparison.
- Avoid correctness assertions based on machine-dependent timing.
- Use `SkipWithError()` when setup cannot produce a meaningful measurement.
- Record command-line options with retained results.

## Current scenarios

| Benchmark family | Purpose |
| --- | --- |
| `BM_Assert_*` | Passing assertion and check macro paths. |
| `BM_Logger_*` | Disabled output, filtered formatting, enabled asynchronous output, registered-`SourceId`, and 2/4/8-thread producer contention paths. |
| `BM_FileSystem_*` | Materialized and streaming directory enumeration at 1K/10K entries and path depths 1/8/32. |

Logger scenarios report queue, drop, flush, or error counters where necessary so a fast producer result cannot hide lost work.

FileSystem fixtures are created below the operating-system temporary directory and reused across scenarios. Set the host temporary-directory environment to a representative local or network volume before launching the benchmark when comparing storage backends.

## Outputs and artifacts

Console output is suitable for local inspection. Use JSON for retained evidence, comparison, or issue attachments.

Benchmark output must not be written into source directories. Store retained data in an explicit analysis path or build artifact.

## Failure behavior

| Symptom | Likely cause | Action |
| --- | --- | --- |
| Benchmark executable exits before running. | A forwarded argument was not recognized. | Remove the argument or use a Google Benchmark-owned spelling. |
| Benchmark process terminates after an unexpected exception. | Argument storage allocation or benchmark/framework code threw past the runner. | Diagnose the exception at the owning allocation, benchmark, or framework boundary; `BenchmarkResult` is not produced. |
| A benchmark is absent. | Its module was not discovered, registered, linked, or selected by the filter. | Check module CMake and the active filter. |
| A scenario reports `SkipWithError()`. | Setup could not create a meaningful measurement. | Fix the setup failure before comparing timings. |
| Results are unstable. | Machine load, asynchronous drain, or input varies. | Stabilize the workload, use repetitions, and record limitations. |
| A producer benchmark is fast while work is dropped. | Completion or loss was not observed. | Drain asynchronous work and expose drop/error counters. |

## Maintainer notes

When adding benchmark coverage:

- Add correctness coverage first.
- Measure representative public behavior or a justified internal hot path.
- Keep benchmark code separate from correctness modules.
- Keep setup failures explicit.
- Do not introduce elapsed-time merge gates without a separate reviewed policy.

## Related pages

- @ref project_validation
- @ref project_testing
- @ref project_profiling
- @ref project_build
- @ref project_documentation
