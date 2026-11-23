@page project_game_executable Game executable

The `game/` tree is the small composition layer that turns reusable libraries
into runnable programs. It connects generated build identity, optional startup
validation, benchmark startup, and the game runtime without moving reusable
behavior into the executable.

This page follows command-line parsing, startup, runtime initialization,
shutdown, and exit-code selection. Correctness-runner behavior is in @ref
project_validation, test authoring in @ref project_testing, and benchmark
behavior in @ref project_benchmarking.

## Scope

This page covers `game/main.cpp` startup sequencing, the executable-owned
runtime facade in `game/runtime/`, generated version metadata and `--version`
output, the compile-time validation facade, and the boundary between executable
integration code and reusable libraries.

## Source layout

| Path | Purpose |
| --- | --- |
| `game/main.cpp` | Process entry point and startup sequencing. |
| `game/runtime/game.h` | Source-tree runtime facade called after startup work succeeds. |
| `game/runtime/game.cpp` | Session sequencing and resource composition. |
| `game/runtime/CMakeLists.txt` | Explicit source registration for runtime implementation files. |
| `game/runtime/diagnostics/display_report.h` | Startup display inspection entry point. |
| `game/runtime/diagnostics/display_report.cpp` | Display, mode, and HDR/color report formatting and logging. |
| `game/runtime/diagnostics/runtime_diagnostics.h` | Internal runtime status, input-device, and window-event reporting helpers. |
| `game/runtime/diagnostics/runtime_diagnostics.cpp` | Structured window and operation-status diagnostics. |
| `game/runtime/diagnostics/input_devices.cpp` | Input device inventory at Debug and grouped descriptors at Trace. |
| `game/runtime/diagnostics/input_control_events.h` | Internal entry points for opt-in control logging. |
| `game/runtime/diagnostics/input_control_events.cpp` | Compact control changes at Debug and detailed events at Trace. |
| `game/runtime/diagnostics/input_reports.h` | Shared entry point for bounded raw report diagnostics. |
| `game/runtime/diagnostics/input_reports.cpp` | Bounded per-device report previews and platform-detail dispatch at Trace. |
| `game/runtime/diagnostics/input_event_format.h` | Shared names and value-formatting declarations for Input diagnostics. |
| `game/runtime/diagnostics/input_event_format.cpp` | Device types, standard controls, and typed value formatting. |
| `game/runtime/diagnostics/input_event_labels.cpp` | Stable event, source, and metadata labels. |
| `game/runtime/diagnostics/input_events.cpp` | Device changes, event dispatch, and incomplete-interval warnings. |
| `game/runtime/lifecycle/input_lifecycle.cpp` | Input health, source detachment, and session cleanup. |
| `game/runtime/lifecycle/logger_lifecycle.h` | Internal Logger lifecycle operations for the runtime. |
| `game/runtime/lifecycle/logger_lifecycle.cpp` | Logger setup, initialization failure detail, session summary, and shutdown. |
| `game/runtime/runtime_input.h` | Selects the runtime Input session for the active platform. |
| `game/runtime/platform/input_reports.h` | Internal contract for native report details. |
| `game/runtime/platform/win32/runtime_input.cpp` | Win32 Input attachment and selected-backend handling. |
| `game/runtime/platform/win32/input_reports.cpp` | Win32 keyboard and mouse report decoding. |
| `game/runtime/options/runtime_options.h` | Environment-backed settings used by runtime startup. |
| `game/runtime/options/runtime_options.cpp` | Runtime option parsing and fallback handling. |
| `game/runtime/window/window_loop.h` | Event-pump summary and runtime loop interface. |
| `game/runtime/window/window_loop.cpp` | Window event draining, Input-frame capture, pump failures, and aggregate loop telemetry. |
| `game/runtime/logging.h` | Typed runtime log areas and their compile-time names and colors. |
| `game/runtime/profiling.h` | Tracy hooks and zone colors; no-ops when Tracy is disabled. |
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
.\build\windows-x64\dev\GameWIP.exe --help
.\build\windows-x64\dev\GameWIP.exe --version
```

These paths use the helper's default Windows x64 target. Direct CMake builds use
`build/dev`; the helper uses `build/<target>/dev`.

| Argument | Availability | Behavior |
| --- | --- | --- |
| No arguments | Every game build | Enters the runtime and opens the game window. |
| `--help`, `-h`, or `-?` | Every game build, as the only argument | Prints usage and build-dependent startup-validation availability, then exits successfully. |
| `--version` | Every game build, as the only argument | Prints `GameWIP::Version::productDisplay`, then exits successfully. |
| `--logLevel=<level>` | Every game build | Sets the runtime log level to `trace`, `debug`, `info`, `warn`, `error`, or `fatal`. Dev and profile default to Debug; Release and MinSizeRel default to Info. The option overrides `GAMEWIP_LOG_LEVEL`. |
| `--inputDebug` | Every game build | Logs live keyboard, button, axis, mouse-delta, and pointer-position changes at Debug; Trace adds event details. |
| `--inputDevices` | Every game build | Adds bounded grouped device descriptors at Trace. |
| `--inputBackends=<list>` | Windows | Selects comma-separated `rawInput`, `hid`, `xinput`, `gameInput`, `directInput`, or `windowMessages` providers; `none` disables them all. Without it, all available providers are tried. |
| `--windowEvents` | Every game build | Logs each Window event with its details at Debug and Trace. |
| `--startup-tests` | `dev`, `profile`, or a custom startup-test build | Runs embedded correctness validation before runtime. Public `GameWIPTests.exe` options may accompany it. |
| `--benchmark_*` or `--v=<level>` | A custom build with startup benchmarks | Forwards Google Benchmark options to the embedded benchmark runner. |

Validation options without `--startup-tests` do not request an ordinary embedded
test run. The game removes runtime options before forwarding startup-test or
benchmark arguments. Invalid values warn and fall back to a valid environment
setting or the build default. Input switches take no value except
`--inputBackends=<list>`. A custom
startup-benchmark build may reject other arguments.
When built, `GameWIPTests.exe --help` and `GameWIPBenchmarks.exe --help` show
their options. See @ref project_command_line_tools for the available targets.

Run `.\gamewip.bat runtime` or select **Development > Run game executable**
to open the interactive builder. On Linux, use `bash ./gamewip.sh runtime`.
The direct `run` action accepts the same options:

```powershell
.\gamewip.bat run game-dev -BuildBeforeRun -LogLevel trace -InputDebug -WindowEvents
```

`-BuildBeforeRun` rebuilds the selected preset before launch. Without it, the
helper builds only when the executable is missing.

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
8. Enter `GameWIP::Game::run()`, which composes Desktop and Input services,
   opens the game window, attaches Input on Windows, and runs the event loop.
9. Return the runtime facade's exit code.

## Input and Desktop integration

`game/runtime/game.cpp` initializes Input before opening the Desktop window. On
Windows it calls `GameWIP::Input::Platform::Win32::attachToWindow()` to route
native window messages to Input. The runtime captures Input frames to report
device changes and input activity, but does not map controls to game actions.
See @ref input_library and @ref desktop_native_interop for contracts and error
handling.

Tracy-enabled builds emit frame marks before startup validation, startup
benchmarks, and runtime execution. The enclosing named zones identify those
process phases in a capture. Zone colors distinguish process/runtime work,
initialization, frames, waits, Input capture and reporting, validation,
benchmarks, and cleanup or failure paths. Runtime plots track Window events per
pump and Input events and drops per capture.

There is no process-wide exception boundary around startup validation, benchmark
execution, or `GameWIP::Game::run()`. The correctness runner converts exceptions
escaping module callbacks, but its outer setup and allocation work and the
benchmark runner may still propagate. Runtime code should express expected startup
or shutdown failures through its returned exit code. Any exception that reaches
`main()` follows the language runtime's uncaught-exception behavior.

## Runtime facade

`GameWIP::Game::run(int, char **)` is the executable-owned transition from
startup wiring into runtime composition.

- `argc` and `argv` are borrowed for the call. The runtime parses its options
  from the table above and ignores other runtime arguments.
- The returned integer becomes the executable's process exit code.
- Reusable behavior belongs in its owning foundation or tools library.

`run()` performs this runtime sequence:

1. Initialize Logger's Console output and register the runtime's Startup,
   Runtime, Display, Input, Window, and Shutdown areas. The Startup area reports
   that Logger started and its selected minimum severity. The Runtime area
   reports the GameWIP version. Debug is the default minimum severity except in
   Release and MinSizeRel builds, which default to Info.
   `GAMEWIP_LOG_LEVEL` selects the level; `--logLevel=<level>` overrides it.
   `Logger started` reports the selected level and source. Source names and RGB
   label colors are defined in `game/runtime/logging.h` before logging begins.
2. Enumerate connected displays. Debug logs one concise record per display
   with its desktop size, active mode, supported-mode count, and HDR state.
   Trace adds display geometry, color measurements, and the full grouped mode
   list.
   Optional measurements stay absent when the OS does not report them. Failures
   in these optional queries produce Warn records with portable and native
   status codes; they do not prevent startup.
3. Open Input and a visible, focused borderless fullscreen window. By default,
   Desktop selects the display. `GAMEWIP_DISPLAY_INDEX` can select a zero-based
   entry from the enumerated display list. Desktop owns mode application and
   restoration; see @ref desktop_fullscreen_monitors. On Windows, attach Input
   to the window. Info logs the device count and connection changes. Debug
   lists each connected logical device once with its reported name, inferred
   type, source backends, total control count, and count with standard IDs.
   Key, button, wheel, and axis changes identify their device number when
   `--inputDebug` is enabled. Win32
   uses HID product and manufacturer strings when available, with generic labels
   as fallback. Interfaces are grouped by Windows device-container identity
   when available, with each interface path retained under its source.
   Controls without a standard ID can still be reported by HID identity.
   `--inputDevices` adds bounded grouped source, path, and control details at
   Trace, including reported ranges. Keyboard controls are counted rather than
   expanded.
   `--inputDebug` logs raw axis values, mouse movement and wheel deltas, and
   window-relative pointer position at Debug; Trace adds event metadata, raw
   keyboard fields, mouse-button flags, and one bounded report preview per
   device and backend. Text contents are never logged. Repeated control invalidations are Trace-only.
   Dropped events and final Input health issues produce warnings.
4. The Runtime area reports when the session is ready, gives the manual exit
   path, and logs the loop rate, display, and enabled diagnostic switches at
   Debug. The Window area logs close requests at Info with their source and
   event sequence, then logs when the window closes successfully. Debug also
   adds the window's actual mode, monitor, visibility, focus, client size, and
   framebuffer size. A `user` close source identifies a user-originated
   request, but does not distinguish Alt+F4 from other user close actions.
5. Run the Window pump and Input capture together at 60 Hz by default.
   `GAMEWIP_EVENT_WAIT_MS` sets another loop interval when needed (valid range
   1 through 1000). `--windowEvents` logs each event with its details at Debug
   and Trace. Track dropped events and summarize window and Input
   activity when the window closes. Trace adds pump, skipped-tick, and
   wait-duration details. Info reports elapsed time from session-ready through
   cleanup, with a `stop_reason` of `close_request` or `event_pump_failure`.
6. Detach and stop Input, then stop the window. Logger flushes and shuts down
   after session cleanup.
   Trace adds Input health and Logger queue statistics. Logger initialization
   and shutdown failures retain their status details on stderr.

Invalid runtime values generate warnings and use the documented fallbacks; the
selected minimum level controls whether warnings are visible. An out-of-range
display index falls back to the operating-system selection. Loop interval and
display selection use environment settings.

The final Window close releases current-thread display-color resources before
`GameWIP::Game::run()` returns. Process-level Desktop regression children verify
that standalone color discovery, `WM_CLOSE`, and owner-thread cleanup do not
replace the intended successful process exit code.

Failure to initialize Logger, enumerate displays, open Input, open or close the
window, attach Input, pump events, or close Input returns `EXIT_FAILURE`. Once
Logger has started, it is flushed and shut down on every runtime result. A
normal-path flush or shutdown failure also changes the result to
`EXIT_FAILURE`. Undelivered operation failures retain their original status on
stderr.

Runtime messages use the registered area IDs, so the console can group and
color each subsystem consistently. The default line layout places the padded
area before the padded severity, followed by the message. Structured records
carry the observed display, input-device, window, event-queue, and Logger health
values. Unavailable optional measurements are not represented as zero. Logger's
configuration and formatting options are documented in @ref logger.

Tracy-enabled builds identify display discovery, window open and close, close
requests, event waits, runtime event cycles, and Logger lifetime with named
zones and messages. The window close zone includes the close call, the successful
"Window stopped" message, and a successful-close marker on the Tracy timeline.
They plot each event wait as `Event wait (ms)` and emit a frame mark after each
successful event-pump cycle. The Game runtime zone covers
`GameWIP::Game::run()`. After Logger starts, its shutdown zone also wraps
cleanup following session failures. Call sites use
`game/runtime/profiling.h` without checking `GAMEWIP_TRACY_ENABLED`.

Use @ref GameWIP::Game for the generated source API reference.

## Version metadata

CMake configures `game/runtime/version.h.in` into `gamewip/version.h` during
project configuration and refreshes it before building the game executable.

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

Configuration creates the initial values, and the game version-header target
refreshes them whenever the executable is built. A long-lived build tree can
therefore observe newer repository state on rebuild without reconfiguration.
Release interpretation is owned by @ref project_versioning.

## Startup validation facade

`game/validation/validation.h` is included unconditionally by `main.cpp`.

- `GAMEWIP_STARTUP_TESTS_ENABLED` controls whether `runTests()` forwards to the correctness runner.
- `GAMEWIP_STARTUP_BENCHMARKS_ENABLED` controls whether `runBenchmarks()` forwards to the benchmark runner.
- Each macro defaults to `0` when the including target does not define it.
- Disabled functions return successful empty results and do not retain runner dependencies.

These definitions are target-private composition controls, not installed
configuration API. Use @ref GameWIP::Validation for the generated result and
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
in an existing library. The runtime consumes Desktop's public display, renderer/color,
window-lifetime, and event-pump interfaces; Desktop retains the platform-specific
display and window mechanics.

Move code into a reusable library when it becomes general-purpose, independently testable, and useful outside the executable. Long-term engine systems
belong under `engine/` when that layer owns them.

## Source comments

Follow the repository-wide source comment and organization rules in @ref project_documentation.

Game runtime comments explain ownership and integration boundaries, startup and
shutdown sequencing, compile-time disabled behavior, registration lifetime and
module adaptation, child-process routing, process-global or generated state,
and non-obvious validation or benchmark framework requirements.

## Integration invariants

Executable integration preserves these invariants:

- `main.cpp` remains small and sequencing-focused.
- Version and help requests remain utility-only; help text reflects current
  build availability.
- Disabled validation paths introduce no validation dependency.
- Child-route results return before benchmark or runtime execution.
- Expected runtime failures remain representable as process exit codes.
- @ref project_validation owns runner and command-line behavior.
- @ref project_testing owns test-module contracts.
- @ref project_benchmarking owns benchmark-runner behavior.
- @ref project_versioning owns generated identity behavior.
- Registered source API comments remain synchronized with their contracts.

## Related pages

- @ref project_structure
- @ref project_build
- @ref project_command_line_tools
- @ref project_validation
- @ref project_testing
- @ref project_benchmarking
- @ref project_versioning
- @ref project_documentation
