@page project_profiling Profiling with Tracy

GameWIP uses Tracy for interactive profiling of representative runtime sessions.
Use benchmarks for repeatable isolated measurements and Tracy captures to see
where an end-to-end run spends time.

## Prepare the profiler

Setup builds the profiler from the Tracy source locked by the checkout:

```powershell
.\setup.bat profiler
```

```bash
bash ./setup.sh profiler
```

Windows installs the profiler and companion tools under
`<managed-tools-root>/tools/tracy/windows` (default
`build/gamewip/tools/managed/tools/tracy/windows`). It uses UCRT64 GCC and Ninja
with an `x86-64-v3` baseline. Linux installs the native profiler under
`<managed-tools-root>/tools/tracy/linux`, with separate Wayland and X11
variants selected from the active desktop session, plus the same command-line
tools as Windows. Linux uses the system display libraries and loader. Tracy
warns that `libwayland-client` versions below 1.22 have reduced functionality.
Both builds use the locked source and keep build state under
`<storage-root>/cache/tracy`.

## Run a profiling build

The helper builds the selected profile preset, starts the matching host profiler,
and runs the game:

```powershell
.\gamewip.bat run game-profile -BuildBeforeRun
```

```bash
bash ./gamewip.sh run game-profile -BuildBeforeRun
```

Use `game-profile-startup-tests` to profile startup validation. The VS Code
profile shortcuts run these same helper commands. Use a representative scenario;
an empty or artificial run rarely provides useful optimization evidence.

## Build controls

The profile preset enables `GAMEWIP_ENABLE_TRACY=ON` and
`GAMEWIP_ENABLE_STARTUP_TESTS=ON`. Runtime arguments decide whether a capture
covers the game or embedded correctness tests; no cache option needs to change.

Tracy support must remain optional:

- Disabled builds must not require Tracy headers or runtime libraries.
- Public reusable-library APIs must not expose Tracy types.
- Project-owned Tracy instrumentation must compile away when disabled.
- Release builds must not retain project-owned Tracy instrumentation or dependencies.

## Marker rules

Markers should answer a specific performance question.

Useful markers include:

- Meaningful work phases.
- Long-lived worker thread names.
- Opaque blocks that need subdivision in a capture.
- Workload-size plots that explain timing, such as entity, contact, job, queue, or allocation counts.

Markers are usually not useful for:

- Every small function.
- High-cardinality dynamic names.
- Speculative future hotspots.
- Public headers.
- Reusable-library API surfaces.
- Leaf functions that do not explain a measured cost.

Markers that no longer answer a performance question should be removed.

## Logger initialization and shutdown zones

The game executable supplies process-level markers for profiler attachment,
startup validation, startup benchmarks, runtime execution, Logger
initialization, and Logger shutdown. The Game runtime zone encloses
`GameWIP::Game::run()`, while Logger shutdown has its own zone inside
`finishRuntime()`, including the error paths that call it. Profile builds also
emit frame marks at the validation, benchmark, and runtime boundaries. These
markers describe executable composition. Subsystem implementations own any
narrower zones needed to explain measured work.

Colors group executable-owned zones by purpose: blue identifies the enclosing
process and runtime, teal identifies initialization, green identifies frames,
gray identifies waits, orange and purple identify validation and benchmarks,
and red identifies cleanup or failure paths. Keep those meanings when adding a
related zone. An uncolored marker is better when no category applies.

## Optimization workflow

1. Capture a representative Tracy session.
2. Identify a hotspot, stall, or frame spike.
3. Add narrower private zones only when needed to subdivide opaque time.
4. Add or extend a repeatable benchmark for the targeted operation when practical.
5. Optimize and compare benchmark results.
6. Capture another representative Tracy session to verify the end-to-end effect.

Profiling should guide investigation. Benchmarks should verify repeatable
performance changes when the operation can be isolated.

## Library instrumentation policy

Reusable libraries remain profiler-agnostic by default. A library may add
private compile-time zones when a representative capture shows meaningful
opaque work that needs subdivision.

Tracy must not appear in public library APIs, installed public headers, package
usage requirements, or consumer examples.

## Failure behavior

| Symptom | Likely cause | Action |
| --- | --- | --- |
| The profiling build cannot find Tracy. | The locked dependency cache is not ready. | Run `setup.bat deps` on Windows or `bash ./setup.sh deps` on Linux, then reconfigure. |
| The profiler executable is missing or mismatched. | The configured managed-tools root was not prepared for the locked client. | Run `setup.bat profiler` on Windows or `bash ./setup.sh profiler` on Linux. |
| A disabled build still references Tracy. | A Tracy include or symbol leaked outside the enable guard. | Move the include to private implementation code and guard instrumentation. |
| A capture is too noisy. | Markers are too fine-grained or use unstable names. | Collapse markers to meaningful phases and remove high-cardinality names. |
| A profile suggests an optimization but benchmarks do not change. | The benchmark does not represent the profiled workload or the hotspot is end-to-end only. | Adjust the benchmark or keep the evidence as profiling-only. |

## Related pages

- @ref project_build
- @ref project_benchmarking
- @ref project_static_analysis
