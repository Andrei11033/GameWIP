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

`formatInfrastructureStatus()` formats a status only when human-readable diagnostics are requested; status construction and inspection do not allocate
diagnostic text.

## Reporting types

Reporting vocabulary lives under `Types::Reporting`:

- `ConsoleVerbosity`: `Minimal`, `Concise`, or `Full`.
- `Options`: console/report-file behavior and report path.
- `Summary`: passed/failed/skipped counts.
- `SuiteResult`: suite name, summary, and elapsed milliseconds.
- `ManualAnswer`: `Yes`, `No`, or `Skipped`.

`Context`, `Runner`, `Section`, `Timer`, and `promptManualCheck()` are active helpers directly under `GameWIP::TestSupport`.

A failed expectation records a failure and returns `false`; it does not abort the suite. `Runner::runSuite()` converts an exception escaping the suite
callable into one recorded failure and continues normal result aggregation.

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

## Process types

Child-process vocabulary lives under `Types::Process`:

- `EnvironmentOverride`: one child environment set/unset override;
- `Options`: executable, arguments, `environmentOverrides`, timeout, capture policy, and environment inheritance;
- `Outcome`: `NotStarted`, `Exited`, `TimedOut`, `TerminatedDuringCleanup`, or `OutcomeUnavailable`;
- `Result`: infrastructure status, exact exit code when available, outcome, `outputTruncated`, and retained raw `outputBytes`.

`runChildProcess()` is the active operation. Child stdout/stderr capture is arbitrary bytes, not UTF-8 text. External processes may emit malformed
data and TestSupport must preserve that data unchanged within the configured retention limit.

`ScopedEnvironmentVariable` and `ScopedUnsetEnvironmentVariable` also live in `test_support/process.h`. Their names and values are text at the
platform boundary and use the same explicit status/outcome semantics as the other process helpers.

## Stress helpers

`test_support/stress.h` owns `StartGate`, `StopFlag`, and `runWorkers()`.

## Dependencies and package boundary

TestSupport is a static library. It may depend on foundational `GameWIP::Unicode` for actual UTF-8 text semantics. It must not acquire IO, FileSystem,
Terminal, Logger, Assert, Window, or engine dependencies merely for convenience.

The installed package resolves the exact matching Unicode package automatically. Public headers remain standard-library based; the Unicode dependency
is implementation-only text validation.

## Compatibility

The installed package exposes the current nested reporting and process
vocabulary under `TestSupport::Types::Reporting` and
`TestSupport::Types::Process`. It provides no flat compatibility aliases.

@ref test_support_reports
@ref test_support_files_environment
@ref test_support_child_processes
@ref test_support_timing_stress
