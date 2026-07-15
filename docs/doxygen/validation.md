@page project_validation Validation architecture

GameWIP validation is modular. The same correctness-test and benchmark code can run in standalone executables for CI and focused local work or be linked into the development game executable as startup validation.

Validation is development infrastructure and is not linked into shipping builds.

## Scope

This page owns correctness-runner composition, source API, command-line policy, module registration, child-process routing, report ownership, result semantics, and runner lifecycle.

Test authoring is documented in @ref project_testing. Benchmark measurement policy is documented in @ref project_benchmarking. Executable startup integration is documented in @ref project_game_executable.

## Build controls

| Option | Purpose | Source default |
| --- | --- | --- |
| `GAMEWIP_BUILD_TESTS` | Builds `GameWIPTests` and CTest entries. | `ON` |
| `GAMEWIP_BUILD_BENCHMARKS` | Builds `GameWIPBenchmarks`. | `OFF` |
| `GAMEWIP_RUN_TESTS_AT_STARTUP` | Links correctness modules into `GameWIP` and runs them before runtime code. | `ON` |
| `GAMEWIP_RUN_BENCHMARKS_AT_STARTUP` | Links benchmarks into `GameWIP` and runs them after startup tests. | `OFF` |

When both build and startup options for one validation kind are disabled, its modules are not compiled or linked into the executable. Google Benchmark is added only when benchmark targets or startup benchmarks require it.

## Source API reference

Use @ref GameWIP::Validation for shared results and the startup facade, and @ref GameWIP::Validation::Tests for the correctness runner and module registry.

These are source-tree integration interfaces. They are not installed package APIs.

### `TestResult`

| Field | Contract |
| --- | --- |
| `modulesRun` | Number of module callbacks invoked. |
| `modulesFailed` | Number of invoked callbacks returning nonzero; runner-level failures use a value of one even when no callback ran. |
| `exitCode` | `0` or `1` for normal aggregate runs; the owning module's exact code for a routed child invocation. |
| `handledChildInvocation` | The caller must return immediately because the command line was classified as a child protocol, including an ambiguous child-route failure. |

`ok()` is true only when `modulesFailed == 0` and `exitCode == 0`. An empty explicit module set therefore produces a successful empty result.

### `RunOptions`

| Field | Purpose |
| --- | --- |
| `enableStressTests` | Enables deterministic stress scenarios in modules that provide them. |
| `enableChildCrashTests` | Enables subprocess scenarios that intentionally terminate abnormally. |
| `enableTestSupportChildProcessTests` | Enables TestSupport process-launch validation. |
| `enableAutomatedInteractiveTests` | Enables non-UI simulations of interactive Assert behavior. |
| `enableManualUiTests` | Enables checks requiring human input or real UI. |
| `enableLoggerPopupTest` | Enables Logger's real fatal-popup validation. |
| `verboseConsole` | Mirrors full TestSupport report categories to stdout. |
| `stressThreadCount` | Shared worker count for module stress scenarios. |
| `loggerStressIterationsPerThread` | Per-worker Logger stress operation count. |
| `assertStressIterations` | Assert stress repetition count. |
| `writeReport` | Enables the retained aggregate report. |
| `appendReport` | Makes the first selected module append rather than replace the report. |
| `reportPath` | Absolute report path or a relative path resolved under the GameWIP temporary root. |

Command-line arguments may override the corresponding policy fields. The runner takes `RunOptions` by value so those overrides do not mutate the caller's object.

### `run()`

`Tests::run()` performs one complete runner invocation. It is intended to run once at a time in a process; it coordinates process-wide module registration, standard streams, report paths, and module code that may mutate other global state.

The runner catches exceptions from module callbacks and converts them into failed module results. Allocation or setup exceptions outside those protected callbacks are not a general exception-free API promise.

## Module registration

A test module defines one process-lifetime `Registration` object in its `module.cpp` file. Its `Module` record contains:

- A non-empty stable name.
- A deterministic integer order; name breaks equal-order ties.
- A non-null run callback.
- An optional child-argument matcher.

The module name is a `std::string_view`; its storage must outlive every registry and runner use. String literals or other static storage are the intended source. Callback function pointers must remain valid for the process lifetime.

Registration appends to process-local vector storage and may allocate. It is intended for static initialization before runner use and is not synchronized for late or concurrent registration. A span returned by `registeredModules()` is invalidated by a later registration.

The runner copies registrations before sorting and execution. It rejects empty names, null run callbacks, and duplicate names before invoking any module.

Current correctness modules are `runner`, `io`, `filesystem`, `terminal`, `test_support`, `logger`, and `assert` in their stable order.

## Module invocation

`ModuleInvocation` borrows the original process arguments and shared runner policy for the duration of one callback.

- `argc` and `argv` are not owned by the module.
- `options` is valid only while the callback runs.
- `appendReport` tells the module whether its TestSupport sink must preserve earlier aggregate output.
- A zero callback result means pass; any nonzero result means fail.

A module adapter should only translate shared policy into its library-specific test options and invoke the suite. Reusable test behavior belongs in the module's `_test.cpp` file or TestSupport, not in the adapter.

