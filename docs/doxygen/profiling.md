@page project_profiling Profiling with Tracy

GameWIP uses Tracy for interactive profiling of representative runtime sessions. Tracy answers where time is spent in a captured run. Benchmarks answer whether a repeatable operation changed in a controlled measurement.

## Scope

This page documents the Tracy build preset, capture workflow, marker policy, disabled-build contract, and the relationship between profiling and benchmarks.

## Common workflow

Build the profiling preset:

```powershell
cmake --preset profiling
cmake --build --preset profiling
```

Start the Tracy profiler, then run the profiling build:

```powershell
.\build\profiling\GameWIP.exe
```

Use a representative scenario. A capture of an empty or artificial run is rarely useful evidence for optimization decisions.

The `profiling` preset disables startup tests and startup benchmarks so the default capture begins with the runtime workload. Use `profiling-validation` when the validation startup path is itself the workload being profiled:

```powershell
cmake --preset profiling-validation
cmake --build --preset profiling-validation
.\build\profiling-validation\GameWIP.exe
```

## Build controls

Both profiling presets enable `GAMEWIP_ENABLE_TRACY=ON` and inherit the development build configuration. `profiling-validation` differs only by enabling startup correctness tests.

Tracy support must remain optional:

- Disabled builds must not require Tracy headers or runtime libraries.
- Public reusable-library APIs must not expose Tracy types.
- Project-owned Tracy instrumentation must compile away when disabled.
- Shipping builds must not retain project-owned Tracy instrumentation or dependencies.

## Marker rules

Markers should answer a specific performance question.

Use markers for:

- Meaningful work phases.
- Long-lived worker thread names.
- Opaque blocks that need subdivision in a capture.
- Workload-size plots that explain timing, such as entity, contact, job, queue, or allocation counts.

Avoid markers for:

- Every small function.
- High-cardinality dynamic names.
- Speculative future hotspots.
- Public headers.
- Reusable-library API surfaces.
- Leaf functions that do not explain a measured cost.

Remove markers that no longer answer a performance question.

## Optimization workflow

1. Capture a representative Tracy session.
2. Identify a hotspot, stall, or frame spike.
3. Add narrower private zones only when needed to subdivide opaque time.
4. Add or extend a repeatable benchmark for the targeted operation when practical.
5. Optimize and compare benchmark results.
6. Capture another representative Tracy session to verify the end-to-end effect.

Profiling should guide investigation. Benchmarks should verify repeatable performance changes when the operation can be isolated.

## Library instrumentation policy

Reusable libraries remain profiler-agnostic by default. A library may add private compile-time zones only when a representative capture shows meaningful opaque work that needs subdivision.

Tracy must not appear in public library APIs, installed public headers, package usage requirements, or consumer examples.

## Future marker candidates

Likely future candidates include:

- Physics broad phase.
- Physics narrow phase.
- Contact generation.
- Solver phases.
- Rendering passes.
- Asset or world streaming.
- Job scheduling.
- Queue processing.

Add these markers with the systems and representative workloads, not speculatively.

## Failure behavior

| Symptom | Likely cause | Action |
| --- | --- | --- |
| The profiling build cannot find Tracy. | Submodules or external dependencies are not initialized. | Run `git submodule update --init --recursive` and reconfigure. |
| A disabled build still references Tracy. | A Tracy include or symbol leaked outside the enable guard. | Move the include to private implementation code and guard instrumentation. |
| A capture is too noisy. | Markers are too fine-grained or use unstable names. | Collapse markers to meaningful phases and remove high-cardinality names. |
| A profile suggests an optimization but benchmarks do not change. | The benchmark does not represent the profiled workload or the hotspot is end-to-end only. | Adjust the benchmark or keep the evidence as profiling-only. |

## Related pages

- @ref project_build
- @ref project_benchmarking
- @ref project_static_analysis
