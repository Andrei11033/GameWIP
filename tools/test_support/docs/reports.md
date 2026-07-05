@page test_support_reports TestSupport reports

`GameWIP::TestSupport::Types::ReportOptions` controls where test output goes:

- `writeConsole` writes report lines to stdout.
- `writeReport` writes report lines to `reportPath`.
- `appendReport` appends instead of truncating the report at runner/context creation.
- `flushReportEachLine` flushes after every report line when immediate external reads are required.
- `consoleVerbosity` selects minimal, concise, or full stdout output.
- `reportPath` defaults to `logs/tests/latest_test_report.txt`.

`ConsoleVerbosity::Full` writes every category. `ConsoleVerbosity::Concise` writes failures, skips, manual instructions, suite results, and summaries. `ConsoleVerbosity::Minimal` writes only failures, skips, and manual instructions so an outer runner can own aggregate status output without duplication. Categories omitted from the console remain in the report file.

Parent directories for the report path are created when possible. Report open, write, and flush failures emit one `[TEST REPORT]` diagnostic to stderr, disable further file output for that sink, and do not change test results.

Report files are buffered by default. `Runner` flushes after each completed suite result, and the report sink flushes when destroyed. Tests that inspect a report before either boundary should set `flushReportEachLine = true`.

Report categories:

- `[INFO]`
- `[PASS]`
- `[FAIL]`
- `[SKIP]`
- `[MANUAL]`
- `[METRIC]`
- `[STRESS]`
- `[SUMMARY]`
- `[RESULT]`

`Context` writes suite-scoped lines such as:

```text
[PASS] [Physics] mass remains positive
[FAIL] [Physics] velocity clamp: expected true (file.cpp:42 in runPhysicsTests)
```

`Runner` writes run-level lines and one `[RESULT]` line per completed suite. `Section` writes an informational begin line and a metric line with elapsed milliseconds when the section ends.
