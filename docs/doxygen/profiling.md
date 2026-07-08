@page project_profiling Profiling

GameWIP uses Tracy for contextual runtime profiling and Google Benchmark for repeatable isolated measurements. Tracy identifies where time is spent in a representative run; benchmarks validate whether a targeted change improves a stable scenario.

## Ownership and dependency rules

The `GameWIP` executable owns Tracy enablement, the profiler client, and process-level markers. Reusable libraries remain profiler-agnostic by default and must not expose Tracy types, headers, macros, or package requirements through their public APIs.

A library may add private profiling zones when a real capture shows a meaningful opaque cost that needs subdivision. Such instrumentation must remain an implementation detail, use the shared project enablement policy, and compile away when Tracy is disabled. Libraries do not initialize or manage an independent profiler client.

Correctness tests use TestSupport section timing. Benchmarks use Google Benchmark. Tracy markers do not replace either mechanism.

## Disabled-build contract

`GAMEWIP_ENABLE_TRACY=OFF` means the game executable does not link Tracy, the client is excluded from the default build, and project-owned markers are removed at preprocessing time. Disabled builds must not retain Tracy runtime branches, threads, networking, allocations, imports, or symbols. The `profiling` preset is the supported Tracy-enabled build; shipping and other non-profiling presets keep it disabled.

## Initial process zones

The profiling build records from process start so startup work is not lost while a profiler connects. Start the capture or Tracy UI before launching the game. The initial integration names the main thread `GameWIP Main` and records only the major process phases:

- `GameWIP process`
- `Startup validation`
- `Startup benchmarks`
- `Game runtime`

The game has no frame loop yet, so it does not emit artificial frame marks. A future loop emits exactly one `FrameMark` for each completed real frame.

## Marker rules

- Mark meaningful work phases rather than every function.
- Use stable string-literal names and avoid high-cardinality dynamic zone names.
- Name each long-lived worker thread once when it starts.
- Add nested zones only when a capture needs more detail to explain a measured cost.
- Add plots when workload size explains timing, such as entity, contact, job, queue, or allocation counts.
- Instrument locks, allocations, or leaf functions only for a specific investigation.
- Remove markers that no longer answer a performance question.
- Keep every marker and Tracy include out of public headers.

Likely future candidates include physics broad phase, narrow phase, contact generation, solver phases, rendering passes, streaming, and job scheduling. Their instrumentation should be added with the systems and representative workloads, not speculatively.

## Optimization workflow

1. Capture a representative Tracy session.
2. Identify a hotspot, stall, or frame spike.
3. Add narrower private zones only when needed to subdivide opaque time.
4. Add or extend a repeatable benchmark for the targeted operation.
5. Optimize and compare benchmark results.
6. Capture another representative Tracy session to verify the end-to-end effect.
