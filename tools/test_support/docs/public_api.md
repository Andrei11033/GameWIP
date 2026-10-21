@page test_support_public_api Public API

`test_support/test_support.h` is the normal umbrella. Focused consumers may include:

```text
test_support/types.h
test_support/reporting.h
test_support/files.h
test_support/process.h
test_support/stress.h
```

Installed consumers link `GameWIP::TestSupport`; source-tree consumers link `TestSupport`.

## Shared types

`GameWIP::TestSupport::Types` is the single passive-type tree. Shared infrastructure vocabulary remains at its root:

- `InfrastructureError`: compact operational categories, including `EncodingFailed` for malformed UTF-8 text data.
- `InfrastructureStatus`: `error` plus optional numeric `nativeCode`; `ok()` is true only for `None`.
- `TextResult`, `BoolResult`, and `CountResult`: shared status plus one value.

`formatInfrastructureStatus()` (`types.h`) formats a status only when human-readable diagnostics are requested and may allocate; status
construction and inspection never allocate diagnostic text.

## Reporting types

Reporting vocabulary lives under `Types::Reporting`:

- `ConsoleVerbosity`: `Minimal`, `Concise`, or `Full`.
- `Options`: console/report-file behavior and report path.
- `Summary`: passed/failed/skipped counts.
- `SuiteResult`: suite name, summary, and elapsed milliseconds.
- `ManualAnswer`: `Yes`, `No`, or `Skipped`.
- `SinkState`: `Disabled`, `Active`, `Failed`, or `Finalized`.
- `SinkStatus`: `state`, requested `reportPath`, and first `failureStatus`.

`Context`, `Runner`, `Section`, `Timer`, and `promptManualCheck()` are active helpers directly under `GameWIP::TestSupport`.

A failed expectation records a failure and returns `false`; it does not abort the suite. `Runner::runSuite()` converts an exception escaping the suite
callable into one recorded failure and continues normal result aggregation.

`Runner::reportStatus()` and `Context::reportStatus()` inspect the report sink. `Runner::finalize()` flushes and closes it, is idempotent, and
never changes assertion counts or `exitCode()`; check the returned `SinkStatus` to detect report-file failures. `Context::finalize()` applies only
to standalone contexts; runner-owned contexts leave the shared sink to the `Runner`.

## File helpers

`test_support/files.h` owns:

- `ScopedTemporaryDirectory`;
- `ScopedCurrentPath`;
- `readTextFile()` and `writeTextFile()`;
- `fileExists()`, `fileContains()`, and `countFileOccurrences()`;
- `createDirectories()` and `removeIfExists()`.

Text-file operations are strict UTF-8. They do not normalize Unicode, add/remove a BOM, or convert line endings. `readTextFile()` returns only valid
UTF-8 in `TextResult::text`; malformed or incomplete content reports `EncodingFailed` and may retain the complete valid prefix. `writeTextFile()`
validates before creating parent directories or opening/truncating the destination.

`ScopedCurrentPath` requires `ScopedProcessState`, and its `restore()` reports explicit restoration failures or process-state conflicts.

## Process types

Child-process vocabulary lives under `Types::Process`:

- `EnvironmentOverride`: one child environment set/unset override;
- `Options`: executable, arguments, `environmentOverrides`, optional `waitTimeout`, child-only `workingDirectory`, capture policy, and environment inheritance;
- `Outcome`: `NotStarted`, `Exited`, `TimedOut`, `TerminatedDuringCleanup`, or `OutcomeUnavailable`;
- `Result`: infrastructure status, exact exit code when available, outcome, `outputTruncated`, and retained raw `outputBytes`.

`runChildProcess()` is the active operation. `kDefaultMaxCapturedOutputBytes` (4 MiB) is the default `Options` capture limit. Child stdout/stderr
capture is arbitrary bytes, not UTF-8 text. External processes may emit malformed data and TestSupport must preserve that data unchanged within the
configured retention limit.

`ScopedProcessState`, `ScopedEnvironmentVariable`, and `ScopedUnsetEnvironmentVariable` also live in `test_support/process.h`. The process-state scope
serializes TestSupport's process-global operations only: environment or current-directory changes made directly through native or standard-library
calls bypass the lock and the caller must coordinate them. Environment guards require the scope and expose `restore()` for an explicit result;
unrelated native mutations are detected as `ProcessStateConflict` rather than silently overwritten.

## Stress helpers

`test_support/stress.h` owns `StartGate`, `StopFlag`, and `runWorkers()`. A `runWorkers()` callable invocable with both `std::size_t` and no
arguments receives the worker index. Worker exceptions do not stop peers; every started worker is joined, then one scheduling-dependent exception is
rethrown.

## Type organization

Types live under `TestSupport::Types::Reporting` and `TestSupport::Types::Process`.
Details: @ref test_support_reports, @ref test_support_files_environment,
@ref test_support_child_processes, @ref test_support_timing_stress.
Dependencies: @ref test_support.
