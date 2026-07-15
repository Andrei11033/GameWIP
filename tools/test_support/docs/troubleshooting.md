@page test_support_troubleshooting TestSupport troubleshooting

## Report file is missing or empty

Check `writeReport`, `reportPath`, and `appendReport`. An empty path disables file opening. Ordinary directory/open/write/flush failure emits one `[TEST REPORT]` diagnostic and disables that sink without affecting the test result.

A standalone context normally flushes at destruction; a runner flushes after each completed suite. Use `flushReportEachLine` when another process must read each line immediately.

## Runner succeeds although the report failed

This is expected. `exitCode()` reflects recorded test failures, not report-file health.

## A suite continues after a failed expectation

Expectations record and return a result; they do not abort. Return early based on the boolean when later steps require the check to pass.

## A zero file-occurrence expectation passed for a missing file

`countFileOccurrences()` returns zero for empty search text, missing/unreadable input, and no matches. Check `fileExists()` first or use a helper with detailed I/O status.

## Temporary files remain

Close files and stop subsystem workers before the temporary-directory guard is destroyed. Cleanup is best effort; locks, external processes, abnormal termination, or filesystem errors can leave artifacts.

## Current directory or environment restored unexpectedly

These states are process-global. TestSupport serializes individual environment mutations, not whole scope lifetimes. Avoid overlapping guards for the same state and prevent unrelated code from mutating it.

On Win32, setting an environment guard to an empty string removes the variable.

## Child output is missing

Check `captureOutput`. stdout and stderr share one retained byte stream. A zero retained limit discards output while still draining it. Inspect `outputTruncated`.

## Child returned `exitCode == -1`

This indicates TestSupport launch, setup, wait, inspection, or capture failure. It is not a normal child exit code. Also inspect `timedOut` and `wasTerminatedByTest`.

## Child call exceeded the configured timeout

The timeout starts termination; job cleanup and output-reader shutdown can extend total call duration.

## Manual checks skip unexpectedly

EOF returns `Skipped`. The prompt does not clear stream state or trim whitespace.

## Worker run hangs during thread-start failure

Already-started workers are joined before startup failure is rethrown. Do not make them wait irrevocably for a fixed participant count unless startup failure can release them.

## Related pages

- @ref test_support_reports
- @ref test_support_expectations
- @ref test_support_files_environment
- @ref test_support_child_processes
- @ref test_support_timing_stress
