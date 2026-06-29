@page test_support_troubleshooting TestSupport troubleshooting

## Report file is empty

Check `Types::ReportOptions::writeReport`, `reportPath`, and `appendReport`. Parent directories are created when possible. An open or write failure emits one `[TEST REPORT]` stderr diagnostic and disables further report-file writes for that sink.

## Temporary test files remain after a run

Use `ScopedTemporaryDirectory` for the owning workspace and ensure file handles and subsystem workers stop before the scope exits. Cleanup errors are suppressed so a locked file cannot hide the original test outcome.

## A suite continues after a failed expectation

This is expected. Expectations record failures and return `false`; they do not abort the suite. Use the returned boolean when a scenario should stop early.

## Child-process output is missing

Check `Types::ChildProcessOptions::captureOutput`. Captured output combines stdout and stderr when capture is enabled.

## A child process timed out

Inspect `ChildProcessResult::timedOut` and `wasTerminatedByTest`. A timeout is reported separately from the process exit code.

## Manual checks skip unexpectedly

`promptManualCheck()` returns `Types::ManualAnswer::Skipped` when standard input ends before an answer.

## Related pages

- @ref test_support_reports
- @ref test_support_expectations
- @ref test_support_child_processes
