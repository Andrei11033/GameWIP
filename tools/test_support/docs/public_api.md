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

- `ScopedTemporaryDirectory`: unique temporary workspace with best-effort recursive cleanup.
- `ScopedCurrentPath`: process-current-directory guard with best-effort restoration.
- `readTextFile()`: binary whole-file convenience read with an empty-result ambiguity.
- `writeTextFile()`: binary truncate-and-replace write that creates parents.
- `fileExists()`, `fileContains()`, `countFileOccurrences()`.
- `createDirectories()` and best-effort `removeIfExists()`.
- `ScopedEnvironmentVariable` and `ScopedUnsetEnvironmentVariable`: process-environment guards.

See @ref test_support_files_environment for ownership, ambiguity, exception, and coordination rules.

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

- `exitCode`: complete native unsigned 32-bit exit code when infrastructure is healthy.
- `infrastructureFailure`: TestSupport launch/setup/wait/inspection/capture failure occurred.
- `timedOut`: the configured wait expired before normal completion.
- `wasTerminatedByTest`: TestSupport requested primary-process termination during timeout or infrastructure-failure handling.
- `output`: retained combined stdout/stderr bytes.
- `outputTruncated`: bytes were drained but discarded after the retained limit.
- `exitedSuccessfully()` and `exitedWithFailure()`: convenience predicates over infrastructure, exit, timeout, and termination state.

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

APIs marked `noexcept` do not throw. Other APIs may propagate standard allocation, formatting, filesystem, standard-stream, thread, or platform-conversion exceptions as documented by their owner page.

Report calls serialize sink access. Summary counters are protected. File helpers do not create a transaction around external filesystem activity. Current-directory and environment state are process-global. Child execution, manual prompts, gate waits, worker joins, and filesystem operations can block.

## Package and binary boundary

TestSupport is a static library. Its exact-version installed package exports `GameWIP::TestSupport`, installs `test_support/test_support.h`, has no generated export header, and has no GameWIP library dependency.

Public standard-library layouts and templates require compatible compiler, standard library, runtime, and C++23 settings. Internal declarations under `test_support/internal` and platform implementations are not installed API.
