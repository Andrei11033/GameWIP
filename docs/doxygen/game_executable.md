@page project_game_executable Game executable

The `game/` tree is the small composition layer that turns the reusable
libraries into runnable programs. It connects generated build identity,
optional startup validation, benchmark startup, and the game runtime in one
process without moving reusable behavior into the executable.

This page follows command-line parsing, startup, runtime initialization,
shutdown, and exit-code selection. Correctness-runner behavior is in @ref
project_validation, test authoring in @ref project_testing, and benchmark
behavior in @ref project_benchmarking.

## Scope

Use this page when changing:

- `game/main.cpp` startup sequencing.
- The executable-owned runtime facade in `game/runtime/`.
- Generated version metadata and `--version` output.
- The compile-time facade in `game/validation/validation.h`.
- The boundary between executable integration code and reusable libraries.

## Source layout

| Path | Purpose |
| --- | --- |
| `game/main.cpp` | Process entry point and startup sequencing. |
| `game/runtime/game.h` | Source-tree runtime facade called after startup work succeeds. |
| `game/runtime/game.cpp` | Runtime facade implementation. |
| `game/runtime/version.h.in` | Template for generated build and release identity. |
| `game/validation/validation.h` | Compile-time facade that keeps `main.cpp` stable when startup validation is disabled. |
| `game/validation/types.h` | Result types shared by embedded and standalone validation runners. |
| `game/validation/tests/` | Correctness runner, module registry, standalone executable, and modules. |
| `game/validation/benchmarks/` | Google Benchmark runner, standalone executable, and benchmark modules. |
| `game/validation/public_headers/` | Compile-only public-header self-containment checks. |
| `game/validation/installed_consumer/` | Clean installed-package consumer check. |

## Command-line interface

Use the utility forms to inspect the executable without initializing validation,
Logger, Desktop, or runtime services:

```powershell
.\build\dev\GameWIP.exe --help
.\build\dev\GameWIP.exe --version
```

| Argument | Availability | Behavior |
| --- | --- | --- |
| No arguments | Every game build | Enters the runtime and opens the game window. |
| `--help`, `-h`, or `-?` | Every game build, as the only argument | Prints usage and build-dependent startup-validation availability, then exits successfully. |
| `--version` | Every game build, as the only argument | Prints `GameWIP::Version::productDisplay`, then exits successfully. |
| `--startup-tests` | `dev`, `profile`, or a custom startup-test build | Runs embedded correctness validation before runtime. Public `GameWIPTests.exe` options may accompany it. |
| `--benchmark_*` or `--v=<level>` | A custom build with startup benchmarks | Forwards Google Benchmark options to the embedded benchmark runner. |

Validation options without `--startup-tests` do not request an ordinary embedded
test run. The runtime facade does not interpret remaining arguments. A custom
startup-benchmark build may reject arguments forwarded to Google Benchmark.
Use `GameWIPTests.exe --help` and `GameWIPBenchmarks.exe --help` for their
complete current option sets, or start at @ref project_command_line_tools.

Embedded validation retains relative reports under `logs/validation` beside the game
executable and scopes disposable validation or benchmark fixtures to its preset's
`temp` directory. Those temporary environment changes are restored before the
ordinary game runtime begins.

## Runtime sequence

`main.cpp` follows one process-level sequence:

1. In Tracy-enabled builds, name the main thread and open the process zone.
2. Handle an exact utility-only help or version request and exit successfully.
3. Run startup correctness validation when it was compiled into the executable
   and the arguments request it.
4. Return the validation child-route exit code immediately when
   `handledChildInvocation` is true.
5. Stop startup when correctness validation fails.
6. Run startup benchmarks when they were compiled into the executable.
7. Stop startup when Google Benchmark rejects its arguments.
8. Enter `GameWIP::Game::run()`, which initializes runtime services, opens the
   game window, and runs the event loop.
9. Return the runtime facade's exit code.

Utility-only help and version queries intentionally bypass validation and runtime startup. A help or `--version` token combined with other arguments
is not treated as the utility-only form.

Tracy-enabled builds emit frame marks before startup validation, startup
benchmarks, and runtime execution. The enclosing named zones identify those
process phases in a capture. Zone colors distinguish process/runtime work,
initialization, frames, waits, validation, benchmarks, and cleanup or failure
paths.

There is no process-wide exception boundary around startup validation, benchmark execution, or `GameWIP::Game::run()`. The correctness runner converts
exceptions escaping module callbacks, but its outer setup/allocation work and the benchmark runner may still propagate. Runtime code should express
expected startup or shutdown failures through its returned exit code. Any exception that reaches `main()` follows the language runtime's
uncaught-exception behavior.

## Runtime facade

`GameWIP::Game::run(int, char **)` is the executable-owned transition from startup wiring into runtime composition.

- `argc` and `argv` are the original process arguments and are borrowed for the call.
- The returned integer becomes the executable's process exit code.
- Reusable behavior must remain in its owning foundation or tools library rather than accumulating behind this facade.

`run()` currently performs this runtime sequence:

1. Initialize Logger console output at Debug severity.
2. Enumerate connected displays and query each display's active mode, supported
   modes, and HDR/color information.
