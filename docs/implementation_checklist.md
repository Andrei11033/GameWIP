# GameWIP Project Implementation Checklist

## Purpose

This file is the literal project-level implementation ledger. It records repository systems that exist, project integration that is incomplete, and work required to close the active milestone.

It includes organized per-library capability groups because implementation completeness is its purpose. It does not restate every function, overload, or private algorithm; those details remain authoritative in the owning headers, implementation, manual, and tests.

Related documents:

- `roadmap.md` defines planned development order.
- `testing_checklist.md` defines project-level verification gates.
- `decisions.md` records stable project decisions and standards.
- `platform_backend_contract.md` defines the shared backend boundary.
- `docs/doxygen/project_structure.md` explains how the implemented systems fit together.

## Status legend

```text
[ ] Not implemented
[-] In progress
[x] Implemented
[!] Requires an external decision or repository setting
```

## Milestone 00: foundation

### Repository and toolchain

- [x] The root project requires CMake 3.23, C++23, Ninja, and the MSYS2 UCRT64 toolchain on Windows.
- [x] Presets exist for development, validation, benchmark, tools, profiling, optimized, shipping, coverage, documentation, and static analysis.
- [x] Build output is isolated under `build-<preset>/` and ignored by Git.
- [x] Third-party dependencies are pinned as Git submodules under `external/`.
- [x] Shared editor settings use repository-relative configuration and the documented UCRT64 tools.
- [x] Project-owned text uses UTF-8, LF, final newlines, and EditorConfig formatting rules.

### Project composition and startup

- [x] `game/main.cpp` is a stable process entry point.
- [x] `game/runtime/` owns the game-facing runtime facade.
- [x] The game executable links only dependencies used by its runtime or enabled startup modules.
- [x] Development builds can run correctness modules before the game runtime.
- [x] Child validation invocations return without entering benchmarks or game code.
- [x] Benchmark startup is independently optional.
- [x] Shipping builds remove tests, benchmarks, TestSupport startup code, assertions, Tracy, and development tools.
- [x] Optimized and shipping build presets target only `GameWIP` and its real dependency closure.
- [x] Every executable uses the shared UCRT64 runtime-dependency copy helper.

### Reusable library integration

- [x] Foundation and tool libraries are separate CMake targets with canonical `GameWIP::` aliases.
- [x] Dependency visibility distinguishes public header requirements from implementation-only dependencies.
- [x] Shared libraries use generated export headers and hidden-by-default symbol visibility.
- [x] Internal platform contracts and test-hook headers are outside public CMake file sets.
- [x] Each reusable library owns its public API comments and manual pages beside its source.
- [x] Project documentation describes composition and policy without copying library API manuals.

This milestone summary does not restate each library's features. The detailed sections below track capability groups for completion review; exact behavior remains authoritative in each library's own docs and tests. The current reusable set and dependency direction are maintained in `docs/doxygen/project_structure.md`.

### Platform backend infrastructure

- [x] `cmake/LibraryPlatform.cmake` resolves one `GAMEWIP_PLATFORM_ID` for the repository.
- [x] Platform implementations live under `<library>/platform/<platform-id>/`.
- [x] Optional backend-local CMake files own system libraries, resources, generated files, and compile definitions.
- [x] Portable core code calls internal backend contracts instead of exposing native handles publicly.
- [x] The current supported backend is `win32`.
- [ ] Additional platform IDs and backend implementations are added only when the roadmap schedules them.

### Validation architecture

- [x] Correctness tests are discovered as immediate modules under `game/validation/tests/`.
- [x] Benchmarks are discovered independently under `game/validation/benchmarks/`.
- [x] Module CMake files explicitly list sources and dependencies.
- [x] The same correctness modules can run in `GameWIPTests` and at development startup.
- [x] Focused module selection, child-process routing, aggregate reports, and deterministic module order are implemented.
- [x] Validation fixtures use scoped operating-system temporary directories.
- [x] Final reports resolve beneath the GameWIP temporary root unless an absolute path is requested.
- [x] Google Benchmark owns performance iteration and reporting; correctness tests do not enforce timing thresholds.

### Packaging and API boundaries

- [x] Every reusable library installs a CMake config package and exact pre-1.0 version file.
- [x] Installed targets use the `GameWIP::` namespace.
- [x] Transitive public package dependencies are resolved by package configs.
- [x] Only declared public header file sets and generated export headers are installed.
- [x] A clean external consumer validates installed headers, package dependencies, targets, and runtime loading.
- [x] Public entry headers compile independently.
- [x] Reviewed allowlists validate shared-library export roots.
- [x] Validation-only definitions and test-hook headers do not leak into installed target interfaces.

