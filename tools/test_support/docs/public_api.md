@page test_support_public_api TestSupport public API

Include `test_support/test_support.h`. Installed-package consumers link `GameWIP::TestSupport`; builds within the source tree link the short `TestSupport` target. See @ref test_support_quick_start for complete CMake examples.

TestSupport has no dependencies on other project libraries and uses only the C++ standard library in its public contract.

Passive options and results live in `GameWIP::TestSupport::Types`; active helpers live directly in `GameWIP::TestSupport`.

## Suite ownership

`Runner` owns a shared report sink, runs named suites, catches uncaught suite exceptions as failures, and aggregates completed results. `Context` records one suite's output and counts. Standalone contexts own their own report sink.

Expectations record pass or failure and return whether the check passed. They do not abort execution or invoke another assertion system.

`Section` is an RAII timing/reporting scope and does not affect pass, failure, or skip counts.

## Reporting

Report files are buffered by default, flushed after each completed suite, and flushed when the sink is destroyed. Set `flushReportEachLine` only when another reader must observe each line immediately.

Report write failures do not abort the test process. They disable further file output while console reporting and result aggregation continue.

`ConsoleVerbosity::Minimal` writes only failures, skips, and manual instructions. `ConsoleVerbosity::Concise` also writes results and summaries. `ConsoleVerbosity::Full` mirrors every category. The report file receives every category in all three modes.

## Process and environment isolation

Scoped environment helpers restore the previous process state on destruction. Environment mutation is process-global, so overlapping conflicting scopes across threads are unsupported.

`ScopedTemporaryDirectory` owns an isolated workspace beneath the operating-system temporary directory and removes its complete tree on destruction. Test executables should use it instead of writing fixtures or subsystem logs relative to their working directory.

`ScopedCurrentPath` supports tests of intentionally relative-path APIs and restores the previous process path. Because the current path is process-global, such scopes require exclusive control over relative path resolution.

Child-process capture retains at most `maxCapturedOutputBytes` while continuing to drain excess output. A zero limit retains nothing. `outputTruncated` reports discarded bytes; disabling capture leaves output empty and ignores the limit.

On Win32, the child inherits only the selected standard handles. Timeouts and primary-process completion terminate lingering descendants so they cannot keep capture handles open.

## Timing and stress helpers

Timer reports diagnostic elapsed time; benchmark-style iteration metrics belong to Google Benchmark. `runWorkers()` joins every worker and rethrows a captured worker exception after joining.

See @ref test_support_expectations, @ref test_support_reports, @ref test_support_child_processes, @ref test_support_files_environment, and @ref test_support_timing_stress for examples and detailed contracts.
