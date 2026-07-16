@page test_support_reports TestSupport reports

## Output selection

`Types::ReportOptions` controls independent console and report-file sinks.

- `writeConsole` enables stdout.
- `writeReport` enables report-file setup.
- `reportPath` selects the file; an empty path opens no file even when `writeReport` is true.
- `appendReport` appends rather than truncating when the sink is created.
- `flushReportEachLine` flushes the report file after each line.
- `consoleVerbosity` filters stdout categories only.

`Full` writes every category to stdout. `Concise` writes failures, skips, manual instructions, suite results, and summaries. `Minimal` writes failures, skips, and manual instructions. The report file receives all categories.

## Categories

- `[INFO]`
- `[PASS]`
- `[FAIL]`
- `[SKIP]`
- `[MANUAL]`
- `[METRIC]`
- `[STRESS]`
- `[SUMMARY]`
- `[RESULT]`

Context lines include the suite name. Failure lines include source-location details. Runner-level lines omit a suite label. Each completed suite produces one `[RESULT]` line.

## Buffering and flush boundaries

Report files are buffered by default. A runner flushes its shared file after each completed suite. A standalone context normally relies on sink destruction for the final flush. Set `flushReportEachLine` only when an external reader must observe each line immediately.

`flushReportEachLine` does not request an explicit stdout flush. Console output follows normal `std::cout` buffering.

## Sink failure

Directory creation, file open, file write, and file flush failures disable only that report-file sink and emit at most one `[TEST REPORT]` diagnostic to stderr. Counting and console output continue.

The result summary does not encode report health. `Runner::exitCode()` can be zero even when the report file failed.

This degradation rule covers ordinary stream-state failures. Reporting methods are not universally `noexcept`: allocation, formatting, path conversion, and standard-stream exceptions can still propagate.

## Concurrency and ownership

One report sink serializes complete lines. Context count updates are also serialized. Concurrent call ordering follows lock acquisition and is not a deterministic event timeline.

Independent contexts/runners own independent sinks. TestSupport does not coordinate two sinks writing the same path; use one shared runner or distinct paths.

## Related pages

- @ref test_support_expectations
- @ref test_support_troubleshooting