### Documentation system

- [x] The root Doxygen target merges explicit project pages with library-owned public headers and manuals.
- [x] Doxygen does not recursively ingest implementation files, external code, repository planning docs, or build output.
- [x] Project page IDs use the `project_` prefix where they describe project workflows.
- [x] Library page IDs and child pages remain owned by their libraries.
- [x] Duplicate project documentation pages have been removed.
- [x] Repository policy, planning, contracts, and checklists use Markdown filenames.
- [x] Project structure and extension tutorials document file ownership and how to add libraries, APIs, tests, benchmarks, docs, CMake options, and platforms.

### Repository workflow and automation

- [x] Root contribution and security policies exist.
- [x] Pull-request metadata, validation evidence, merge style, and commit message standards are documented.
- [x] GitHub workflows validate builds, tests, packages, exports, docs, repository formats, and pull-request metadata.
- [x] Repository automation reconciles issue and pull-request metadata with project status.
- [x] Workflow JavaScript has syntax and unit tests.
- [!] Branch protection, Actions variables, secrets, and GitHub Pages settings must be verified in GitHub because they are not repository files.

## Detailed library implementation status

These sections are intentionally detailed because this file is the implementation ledger. They describe completed capability groups and integration work, while the owning library manual remains the authoritative behavioral reference.

### IO

#### Target and boundary

- [x] IO is a standalone static target with the canonical `GameWIP::IO` alias.
- [x] `io/io.h` is the only installed public header and does not require another GameWIP library.
- [x] Implementation is separated under `foundation/io/core/`.
- [x] C++23 requirements, public include paths, install rules, package config, and exact version config are declared on the target.

#### Reader and writer model

- [x] Abstract `Reader` and `Writer` contracts expose byte-oriented operations, position, size, seek, flush, and close behavior where supported.
- [x] Results distinguish status from accepted/transferred byte counts.
- [x] Closed, unsupported, invalid-argument, end-of-input, and partial-transfer paths are represented without platform error types in the public API.
- [x] `MemoryReader` supports caller-owned byte, string, and vector storage while rejecting direct temporary owning objects that would dangle.
- [x] `MemoryWriter` owns append-only growable output, reports its current end position, and handles input that aliases its current storage.

#### Whole-stream helpers

- [x] Whole-stream byte and text reads use known-size fast paths when size and position are available.
- [x] Unknown-size reads grow output geometrically and support caller-owned scratch storage.
- [x] Whole-stream writes preserve partial progress in their result.
- [x] Convenience overloads remain layered on the same Reader/Writer contracts.

#### Ownership artifacts

- [x] Public IntelliSense comments and library-owned quick start, contracts, examples, performance, troubleshooting, and testing pages exist.
- [x] The public-header isolation check and installed-package consumer include IO.

### FileSystem

#### Target and boundary

- [x] FileSystem is a standalone static target with the canonical `GameWIP::FileSystem` alias.
- [x] Its public dependency on IO is declared because public file objects implement IO contracts.
- [x] Public declarations live in `filesystem/filesystem.h`; platform declarations remain under `filesystem/internal/`.
- [x] Public owning file and lock types hide backend state behind pImpl storage.

#### Paths and entry queries

- [x] Public path handling uses `std::filesystem::path` and preserves native path semantics.
- [x] UTF-8 conversion helpers define explicit conversion/failure behavior.
- [x] Existence, type, size, timestamp, permissions, canonicalization, current-directory, and temporary-directory queries are implemented.
- [x] Query results distinguish missing entries, invalid input, unsupported behavior, and platform failures.
- [x] Symlink follow/no-follow policy is explicit where entry identity matters.

#### File and directory operations

- [x] File creation/open modes, reading, writing, appending, seeking, flushing, truncation, and close behavior are implemented.
- [x] Directory creation, recursive creation, enumeration, path movement, empty removal, and recursive removal are implemented; file copy is provided separately.
- [x] Replace/fail-if-exists behavior is explicit for destructive operations.
- [x] Persistent readers/writers preserve partial IO progress through IO result types.
- [x] Atomic byte/text replacement uses a restrictive same-directory temporary file and has no silent non-atomic fallback.
- [x] Atomic options cover parent creation, replacement policy, symlink policy, temporary prefix validation, file flush, and parent-directory flush.