## Command-line interface

The runner recognizes these project-level arguments:

| Argument | Behavior |
| --- | --- |
| `--test-module=<name>` | Selects one registered module. Unknown or empty names fail validation. |
| `--test-report=<path>` | Selects the aggregate report path. |
| `--no-test-report` | Disables retained file output. |
| `--verbose-tests` | Mirrors every TestSupport report category to stdout. |
| `--manual-ui` | Enables human-interactive checks. |
| `--logger-popup` | Enables Logger's real fatal-popup check. |
| `--no-test-support-child-process` | Disables TestSupport child-process scenarios. |

Compatibility selectors remain supported:

| Argument | Equivalent policy |
| --- | --- |
| `--io-only` | Select `io`. |
| `--filesystem-only` | Select `filesystem`. |
| `--terminal-only` | Select `terminal`. |
| `--test-support-only` | Select `test_support`. |
| `--no-io-tests` | Exclude `io`. |
| `--no-filesystem-tests` | Exclude `filesystem`. |
| `--no-terminal-tests` | Exclude `terminal`. |

New tooling should use `--test-module=<name>`. Repeating the same selector is accepted; selecting different modules or selecting and excluding the same module is an error.

The runner does not remove recognized arguments or reject unrelated arguments. Every selected module receives the original `argc` and `argv`, allowing module-owned child protocols and library-specific test logic to inspect them.

## Child-process routing

Child matchers are evaluated for every sorted module before ordinary module selection.

- No match continues to normal selection.
- One match invokes only the owning module and preserves its exact exit code.
- Multiple matches are an ambiguity failure and set `handledChildInvocation` so executable startup returns immediately.
- A matcher exception becomes a runner failure.

Matchers should inspect arguments only. They should not perform the child operation or mutate shared validation state.

This routing prevents crash, fatal, reentrant-format, and process-helper child invocations from recursively running the full suite or entering game runtime code.

Current limitation: the Assert and TestSupport matchers claim any argument with their reserved child prefix, while their suites recognize only defined complete selectors. A malformed reserved selector is routed to the owner but can fall through to that module's ordinary full suite. Reserved child arguments are internal protocol values; callers must use only the documented/generated exact forms until unknown suffixes fail closed.

## Report paths and output

Relative report paths are lexically normalized under the GameWIP directory in the operating-system temporary root. A relative path that still contains a parent-traversal component after normalization is rejected. Absolute paths are normalized and honored as explicit destinations.

On Windows, use an ordinary relative path with neither a root name nor a root directory when temporary-root containment is required. The current classifier does not reject drive-relative forms such as `D:report.txt` or root-relative forms such as `\report.txt`; path composition can place those outside the intended GameWIP subtree.

An empty or invalid report path disables retained file reporting and emits a console diagnostic; it does not fail the tests. `--no-test-report` has the same execution semantics without treating the path as invalid.

The runner prints:

- The resolved report path when retained output is active.
- One module result line with the module's exact callback code.
- One aggregate result with selected and failed counts.

TestSupport owns suite detail and report-file behavior. After the first selected module writes an aggregate report, subsequent modules receive `appendReport == true` so earlier evidence is preserved.

## Module lifecycle

1. Copy and sort registrations by order, then name.
2. Validate registrations and duplicate names.
3. Evaluate child-route ownership.
4. Apply runner arguments and validate selection.
5. Resolve retained-report policy.
6. Invoke selected modules in sorted order.
7. Convert escaped module exceptions into failed module results.
8. Continue after ordinary module failures and emit the aggregate result.

Normal aggregate failures produce `exitCode == 1`; only routed child invocations preserve an arbitrary module exit code.

## Runner test seam

`GameWIP::Validation::Tests::Detail::runWithModules()` accepts an explicit module span so the `runner` validation module can test ordering, selection, conflict handling, and policy propagation without mutating the static registry.

Its declaration lives in `game/validation/tests/internal/runner_test_hooks.h`. It is an approved source-tree test seam, is not registered as ordinary generated API, is not installed, and must not be used by application or validation-module code.

## Preset behavior

| Preset | Validation behavior |
| --- | --- |
| `development` | Startup correctness tests enabled. |
| `validation` | Standalone correctness tests and benchmark registration checks. |
| `benchmark` | Optimized standalone benchmarks. |
| `shipping` | Validation, benchmarks, and development assertions disabled. |
| `coverage` | Standalone correctness tests with coverage instrumentation. |
| `address-sanitizer` | Standalone correctness tests with AddressSanitizer instrumentation. |
| `docs` | Doxygen only; validation execution disabled. |

## Maintainer notes

When changing validation behavior:

- Give every module a stable lowercase name and order.
- Keep registration data in static storage.
- Route each child protocol to exactly one module.
- Keep default runs unattended.
- Preserve console failure visibility when report output fails.
- Keep internal seams under `internal/` and out of installed packages.
- Add runner regression coverage for parsing, selection, routing, ordering, and error conversion.

## Related pages

- @ref project_game_executable
- @ref project_testing
- @ref project_benchmarking
- @ref project_build
- @ref project_documentation