3. Open a visible, focused borderless-fullscreen window on the default display.
   Borderless fullscreen uses the desktop resolution and does not change the
   system display mode.
4. Emit one startup report containing the display inventory, available modes,
   color capabilities, and active-window state.
5. Wait for and pump window events at intervals of up to 16 milliseconds until
   the window receives a close request. On Windows, `Alt+F4` is the expected
   manual exit path.
6. Close the window, report shutdown, and shut Logger down.

The final Window close releases current-thread display-color resources before `GameWIP::Game::run()` returns. Process-level Desktop regression
children verify that standalone color discovery, `WM_CLOSE`, and owner-thread cleanup do not replace the intended successful process exit code.

Failure to enumerate displays, open or close the window, or pump events is
logged and returns `EXIT_FAILURE`. A failed HDR/color query is included in the
startup report for that display but does not prevent the window from opening.
The facade currently ignores `argc` and `argv`; it neither retains nor
interprets them.

Tracy-enabled builds identify display discovery, window open and close, the
event wait/pump, individual game frames, and Logger lifetime with named zones
and messages. A frame mark is emitted after each successful event-pump cycle.

Use @ref GameWIP::Game for the generated source API reference.

## Version metadata

CMake configures `game/runtime/version.h.in` into `gamewip/version.h` during project configuration and refreshes it before building the game
executable.

@ref GameWIP::Version exposes:

| Name | Meaning |
| --- | --- |
| `number` | Numeric project version. |
| `display` | Human-readable release or development version. |
| `productDisplay` | Complete product line printed by `--version`. |
| `buildNumber` | Repository-derived build count or configured fallback. |
| `gitCommit` | Generation-time abbreviated commit or fallback. |
| `dirty` | Whether tracked source changes were detected when identity was generated. |
| `release` | Whether generation observed the expected clean annotated release tag. |

Configuration creates the initial values, and the game version-header target refreshes them whenever the executable is built. A long-lived build tree
therefore observes newer repository state on rebuild without requiring reconfiguration. Release interpretation is owned by @ref project_versioning.

## Startup validation facade

`game/validation/validation.h` is included unconditionally by `main.cpp`.

- `GAMEWIP_STARTUP_TESTS_ENABLED` controls whether `runTests()` forwards to the correctness runner.
- `GAMEWIP_STARTUP_BENCHMARKS_ENABLED` controls whether `runBenchmarks()` forwards to the benchmark runner.
- Each macro defaults to `0` when the including target does not define it.
- Disabled functions return successful empty results and do not retain runner dependencies.

These definitions are target-private composition controls, not installed configuration API. Use @ref GameWIP::Validation for the generated result and
facade reference.

## Source API boundary

The generated manual includes the explicitly registered headers that connect `game/` components. They are documented source-tree interfaces for
contributors and validation modules; they are not installed package APIs or long-term binary compatibility promises.

The documented namespaces are:

- @ref GameWIP::Game for the runtime facade.
- @ref GameWIP::Version for generated process identity.
- @ref GameWIP::Validation for shared validation results and startup facade.
- @ref GameWIP::Validation::Tests for correctness-runner and module-registration contracts.
- @ref GameWIP::Validation::Benchmarks for benchmark-runner integration.
- @ref GameWIP::Test for source-tree library self-test entry points and option types.

Private helpers, module-local functions, and approved internal test seams remain implementation or maintainer interfaces and are not presented as
installed consumer API.

## Dependency boundary

`game/` may compose reusable libraries, including Logger and Desktop, validation
objects, Google Benchmark, Tracy instrumentation, and generated project
metadata. It must not become the implementation owner for behavior that belongs
to an existing library. The runtime consumes Desktop's public display,
renderer/color, window-lifetime, and event-pump interfaces; platform-specific
display and window mechanics remain owned by Desktop.

Move code into a reusable library when it becomes general-purpose, independently testable, and useful outside the executable. Long-term engine systems
belong under `engine/` when that layer owns them.

## Source comments

Every important `.h`, `.cpp`, and generated-header template under `game/` starts with `@file` and `@brief` documentation.

Source comments should explain:

- Ownership and integration boundaries.
- Startup and shutdown sequencing.
- Compile-time disabled behavior.
- Registration lifetime and module adaptation.
- Child-process routing protocols.
- Process-global or generated state.
- Non-obvious validation and benchmark framework requirements.

Do not narrate simple assignments, forwarding calls, obvious test expectations, or compile-only includes beyond their file-level purpose.

## Review checklist

When changing executable integration:

- Keep `main.cpp` small and sequencing-focused.
- Preserve utility-only version behavior.
- Preserve utility-only help behavior and keep its build-availability text current.
- Keep disabled validation paths dependency-free.
- Return child-route results before benchmarks and runtime code.
- Keep expected runtime failures representable as process exit codes.
- Update @ref project_validation for runner or command-line changes.
- Update @ref project_testing for test-module contract changes.
- Update @ref project_benchmarking for benchmark-runner changes.
- Update @ref project_versioning for generated identity changes.
- Update registered source API comments when contracts change.

## Related pages

- @ref project_structure
- @ref project_build
- @ref project_command_line_tools
- @ref project_validation
- @ref project_testing
- @ref project_benchmarking
- @ref project_versioning
- @ref project_documentation