#### Locking and platform implementation

- [x] Shared/exclusive file locks support explicit unlock and RAII cleanup.
- [x] Native unlock failure leaves ownership state retryable instead of falsely decrementing ownership.
- [x] The Win32 backend implements wide-character path operations behind the internal contract.
- [x] Backend sources are selected through the shared platform resolver.

#### Ownership artifacts

- [x] Library-owned docs cover quick start, public API, open modes, Unicode paths, directories, atomic writes, examples, troubleshooting, and testing.
- [x] Package config, public-header isolation, installed consumer, and modular correctness validation include FileSystem.

### Terminal

#### Target and boundary

- [x] Terminal is a shared target with the canonical `GameWIP::Terminal` alias and generated export header.
- [x] IO is a public dependency because Terminal accepts IO writer abstractions publicly.
- [x] Only the public API and generated export header are installed; backend/test-hook headers remain internal.
- [x] Hidden visibility and the reviewed export allowlist constrain the shared-library ABI.

#### Output and styling

- [x] stdout/stderr writing supports UTF-8, native line endings, complete-record writes, and redirected output.
- [x] Portable colors/styles degrade predictably when terminal capabilities are absent.
- [x] Segmented writes and `OutputBuffer` compose styled output without exposing platform escape details to callers.
- [x] Formatted print bridges support public templates while keeping backend implementation out of the header.
- [x] Formatted output runs user formatter callbacks before taking the stream lock and uses nesting-safe reusable scratch storage.

#### Input and control state

- [x] Input reads and mode changes are implemented through the backend contract.
- [x] Input mode scopes capture and restore complete previous backend state.
- [x] Cursor visibility and alternate-screen scopes are nesting-safe and expose setup/restore status.
- [x] Capability and redirection queries let callers choose interactive versus plain behavior.
- [x] Repeated Win32 input-availability polling reuses bounded event scratch instead of allocating on every poll.

#### Ownership artifacts

- [x] Win32 console implementation, validation-only hooks, package config, public-header check, export check, and installed consumer are present.
- [x] Library docs cover quick start, reading/writing, Unicode, styling, segmented writes, control primitives, input modes, capabilities, examples, troubleshooting, and testing.

### Logger

#### Target and dependency boundary

- [x] Logger is a shared target with the canonical `GameWIP::Logger` alias and generated export header.
- [x] FileSystem, Terminal, and IO are implementation dependencies; Logger's public header does not expose their types.
- [x] Core implementation is split by state, queue, filters, formatting, reports, and test hooks.
- [x] Win32-specific debugger, popup, time, memory, and scratch behavior remains behind the internal platform contract.

#### Configuration, filtering, and formatting

- [x] Default, low-memory, and throughput configurations are implemented.
- [x] Output modes, severity thresholds, source registration, source filters, and level filters are implemented.
- [x] Compile-time and runtime format paths share bounded producer-side formatting behavior.
- [x] Invalid formats, allocation failures, unknown sources, and truncation are counted separately.
- [x] Reentrant custom formatters receive separate per-thread scratch so nested logging cannot overwrite the outer message.

#### Queue, sinks, and reports

- [x] Normal accepted messages use the bounded asynchronous queue.
- [x] Queue pressure distinguishes soft and hard drops from intentional filtering and sink failures.
- [x] Out-of-order producer publication wakes the worker when the exact queue head becomes drainable, avoiding a busy-yield loop.
- [x] Console output delegates to Terminal and file output delegates to FileSystem/IO.
- [x] Synchronous reports bypass filters and the async queue.
- [x] Bounded report/flush paths preserve the distinction between “line written” and “queue drained.”
- [x] Fatal report and termination paths support optional platform UI without making normal logging interactive.

#### Lifecycle and observability

- [x] Initialization, repeated initialization, flush, shutdown, and repeated lifecycle behavior are implemented.
- [x] Producer calls, report calls, and shutdown coordinate across threads.
- [x] Stats expose queued, written, drop, failure, truncation, unknown-source, and peak-queue evidence.
- [x] Validation-only failure injection covers rare file, allocation, popup, and timeout paths without entering the installed interface.

#### Ownership artifacts

- [x] Package config, public-header checks for API and macros, export allowlist, installed consumer, correctness module, and benchmarks are present.
- [x] Library docs separate user configuration/lifecycle/API/performance material from test-hook and validation material.

### Assert

#### Target and compile contract

