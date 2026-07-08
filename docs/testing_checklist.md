# GameWIP Project Verification Checklist

## Purpose

This file defines the project-level evidence required to trust the repository foundation and close a milestone. It verifies integration, packaging, automation, documentation, and shipping composition.

It includes organized per-library proof groups because validation completeness is its purpose. It does not copy every test case or assertion; executable modules and library-owned maintainer pages remain authoritative.

Related documents:

- `implementation_checklist.md` records which project systems exist.
- `roadmap.md` defines future development order.
- `decisions.md` records stable standards.
- `docs/doxygen/testing.md` explains how to run correctness tests.
- `docs/doxygen/validation.md` explains the validation architecture.

## Status legend

```text
[ ] Not verified
[-] Verification in progress
[x] Verified automatically or by a recorded inspection
[M] Manual or environment-dependent verification
[!] Requires external repository access or configuration
```

## Milestone 00 verification gates

### Clean configuration and builds

- [x] The `validation` preset configures and builds with MSYS2 UCRT64 GCC and Ninja.
- [x] The `development` preset builds the game with startup correctness modules.
- [x] The `shipping` preset builds without validation, assertions, Tracy, or tools.
- [x] A clean shipping preset does not build or copy unrelated standalone library artifacts.
- [x] The `benchmark` preset builds optimized benchmark targets.
- [x] The `docs` preset builds documentation without requiring the game executable.
- [x] The `static-analysis` preset configures with UCRT64 Clang.
- [x] The `address-sanitizer` preset configures with MSYS2 CLANG64 and passes every registered project test under AddressSanitizer.
- [x] Preset and workspace JSON parse successfully.

### Correctness and runtime integration

- [x] `ctest --preset validation` passes every registered project test.
- [x] Every correctness module appears as a focused CTest entry.
- [x] The development executable runs compiled-in modules before entering game runtime.
- [x] Focused selection runs only the requested module.
- [x] Unknown module selection fails with a nonzero result.
- [x] Conflicting module selection and exclusion fails instead of reporting a zero-module pass.
- [x] Child-process routes return their exact result without recursive full-suite startup.
- [x] Ambiguous child routes and unexpected module exceptions become explicit failed results.
- [x] Successful automated runs retain aggregate reports and clean scoped fixtures.
- [x] Manual UI and the Logger fatal popup default off and are enabled independently by `--manual-ui` and `--logger-popup`.
- [x] Automated runner tests verify positive-option parsing, module-selection independence, module-invocation propagation, and removal of the obsolete manual options.

The detailed groups below summarize library behavior coverage. Module sources and library-owned testing pages remain the authoritative contract-to-test map.

## Detailed library verification

These sections provide milestone evidence at a finer level. They summarize behavior groups and hard-to-reach paths without replacing the executable tests or library manuals.

### IO verification

#### Status and base contracts

- [x] Default/success/failure statuses and stable error names are verified.
- [x] Reader and Writer default, move, open/closed, capability, seek, position, size, flush, and close contracts are verified.
- [x] Unsupported operations return the documented status rather than silently succeeding.
- [x] Invalid backend byte counts and impossible progress reports are rejected.

#### Memory IO

- [x] MemoryReader covers byte spans, string views, vectors, sequential reads, seeking, overlap, end-of-input, and close state.
- [x] Direct construction from temporary owning string/vector storage is rejected at compile time.
- [x] MemoryWriter covers append-only growth, position, capacity reuse, extraction, clear, close state, and aliased input.

#### Whole-stream helpers

- [x] Known-size and unknown-size reads are verified.
- [x] Exact limit, over-limit, zero-limit, partial read, zero-progress, and backend failure paths are verified.
- [x] Caller-owned scratch buffers are verified.
- [x] Partial writes, repeated progress, zero progress, and failure-after-progress preserve transferred-byte counts.

### FileSystem verification

#### API shape and paths

- [x] Public result/option defaults and move-only file/lock ownership are compile-time checked.
- [x] Empty paths, invalid enum values, incompatible options, UTF-8 conversion failures, and embedded-invalid data are rejected predictably.
- [x] Current path, temporary path, absolute/canonical/weakly-canonical operations, and UTF-8 round trips are verified.

