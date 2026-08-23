@page project_profiling Profiling with Tracy

GameWIP uses Tracy for interactive profiling of representative runtime
sessions. Use benchmarks for repeatable isolated measurements; use a Tracy
capture to explain where an end-to-end run spends time.

## Common workflow

Rebuild the Windows tools from the Tracy client pinned by the checkout:

```powershell
.\setup.bat profiler
```

The focused command produces `tracy-profiler.exe`, `tracy-capture.exe`,
`tracy-csvexport.exe`, `tracy-import-chrome.exe`,
`tracy-import-fuchsia.exe`, and `tracy-update.exe` under `C:\MSYS2\GameWIPTools\tools\tracy`. It uses
UCRT64 GCC/Ninja Release builds with a reproducible `x86-64-v3` baseline,
stages the complete set and required UCRT DLLs, then replaces the existing tools
only after verification. Visual Studio is not required. First use may take
several minutes because the GUI profiler compiles substantial pinned
dependencies.

Build the profiling preset:

```powershell
cmake --preset profile
cmake --build --preset profile
```

Start the matching profiler, then run the profiling build:

```powershell
Start-Process 'C:\MSYS2\GameWIPTools\tools\tracy\tracy-profiler.exe'
.\build\profile\GameWIP.exe
```

Use a representative scenario. A capture of an empty or artificial run is rarely useful evidence for optimization decisions.

The VS Code `F9` workflow performs the configure, build, profiler
launch, and game launch sequence. Pass `--startup-tests` when validation itself
is the workload being profiled:

```powershell
cmake --preset profile
cmake --build --preset profile
.\build\profile\GameWIP.exe --startup-tests
```

## Build controls

The profile preset enables `GAMEWIP_ENABLE_TRACY=ON` and `GAMEWIP_ENABLE_STARTUP_TESTS=ON`. Runtime arguments decide whether a capture covers the game
or embedded correctness tests; no cache option needs to be changed.

Tracy support must remain optional:

- Disabled builds must not require Tracy headers or runtime libraries.
- Public reusable-library APIs must not expose Tracy types.
- Project-owned Tracy instrumentation must compile away when disabled.
- Release builds must not retain project-owned Tracy instrumentation or dependencies.

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

The game executable supplies process-level markers for profiler attachment,
startup validation, startup benchmarks, runtime execution, Logger
initialization, and Logger shutdown. Profile builds also emit frame marks at
the validation, benchmark, and runtime boundaries. These markers describe the
executable composition path; subsystem implementations own any narrower zones
needed to explain measured work.

Colors group executable-owned zones by purpose: blue identifies the enclosing
process and runtime, teal identifies initialization, green identifies frames,
gray identifies waits, orange and purple identify validation and benchmarks,
and red identifies cleanup or failure paths. Preserve these meanings when
adding a related zone; prefer an uncolored marker when no category applies.

## Optimization workflow

1. Capture a representative Tracy session.
2. Identify a hotspot, stall, or frame spike.
3. Add narrower private zones only when needed to subdivide opaque time.
4. Add or extend a repeatable benchmark for the targeted operation when practical.
5. Optimize and compare benchmark results.
6. Capture another representative Tracy session to verify the end-to-end effect.

Profiling should guide investigation. Benchmarks should verify repeatable performance changes when the operation can be isolated.

## Library instrumentation policy

Reusable libraries remain profiler-agnostic by default. A library may add private compile-time zones only when a representative capture shows
meaningful opaque work that needs subdivision.

Tracy must not appear in public library APIs, installed public headers, package usage requirements, or consumer examples.

## Failure behavior

| Symptom | Likely cause | Action |
| --- | --- | --- |
| The profiling build cannot find Tracy. | Submodules or external dependencies are not initialized. | Run `git submodule update --init --recursive` and reconfigure. |
| The profiler executable is missing or mismatched. | `C:\MSYS2\GameWIPTools\tools\tracy` was not prepared for the pinned client. | Run `setup.bat profiler`; existing tools remain intact if rebuilding fails. |
| A disabled build still references Tracy. | A Tracy include or symbol leaked outside the enable guard. | Move the include to private implementation code and guard instrumentation. |
| A capture is too noisy. | Markers are too fine-grained or use unstable names. | Collapse markers to meaningful phases and remove high-cardinality names. |
| A profile suggests an optimization but benchmarks do not change. | The benchmark does not represent the profiled workload or the hotspot is end-to-end only. | Adjust the benchmark or keep the evidence as profiling-only. |

## Related pages

- @ref project_build
- @ref project_benchmarking
- @ref project_static_analysis