- [x] Assert becomes a shared runtime target when enabled and an interface-only compile contract when disabled.
- [x] The canonical alias is `GameWIP::Assert`; the public header includes a generated export header only when the runtime exists.
- [x] Compile definitions make enabled/disabled macro behavior explicit to consumers.
- [x] A separate internal compile interface supports validation objects without attaching executable resources.

#### Macro families and actions

- [x] Fatal assertion, verification, and unreachable families are implemented.
- [x] Recoverable check, check-once, ensure, and verification behavior is implemented.
- [x] Expression evaluation rules and return/value behavior are fixed per macro family.
- [x] Interactive variants are separate opt-in developer paths.
- [x] Passing paths avoid formatting, logging, popup work, and unnecessary allocation.

#### Runtime and platform support

- [x] Runtime failures delegate durable diagnostics to Logger.
- [x] Win32 interactive actions and test hooks remain behind internal platform contracts.
- [x] Common Controls v6 resources are generated and attached through build-tree and installed-package helpers.
- [x] Disabled/shipping configurations remove the assertion runtime and Logger dependency.

#### Ownership artifacts

- [x] Package config, public-header check, export allowlist, installed consumer, modular correctness tests, and passing-path benchmarks are present.
- [x] Library docs cover macro behavior, actions, diagnostics, interactivity, examples, troubleshooting, public API, hooks, and testing.

### TestSupport

#### Target and responsibility

- [x] TestSupport is a standalone static target with the canonical `GameWIP::TestSupport` alias.
- [x] Its public API depends only on the standard library and does not pull production diagnostics into tests.
- [x] TestSupport owns reusable test mechanics; GameWIP's validation runner owns project module selection and report placement.

#### Test execution and reporting

- [x] Context, section, runner, summary, suite result, and report-option types are implemented.
- [x] Boolean, equality, inequality, near, file-content, and explicit failure/skip/manual expectations are implemented.
- [x] Reports distinguish INFO, PASS, FAIL, SKIP, MANUAL, METRIC, STRESS, RESULT, and SUMMARY records.
- [x] Console verbosity and retained complete reports are independently configurable.
- [x] Timers provide diagnostic duration without turning elapsed time into a pass/fail threshold.

#### Isolation and process utilities

- [x] Scoped temporary directories clean recursively on normal return and exception unwinding.
- [x] Scoped current-directory and environment-variable helpers validate inputs, report setup failures, and restore prior process state without throwing from destructors.
- [x] Text-file helpers support deterministic fixture and report setup.
- [x] Child-process launching captures result, timeout, exit, and output evidence while rejecting invalid UTF-8, embedded nulls, and invalid environment names.
- [x] Start gates and stop flags support deterministic concurrency/stress coordination.
- [x] Manual checks are explicit and never required by unattended runs.

#### Ownership artifacts

- [x] Win32 environment and child-process backends, package config, public-header check, installed consumer, and correctness module are present.
- [x] Library docs cover quick start, expectations, reports, files/environment, child processes, timing/stress, manual checks, examples, troubleshooting, and testing.

### Game validation and runtime shell

#### Module framework

- [x] Registration records include name, deterministic order, run callback, and optional child-argument ownership.
- [x] Duplicate/invalid registrations are rejected before module execution.
- [x] Ambiguous child routes, conflicting selection/exclusion, and module exceptions produce explicit failed results.
- [x] Shared invocation policy carries report, verbosity, manual UI, popup, and child-process controls.
- [x] The standalone test executable and development startup link the same object modules.
- [x] CTest creates a focused entry and independent report path for each registered module.

#### Benchmark framework

- [x] Correctness and benchmark source trees are separate.
- [x] Benchmark modules register independently and link Google Benchmark only when requested.
- [x] Standalone and optional startup benchmark runners share the same module objects.
- [x] Current scenarios cover passing Assert paths and representative Logger producer/output paths.

## Post-milestone maintenance

- [ ] Add a project-level checklist entry only when a repository system, integration boundary, or milestone gate changes.
- [ ] Put new product work in `roadmap.md`, not in this completed-foundation ledger.
- [ ] Put individual library implementation status in the owning issue, pull request, tests, and source artifacts; update the consumer manual only when supported behavior changes.
- [ ] Archive completed milestone evidence when this file becomes difficult to scan; do not let it grow into a second API manual.

## Update rule

Every item must name a concrete repository capability. Mark it implemented only when the corresponding files and integration exist. Proof and test-case detail belong in `testing_checklist.md`, library validation pages, and automated results rather than being repeated in this implementation ledger.