#### Queries and metadata

- [x] Missing-path predicates return successful false; value queries return `NotFound`.
- [x] File, directory, symlink, size, time, permission, and entry-info queries are verified.
- [x] Timestamp conversions and read-only metadata behavior are covered.

#### Handles and whole-file helpers

- [x] Reader/writer create, open, append, truncate, seek, position, size, flush, close, and repeated-close behavior are verified.
- [x] Sharing and replace policies are verified against concurrent handles where supported.
- [x] Whole-file byte/text read, write, append, and partial-progress results are verified.
- [x] Persistent handles remain safe across move construction and reject hidden ownership replacement.

#### Directories and mutation

- [x] Single and recursive directory creation, listing, nested entries, and cleanup are verified.
- [x] File copy, path move, file/empty-directory removal, recursive tree removal, missing-source, existing-target, and read-only cases are verified.
- [x] Intermediate and final symlink policies are exercised when the host grants symlink creation.
- [M] Full symlink proof remains environment-dependent when Windows denies symlink creation.

#### Atomic writes and locks

- [x] Atomic replacement, fail-if-exists, parent creation, invalid temporary prefixes, temporary cleanup, and visible final content are verified.
- [x] File and parent-directory flush requests are routed as real durability requests.
- [x] Shared/shared, shared/exclusive, and exclusive/exclusive lock contention are verified.
- [x] Explicit unlock, RAII cleanup, move ownership, double unlock, and native unlock-failure retry state are verified.

### Terminal verification

#### Output

- [x] stdout/stderr text, bytes, lines, formatted output, segmented output, and `OutputBuffer` are verified.
- [x] One complete backend write is used for composed records where promised.
- [x] UTF-8, native line endings, stdout/stderr routing, and redirected plain-text fallback are verified.
- [x] `StyleMode::Never`, `Auto`, and `Required` success/failure behavior is verified.
- [x] Invalid style/segment/write options are rejected before output.
- [x] Isolated formatter reentry proves same-stream nested `print()` and `println()` calls complete without deadlock or outer-message corruption.

#### Input and capabilities

- [x] Line, byte, and bounded text reads cover success, end, timeout, would-block, and invalid options.
- [x] Input availability, terminal-size, redirection, and capability queries are verified.
- [x] Output preparation is idempotent and propagates forced backend failure.

#### Control state

- [x] Input mode get/set and complete prior-state restoration are verified.
- [x] Cursor movement/position/visibility, clear, title, bell, and alternate-screen operations are verified.
- [x] The opt-in real-console suite covers Unicode, style/color, cursor behavior, alternate-screen handling, input, and terminal-state restoration.
- [M] Run the real-console suite in Windows Terminal with `--test-module=terminal --manual-ui` and record the observations.
- [x] Cursor and alternate-screen scopes cover nesting, failed setup, failed restoration, explicit retry, move, and destruction.
- [x] Per-stream concurrency and shared-runtime behavior with Logger are verified.

#### Internal hooks

- [x] Validation builds expose backend-neutral source-tree hooks only when `TERMINAL_ENABLE_TEST_HOOKS` is enabled.
- [x] Hook-disabled builds remove hook declarations and installed packages omit their headers.
- [x] Forced state is reset between scenarios.

### Logger verification

#### Configuration and lifecycle

- [x] Default, low-memory, and throughput configurations expose reviewed values.
- [x] Init, double init, shutdown, double shutdown, and repeated cycles are verified.
- [x] Output modes None, Console, File, and Both are covered.
- [x] Default relative log placement is tested in a scoped temporary working directory.

#### Sources, filters, and formatting

- [x] String sources, registered IDs, enum sources, and unknown IDs are verified.
- [x] Minimum level, source filter, level filter, and dynamic filter changes are verified.
- [x] Filtered calls do not enter the queue or inflate failure/drop counters.
- [x] Compile-time and runtime format overloads, invalid runtime formats, long-message truncation, and formatting failure counters are verified.
- [x] A reentrant custom formatter proves nested logging preserves both nested and outer messages.

#### Async queue and sinks

