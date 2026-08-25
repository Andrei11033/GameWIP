@page project_benchmarking Benchmarking

GameWIP uses Google Benchmark for performance measurement. Google Benchmark owns iteration control, calibration, timing, repetitions, statistics, and
benchmark filtering.

Benchmarks are not correctness tests and do not define merge-gating performance thresholds.

This guide covers benchmark registration, runner integration, measurement
rules, result interpretation, the current scenarios, and retained output. It
also marks the boundary between a repeatable measurement and a correctness test.

Correctness behavior must be covered through @ref project_testing before performance coverage is added. Startup ordering is documented in @ref
project_game_executable.

## Common workflow

Build and run the standard optimized benchmark profile:

```powershell
.\gamewip.bat benchmark
```

Validate registration without collecting meaningful timings:

```powershell
.\gamewip.bat benchmark dry-run
```

List registered scenarios without measuring them:

```powershell
.\gamewip.bat benchmark list
```

CI performs registration dry runs only. Machine-dependent timings are not merge gates. Direct executable invocation remains supported when diagnosing
Google Benchmark itself, but the helper is the normal local workflow because it standardizes optimized builds, arguments, retained results, and run
metadata.

Print the complete option set for the pinned Google Benchmark executable without
running a measurement:

```powershell
.\build\benchmark\GameWIPBenchmarks.exe --help
```

That generated help is authoritative for direct third-party runner flags. This
page owns GameWIP's wrapper behavior and supported measurement workflow.

## Runner source API

Use @ref GameWIP::Validation::Benchmarks and @ref GameWIP::Validation::BenchmarkResult for the generated source reference.

`Benchmarks::run(int, char **, bool embedded)` performs one Google Benchmark lifecycle:

1. Build an argv view with owned string storage.
2. Initialize Google Benchmark.
3. Reject unrecognized forwarded arguments.
4. Run selected benchmark registrations.
5. Shut Google Benchmark down.

The runner is intended for one benchmark invocation at a time in a process because Google Benchmark owns process-global registration and runtime
state.

The runner is not an exception boundary. Allocation failures while it copies arguments, and any exception that escapes Google Benchmark initialization
or execution, propagate to the caller. `BenchmarkResult` describes only a normally completed invocation. The current standalone benchmark entry point
and embedded game entry point do not catch such exceptions, so an exception that reaches `main()` follows the language runtime's uncaught-exception
behavior.

### Standalone argument behavior

With `embedded == false`, every original argument is forwarded. Google Benchmark reports and rejects arguments it does not recognize.

### Embedded argument behavior

With `embedded == true`, the runner forwards only:

- `--help`.
- Arguments beginning with `--benchmark_`.
- Arguments beginning with `--v=`.

GameWIP startup, validation, and runtime arguments are not passed to Google Benchmark. The original process arguments remain unchanged for later
executable stages.

### `BenchmarkResult`

| Field | Contract |
| --- | --- |
| `benchmarksRun` | Number returned by `benchmark::RunSpecifiedBenchmarks()`. Zero selected benchmarks is not by itself a runner failure. |
| `argumentsValid` | False only when Google Benchmark reports unrecognized forwarded arguments. |

`ok()` reflects argument validity only. It does not encode performance thresholds, propagated exceptions, or every per-scenario `SkipWithError()`
diagnostic. Inspect Google Benchmark output and retained results for scenario-level setup errors.

The standalone benchmark executable returns failure only when `ok()` is false. Startup benchmarks use the same rule before entering game runtime code.

## Commands

Run a focused family with the standard profile:

```powershell
.\gamewip.bat benchmark -Filter BM_Logger
```

Request explicit repetitions and minimum measurement time:

```powershell
.\gamewip.bat benchmark `
  -Filter BM_Logger `
  -Repetitions 10 `
  -MinTime 1s
```

Use a named profile:

```powershell
.\gamewip.bat benchmark -BenchmarkProfile quick
.\gamewip.bat benchmark -BenchmarkProfile standard
.\gamewip.bat benchmark -BenchmarkProfile stable
```

Save results to an explicit path:

```powershell
.\gamewip.bat benchmark -Filter BM_Logger -Output D:\Results\logger.json
```

Compare two retained JSON results descriptively:

```powershell
.\gamewip.bat benchmark compare `
  -Baseline build\gamewip\runs\<before>\artifacts\benchmark-results.json `
  -Candidate build\gamewip\runs\<after>\artifacts\benchmark-results.json
```

The comparison matches benchmark run names, normalizes time units, prefers Google Benchmark's mean aggregate when present, and reports CPU and
real-time percentage changes. It is descriptive evidence, not a statistical significance test or performance gate.

### Helper options

| Option | Behavior |
| --- | --- |
| `benchmark run` | Configure, build, measure, and retain results. This is the default. |
| `benchmark dry-run` | Validate selected registrations without useful timings. |
| `benchmark list` | Print selected registered benchmark names. |
| `benchmark compare` | Compare two retained JSON files supplied with `-Baseline` and `-Candidate`. |
| `-BenchmarkProfile quick` | One short development measurement per scenario. |
| `-BenchmarkProfile standard` | Five repetitions with aggregate reporting; this is the default profile. |
| `-BenchmarkProfile stable` | Ten longer, randomly interleaved repetitions for careful local comparison. |
| `-Filter <regex>` | Forward a Google Benchmark name filter. |
| `-Repetitions <count>` | Override the selected profile's repetition count. |
| `-MinTime <time>` | Override minimum measurement time, such as `0.5s`, `2s`, or `100x`. |
| `-AggregatesOnly` | Retain and display aggregate rows only. |
| `-Output <path>` | Override the default retained result or comparison path. |
| `-OutputFormat json\|csv` | Select retained measurement format; JSON is the default and is required for helper comparison. |
| `-NoBuild` | Use an existing benchmark executable and fail clearly when it is missing. |
| `-ExtraArgs <arguments>` | Forward advanced Google Benchmark arguments not owned by a dedicated helper option. |

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

Use stable `BM_<Module>_<Scenario>` names. The parent directory discovers immediate module directories containing `CMakeLists.txt`; each module still
lists sources and dependencies explicitly.

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
| `BM_IO_*` | Fixed-size memory reads, pre-reserved memory writes, and known-size whole-read allocation at 4 KiB and 1 MiB. |
| `BM_Terminal_*` | Output-buffer formatting, segmented writes, and terminal-facing hot paths without interactive correctness policy. |
| `BM_Unicode_*` | Strict UTF-8 decode, validation, scalar encoding, code-point traversal, and extended grapheme traversal on representative ASCII and non-ASCII text. |

Logger scenarios report queue, drop, flush, or error counters where necessary so a fast producer result cannot hide lost work.

FileSystem fixtures are created below the operating-system temporary directory and reused across scenarios. Set the host temporary-directory
environment to a representative local or network volume before launching the benchmark when comparing storage backends.

## Outputs and artifacts

Each helper invocation creates one action-named directory:

```text
build/gamewip/runs/<timestamp>_benchmark-run/
  summary.txt
  summary.json
  manifest.json
  logs/
  artifacts/
    benchmark-results.json
```

Console output is streamed live and retained in the step log. The manifest records the selected profile, effective options, commands, timings, exit
codes, and output paths. JSON is the default measurement artifact because it supports later comparison and issue attachments.

An explicit `-Output` may point outside the checkout. Inside the checkout it must remain under `build/`; benchmark output must not be written into
source directories.

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
