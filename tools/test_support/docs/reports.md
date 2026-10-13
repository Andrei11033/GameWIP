@page test_support_reports Reports

Reporting types live under `TestSupport::Types::Reporting` and are declared in `test_support/reporting.h`.

`Types::Reporting::Options` configures console mirroring and the report-file sink. `Types::Reporting::ConsoleVerbosity` controls only console
categories. Report-file output receives the complete report while enabled.

`Context` owns per-suite counts and expectations. `Runner` aggregates completed suites. `Types::Reporting::Summary` contains passed/failed/skipped
counts, and `Types::Reporting::SuiteResult` adds the suite name and elapsed milliseconds.

Ordinary report-file setup/write/flush failures disable only that sink and do not rewrite test pass/fail counts. Formatting and stream operations
that are not documented `noexcept` can throw; see @ref test_support_expectations for the suite exception boundary.

Provide valid UTF-8 for suite/check names, messages, and values produced by custom formatters. Reporting writes this text without encoding validation
or repair; use a hexadecimal or another text representation for arbitrary bytes. `readTextFile()` validates report fixtures when tests read them back.

@ref test_support_public_api
@ref test_support_expectations