- [x] Accepted entries, queue capacity, soft/hard drops, peak depth, and final drain are verified.
- [x] Console routing uses Terminal styling/redirection/UTF-8/native-line-ending behavior.
- [x] File output covers UTF-8 directories, open/write/flush failures, and concurrent reader sharing.
- [x] Sink failures remain distinct from queue pressure and intentional filtering.

#### Reports, fatal paths, and statistics

- [x] Reports write synchronously, bypass filters and the async queue, and remain available under queue pressure.
- [x] Timed reports distinguish completed synchronous write from bounded async drain result.
- [x] Error/fatal reports and fatal termination route through the report contract.
- [x] Queued, written, drop, allocation, format, file, popup, unknown-source, truncation, and peak counters are verified.
- [x] Reset clears resettable counters while preserving lifetime queue-drop evidence.

#### Concurrency, hooks, and UI

- [x] Concurrent producers, reporting during production, flush during production, shutdown during production, and final flush are stress-tested.
- [x] Source-tree hooks force allocation, file open/write/flush, popup, and timeout paths deterministically.
- [x] Hook headers and build-only definitions are absent from installed target interfaces.
- [M] The real fatal popup is runtime opt-in and requires human inspection when changed.

### Assert verification

#### Compile modes and evaluation

- [x] Runtime-enabled, assertion-disabled, checks-disabled, and shipping interface-only configurations compile with the intended definitions.
- [x] ASSERT, VERIFY, CHECK, CHECK_ONCE, ENSURE, UNREACHABLE, and interactive variants follow documented expression-evaluation rules.
- [x] Passing expressions avoid message formatting and failure/report calls.
- [x] ENSURE evaluates once and returns the resulting boolean.
- [x] CHECK_ONCE reports only the intended first failure per site.

#### Diagnostics and actions

- [x] Expression, message, source location, function, and optional diagnostic detail reach Logger reports as configured.
- [x] Recoverable macros return; fatal macros terminate through isolated child-process tests.
- [x] Break, Abort, Ignore Once, and Always Ignore action handling is verified through automated forced actions.
- [x] Unreachable behavior is tested with and without the configured optimizer assumption.

#### Hooks, resources, and UI

- [x] Source-tree hooks force primary-dialog fallback, fallback defaults, debugger detection, and popup suppression.
- [x] Hook declarations and definitions are absent from normal installed interfaces/exports.
- [x] Common Controls v6 resources attach correctly for build-tree and installed consumers.
- [M] Real Win32 action dialogs and debugger-break behavior require opt-in human validation when changed.

### TestSupport verification

#### Expectations and aggregation

- [x] Context and Runner aggregate pass, fail, skip, manual, metric, stress, result, and summary records correctly.
- [x] Boolean, equality, inequality, near, file-content, explicit fail, skip, and manual expectations are verified.
- [x] Failed expectations allow suite continuation while preserving a failing final result.
- [x] Sections record scoped duration and nested result context.

#### Reporting

- [x] Minimal, concise, and full console category policies are verified.
- [x] Retained reports contain complete evidence independently of console verbosity.
- [x] Buffered, suite-boundary, and immediate flush modes are verified.
- [x] Invalid report paths degrade to console evidence and report diagnostics without hiding test results.

#### Files, environment, and process state

- [x] Text helper success/failure and parent creation behavior are verified.
- [x] Scoped temporary directories are unique and clean nested artifacts.
- [x] Scoped current path and set/unset environment variables restore prior state.
- [x] Environment helpers reject empty or invalid names, embedded nulls, and invalid UTF-8, and their destructors remain non-throwing.
- [x] Restoration occurs during normal return and exception unwinding.

#### Child processes and coordination

- [x] Successful/nonzero exit, timeout, requested termination, capture limits, continued pipe draining, and descendant cleanup are verified.
- [x] Invalid UTF-8 arguments, embedded-null arguments/values, and invalid environment names are rejected before process creation.
- [x] StartGate and StopFlag coordinate workers without timing-only correctness assumptions.
- [x] Timers report diagnostic metrics without enforcing performance thresholds.
- [x] Manual checks skip rather than block when unattended.
- [x] TestSupport remains independent of all other GameWIP libraries.

### Public API and package isolation

