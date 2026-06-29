@page project_benchmarking Benchmarking

Google Benchmark owns performance iteration, calibration, timing, repetitions, and statistics. TestSupport remains responsible only for correctness-test reporting and diagnostic elapsed durations.

## Build and run

```powershell
cmake --preset benchmark
cmake --build --preset benchmark
.\build-benchmark\GameWIPBenchmarks.exe
```

Run a focused family and save JSON:

```powershell
.\build-benchmark\GameWIPBenchmarks.exe `
  --benchmark_filter=BM_Logger `
  --benchmark_repetitions=5 `
  --benchmark_out=logger_results.json `
  --benchmark_out_format=json
```

Validate registration without collecting meaningful timings:

```powershell
.\build-benchmark\GameWIPBenchmarks.exe --benchmark_dry_run
```

CI performs only a dry run. Machine-dependent timing values are not merge gates.

## Module standard

Each module lives under `game/validation/benchmarks/<module>` and explicitly registers its sources and dependencies:

```cmake
gamewip_add_benchmark_module(
    NAME logger
    SOURCES logger_benchmark.cpp
    LINK_LIBRARIES Logger
)
```

Benchmark functions use `BM_<Module>_<Scenario>` names. Keep correctness setup checks minimal and use `SkipWithError()` for external setup failures.

## Measurement rules

- Build meaningful benchmark results with the `benchmark` Release preset.
- Let Google Benchmark control the iteration loop.
- Use fixtures or setup/teardown callbacks for resources outside the measured loop.
- Use `DoNotOptimize()` and `ClobberMemory()` only where optimizer removal would invalidate the scenario.
- State whether the scenario represents main-thread CPU, process CPU, or real elapsed time.
- Use `UseRealTime()` for asynchronous user-visible producer latency.
- Flush asynchronous work and record drop/error counters during teardown.
- Do not mix correctness assertions or pass/fail thresholds into benchmark results.
- Keep inputs stable enough for comparisons and record command-line options with saved output.

Assert benchmarks cover passing macro paths. Logger benchmarks cover disabled output, filtered formatted calls, and enabled asynchronous file output while reporting queue counters.

## Current scenarios

| Benchmark | Measured path | Timing |
| --- | --- | --- |
| `BM_Assert_Passing` | Enabled passing `ASSERT`. | Default Google Benchmark CPU time. |
| `BM_Check_Passing` | Passing recoverable `CHECK`. | Default CPU time. |
| `BM_Verify_Passing` | Passing always-evaluated `VERIFY`. | Default CPU time. |
| `BM_AssertInteractive_Passing` | Passing interactive assertion without UI. | Default CPU time. |
| `BM_VerifyInteractive_Passing` | Passing always-evaluated interactive verification. | Default CPU time. |
| `BM_Ensure_Passing` | Passing `ENSURE`, including its observable boolean result. | Default CPU time. |
| `BM_Logger_OutputDisabled` | Logger producer call while output is disabled. | Real elapsed time. |
| `BM_Logger_FilteredFormatted` | Formatted producer call rejected by the severity filter. | Real elapsed time. |
| `BM_Logger_EnabledFile` | Accepted asynchronous file producer call. | Real elapsed time. |

The Logger fixture initializes before measurement and flushes, records counters, shuts down, and removes its temporary workspace after measurement. `queued`, `written`, `queue_drops`, and `peak_queue` counters must be reviewed with timing output: a fast run that dropped work is not equivalent to a run that accepted and wrote it.
