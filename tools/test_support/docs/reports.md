@page test_support_reports Reports

Reporting types live under `TestSupport::Types::Reporting` and are declared in `test_support/reporting.h`.

`Types::Reporting::Options` configures console mirroring and the report-file sink. `Types::Reporting::ConsoleVerbosity` controls console categories.
An optional `consoleWriter` receives a category and formatted line without a line ending. It returns whether the line was written successfully.

Validation supplies a Terminal writer that colors complete report lines, including runner-owned status lines: passes green, failures red, skips
yellow, manual checks and summaries cyan, info gray, metrics blue, and stress reports magenta. Result lines are green when there are no failures
and red otherwise. Terminal's `Auto` style mode leaves redirected output plain; standalone TestSupport is plain by default. Report files always
remain plain and receive the complete report while enabled.

If a custom writer returns `false`, TestSupport reports the failure and does not retry the line, since it may have been partially written.

`Context` owns per-suite counts and expectations. `Runner` aggregates completed suites. `Types::Reporting::Summary` contains passed/failed/skipped
counts, and `Types::Reporting::SuiteResult` adds the suite name and elapsed milliseconds.

`Types::Reporting::SinkStatus` reports the requested report path, `SinkState`
(`Disabled`, `Active`, `Failed`, or `Finalized`), and the separate
`failureStatus`. A standalone `Context` or `Runner` should call `finalize()`
when its report must be proven complete; finalization is idempotent. A
runner-owned suite context cannot finalize the shared sink independently.

Ordinary report-file setup/write/flush failures disable only that sink and do not rewrite test pass/fail counts. Formatting and stream operations
that are not documented `noexcept` can throw; see @ref test_support_expectations for the suite exception boundary.

Provide valid UTF-8 for suite/check names, messages, and values produced by custom formatters. Reporting writes this text without encoding validation
or repair; use a hexadecimal or another text representation for arbitrary bytes. `readTextFile()` validates report fixtures when tests read them back.

@ref test_support_public_api
@ref test_support_expectations