- [x] Every installed public entry header compiles in isolation.
- [x] A clean install-prefix consumer configures without source-tree include paths or short build-tree targets.
- [x] The clean consumer links canonical `GameWIP::` imported targets and runs successfully.
- [x] Package configs resolve required public dependencies at the exact pre-1.0 version.
- [x] Installed file sets exclude internal headers and test hooks.
- [x] Installed target interfaces exclude validation-only definitions.
- [x] Terminal, Logger, and Assert shared-library exports match reviewed allowlists.
- [x] Shipping exports do not contain test-hook roots.

### Static and repository analysis

- [x] clang-format accepts all maintained C++ sources.
- [x] clang-tidy accepts all maintained translation units with warnings treated as errors.
- [x] External and generated sources are excluded from project-owned analysis.
- [x] Repository automation JavaScript passes syntax checks and unit tests.
- [x] Maintained JSON files parse successfully.
- [x] actionlint accepts all GitHub Actions workflows.
- [x] Git reports no whitespace errors in the reviewed diff.

### Documentation verification

- [x] Doxygen builds from explicit inputs.
- [x] The Doxygen warning log is empty.
- [x] Every registered library has a landing page.
- [x] Project pages describe repository composition and workflows; library pages describe library APIs and behavior.
- [x] Relative Markdown file links resolve.
- [x] README commands, filenames, CMake minimum, presets, and output paths match the repository.
- [x] Security reporting instructs private disclosure.
- [x] Project documentation uses the shared heading, voice, terminology, list, and authority conventions.
- [x] Generated HTML navigation exposes project structure, extension, build, validation, quality, and library sections without a duplicate documentation page.

### Performance and coverage evidence

- [x] `GameWIPBenchmarks.exe --benchmark_dry_run` validates benchmark registration.
- [x] Benchmarks report queue/drop/error counters where throughput could otherwise hide lost work.
- [x] CI does not use machine-dependent timing as a merge gate.
- [x] The coverage preset instruments project-owned correctness paths and produces HTML and XML reports.
- [x] Coverage report generation fails on corrupt profile data instead of suppressing errors.
- [x] Coverage is reviewed as diagnostic evidence; no arbitrary percentage gate is claimed.

### Environment-dependent checks

- [M] Win32 manual dialogs and terminal interactions are checked when their owning library changes.
- [M] Symlink creation and symlink-policy scenarios require a Windows account with Developer Mode or create-symbolic-link privilege; a skip must be recorded when unavailable.
- [M] Tracy connection and profiling UI are checked when profiling integration changes; the capture must show `GameWIP Main`, `GameWIP process`, `Startup validation`, `Startup benchmarks`, and `Game runtime` without test-owned Tracy zones.
- [M] Runtime DLL origin is inspected when the UCRT64 toolchain or dependency-copy helper changes.

### External GitHub configuration

- [!] Required branch-protection checks must be verified on `master`.
- [!] `PROJECT_OWNER`, `PROJECT_NUMBER`, and `ACTIVE_MILESTONE` Actions variables must be verified in repository settings.
- [!] `PROJECT_TOKEN` permissions and GitHub Pages deployment settings must be verified without exposing secrets.
- [!] A dry-run project reconciliation and one documentation deployment must be observed after workflow changes reach `master`.

## Standard local pre-merge pass

Run the relevant focused command while developing, then use this complete local pass before merging foundation changes:

```powershell
cmake --preset validation
cmake --build --preset validation
ctest --preset validation --output-on-failure
.\build-validation\GameWIPBenchmarks.exe --benchmark_dry_run

cmake --preset static-analysis
cmake --build --preset static-analysis

cmake --preset docs
cmake --build --preset docs

cmake --preset shipping
cmake --build --preset shipping

$env:PATH = "C:\MSYS2\clang64\bin;$env:PATH"
cmake --preset address-sanitizer
cmake --build --preset address-sanitizer
ctest --preset address-sanitizer --output-on-failure
```

Also run the repository checks documented in `docs/doxygen/static_analysis.md`. Record exact commands, results, environment-dependent skips, and manual observations in the pull request.

## Update rule

Add an item only when it proves a project integration boundary or milestone acceptance condition. Put individual behavior assertions in the owning library test suite. Do not use this file as a historical test transcript; Git, CI, and pull requests retain run history.
