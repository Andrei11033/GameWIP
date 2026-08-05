@page test_support_public_api Public API

Include `test_support/test_support.h`. Installed consumers link `GameWIP::TestSupport`; source-tree consumers link `TestSupport`.

## Constant

`kDefaultMaxCapturedOutputBytes` is the default retained-memory limit for combined child stdout/stderr capture. The child pipe continues to drain after that limit so discarded output does not deadlock a verbose child.

## Reporting and result types

### `Types::ConsoleVerbosity`

- `Minimal`: failures, skips, and manual instructions.
- `Concise`: minimal output plus suite results and summaries.
- `Full`: every report category, including passes and diagnostics.

Console verbosity does not filter the report file.

### `Types::ReportOptions`

- `writeConsole`: enables stdout mirroring.
- `writeReport`: enables the report-file sink.
- `appendReport`: appends instead of truncating when the sink is created.
- `flushReportEachLine`: flushes the report file after each written line.
- `consoleVerbosity`: selects stdout categories.
- `reportPath`: report-file path; an empty path disables file opening.

### `Types::Summary`

Fields are `passed`, `failed`, and `skipped`. `total()` returns their sum. `ok()` tests only `failed == 0`; skipped checks and an empty summary remain successful.

### `Types::SuiteResult`

Fields are the owning suite `name`, its `summary`, and `elapsedMilliseconds`. `ok()` forwards to `summary.ok()`.

### Infrastructure status and value results

`Types::InfrastructureError` provides compact TestSupport-owned categories for invalid input, unsupported backends, allocation, process, capture, environment, file, and platform failures. `Types::InfrastructureStatus` stores one category and a `std::uint64_t nativeCode`; `ok()` is true only for `None`.

`Types::TextResult`, `Types::BoolResult`, and `Types::CountResult` pair that status with text, a boolean value, or a count. Successful status construction and inspection do not allocate. A native code of zero means no numeric diagnostic was available.

`formatInfrastructureStatus()` turns a status into a stable category name and appends a nonzero native code. Call it only when constructing a human-readable diagnostic; formatting may allocate.

## Reporting owners

### `Context`

A context owns one suite name, one result counter, and a shared report sink. A standalone context creates its own sink from `ReportOptions`; contexts constructed by `Runner` share the runner sink.

Recording methods are:

- informational: `info()`, `manual()`, `metric()`, `stress()`, `summary()`;
- counted outcomes: `pass()`, `fail()`, `skip()`;
- expectations: `expectTrue()`, `expectFalse()`, `expectEq()`, `expectNe()`, `expectNear()`, `expectContains()`, `expectFileContains()`, `expectFileOccurrenceCount()`;
- queries: `suiteName()`, `result()`, and `ok()`.

Public recording serializes count updates and sink writes. `suiteName()` returns an object-owned reference valid for the context lifetime.

### `Runner`

`Runner` owns one report sink and aggregates suites completed through `runSuite()`. It also provides run-level `info()` and `summary()` lines plus `result()`, `ok()`, and `exitCode()`.

`runSuite()` accepts a callable invocable with `Context&` or with no arguments. If both forms are viable, the `Context&` form is selected. Uncaught exceptions become one failed check and do not escape the suite call.

### `Section`

`Section` writes an informational begin line on construction and attempts to write an elapsed-time metric on destruction. It does not affect result counts. The referenced `Context` must outlive the section. Destructor reporting failures are suppressed.

## Timing

`Timer` starts at construction, can be restarted with `reset()`, and returns steady-clock elapsed milliseconds through `elapsedMilliseconds()`. It is diagnostic infrastructure, not benchmark-grade measurement, and one object is not internally synchronized.

## File and process-state helpers

- `ScopedTemporaryDirectory`: non-throwing unique temporary workspace with construction status and best-effort recursive cleanup.
- `ScopedCurrentPath`: non-throwing process-current-directory guard with construction status and best-effort restoration.
- `readTextFile()`: status plus binary whole-file text.
- `writeTextFile()`: status-returning binary truncate-and-replace write that creates parents.
- `fileExists()`, `fileContains()`, and `countFileOccurrences()`: status plus an unambiguous domain value.
- `createDirectories()` and `removeIfExists()`: explicit cleanup status.
- `ScopedEnvironmentVariable` and `ScopedUnsetEnvironmentVariable`: non-throwing process-environment guards with construction status.

See @ref test_support_files_environment for ownership, failure, cleanup, and coordination rules.

## Child-process and manual-check types

### `Types::EnvironmentVariable`

`name` is the child environment key. `value` sets the key; `std::nullopt` removes it. Overrides are applied in vector order.

### `Types::ChildProcessOptions`

Fields are:

- `executablePath`;
- `arguments`;
- `environment`;
- `timeout`;
- `captureOutput`;
- `maxCapturedOutputBytes`;
- `inheritParentEnvironment`.

### `Types::ChildProcessResult`

- `status`: infrastructure status independent of child outcome.
- `exitCode`: complete native unsigned 32-bit exit code when `outcome` is `Exited`.
- `outcome`: `NotStarted`, `Exited`, `TimedOut`, `TerminatedDuringCleanup`, or `OutcomeUnavailable`.
- `outputTruncated`: bytes were drained but discarded after the retained limit.
- `output`: retained combined stdout/stderr bytes, including useful partial capture on failure.

`Types::ChildProcessOutcome` describes the child domain independently from `Types::InfrastructureStatus`. A nonzero exit and an enforced timeout are not infrastructure failures.

### `Types::ManualAnswer`

`Yes`, `No`, and `Skipped` represent the recognized manual-check outcomes. `promptManualCheck()` returns `Skipped` on end-of-input.

See @ref test_support_child_processes and @ref test_support_manual_tests.

## Stress helpers

- `StartGate`: one-shot gate; `wait()` blocks until idempotent `open()`.
- `StopFlag`: one-way cooperative stop request through `requestStop()` and `stopRequested()`.
- `runWorkers()`: creates, joins, and coordinates a fixed set of worker threads, then rethrows one captured failure.

See @ref test_support_timing_stress.

## Ownership and copying

`Summary`, `SuiteResult`, report/process option values, process results, environment overrides, and `Timer` are ordinary value types. `Context`, `Runner`, `Section`, both filesystem guards, both environment guards, `StartGate`, and `StopFlag` are non-copyable and non-movable owners of synchronization, references, or process state.

`Section` stores a non-owning `Context&`. Guard query methods such as `path()` and `previousPath()` return references owned by the guard. Child-process options and results own their strings, paths, vectors, and captured output.

## Exceptions, blocking, and threading

Infrastructure helpers marked `noexcept` convert expected implementation allocation, filesystem, environment, process, and platform failures into status. Caller-side construction of allocating standard-library arguments remains governed by those types. Other APIs may propagate formatting, standard-stream, thread, or allocation exceptions as documented by their owner page.

Report calls serialize sink access. Summary counters are protected. File helpers do not create a transaction around external filesystem activity. Current-directory and environment state are process-global. Child execution, manual prompts, gate waits, worker joins, and filesystem operations can block.

## Package and binary boundary

TestSupport is a static library. Its exact-version installed package exports `GameWIP::TestSupport`, installs `test_support/test_support.h`, has no generated export header, and has no GameWIP library dependency.

Public standard-library layouts and templates require compatible compiler, standard library, runtime, and C++23 settings. Internal declarations under `test_support/internal` and platform implementations are not installed API.
