@page project_benchmarking Benchmarking

GameWIP uses Google Benchmark for performance measurement. Google Benchmark owns iteration, calibration, timing, repetitions, and statistics.

## Scope

This page documents benchmark build commands, module structure, measurement rules, current scenarios, output expectations, and review requirements.

Benchmarks are not correctness tests and do not set merge-gating performance thresholds. Correctness behavior must be covered by @ref project_testing before performance coverage is added.

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

CI performs registration dry runs only. Machine-dependent timing values are not merge gates.

## Commands

### Run a focused benchmark family

```powershell
.\build\benchmark\GameWIPBenchmarks.exe --benchmark_filter=BM_Logger
```

Use this while developing or investigating one subsystem.

### Run repetitions and save JSON

```powershell
.\build\benchmark\GameWIPBenchmarks.exe `
  --benchmark_filter=BM_Logger `
  --benchmark_repetitions=5 `
  --benchmark_out=build/benchmark/logger_results.json `
  --benchmark_out_format=json
```

Use JSON output when comparing results or attaching evidence to an issue.

### Dry-run benchmark registration

```powershell
.\build\benchmark\GameWIPBenchmarks.exe --benchmark_dry_run
```

Use this in CI and after adding a module to verify registration and setup without relying on machine timing.

## Module standard

Each benchmark module lives under:

```text
game/validation/benchmarks/<module>/
  CMakeLists.txt
  <module>_benchmark.cpp
```

Register sources and dependencies with:

```cmake
gamewip_add_benchmark_module(
    NAME logger
    SOURCES
        logger_benchmark.cpp
    LINK_LIBRARIES
        Logger
)
```

Benchmark functions should use `BM_<Module>_<Scenario>` names.

## Measurement rules

Benchmarks must:

- Use the `benchmark` Release preset for meaningful measurements.
- Let Google Benchmark control the iteration loop.
- Keep setup and teardown outside the measured loop when possible.
- Use fixtures or setup callbacks for reusable resources.
- Use `DoNotOptimize()` and `ClobberMemory()` only when optimizer removal would invalidate the scenario.
- State whether the scenario represents main-thread CPU, process CPU, or real elapsed time.
- Use `UseRealTime()` for asynchronous user-visible producer latency.
- Flush asynchronous work and record drop or error counters during teardown.
- Avoid correctness assertions based on machine-dependent timing.
- Avoid pass/fail thresholds on elapsed time.
- Keep benchmark inputs stable enough for comparisons.
- Record command-line options with saved benchmark output.

Use `SkipWithError()` for external setup failures that prevent measurement.

## Current scenarios

| Benchmark family | Purpose |
| --- | --- |
| `BM_Assert_*` | Measures passing assertion and check macro paths. |
| `BM_Logger_*` | Measures disabled output, filtered formatted calls, and enabled asynchronous file output. |

Logger benchmarks should report relevant queue, drop, and flush counters so a fast result cannot hide lost work.

## Outputs and artifacts

Google Benchmark console output is suitable for local inspection. Use JSON output for retained evidence or comparison.

Benchmark artifacts should not be written into source directories. Store retained results in an explicit analysis location, issue attachment, or build artifact.

## Failure behavior

| Symptom | Likely cause | Action |
| --- | --- | --- |
| A benchmark is not listed. | The module was not registered or its directory was not discovered. | Check the module `CMakeLists.txt` and parent discovery rules. |
| Dry run fails. | Setup code failed or the benchmark executable could not initialize. | Fix registration or setup before collecting timings. |
| Results are unstable. | The scenario depends on machine load, asynchronous drain timing, or variable input. | Stabilize setup, record counters, use repetitions, and document the limitation. |
| A benchmark passes despite dropped work. | The scenario measures enqueue cost without observing completion or loss. | Flush work and report drop/error counters during teardown. |

## Maintainer notes

When adding benchmark coverage:

- Add correctness tests first.
- Benchmark representative public behavior or a justified internal hot path.
- Keep benchmark code separate from correctness-test modules.
- Prefer stable workload names and parameters.
- Keep setup failures explicit.
- Do not make elapsed time a merge gate without a separate, documented performance policy.

## Related pages

- @ref project_validation
- @ref project_testing
- @ref project_profiling
- @ref project_build
