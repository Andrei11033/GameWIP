@page logger Logger

Logger is the runtime diagnostics library for normal asynchronous logs and important synchronous reports.

Use normal logs for regular runtime information. Use reports for failures where the message must be written immediately, such as assertion failures, startup errors, or shutdown errors.

## Consumer manual

- @subpage logger_quick_start
- @subpage logger_public_api
- @subpage logger_configuration
- @subpage logger_lifecycle
- @subpage logger_threading_performance
- @subpage logger_macros
- @subpage logger_reports
- @subpage logger_stats
- @subpage logger_examples
- @subpage logger_troubleshooting

## Maintainer validation

- @subpage logger_testing
- @subpage logger_test_hooks

## Generated API reference

Use @ref GameWIP::Logger for lifecycle, filtering, logging, reporting, statistics, and formatting helpers, and @ref GameWIP::Logger::Types for configuration and result shapes. The generated header reference documents every public type, enum value, field, function, overload, macro, and constant; the manual pages above explain runtime behavior and selection rules.

## Key behavior

Normal log calls are asynchronous, queue-based, and filterable. Report calls are synchronous, bypass filters and the asynchronous queue, write to active sinks immediately, and flush before returning.

Logger uses the shared Terminal behavior for stdout/stderr, portable styling, Unicode output, redirection, and line endings.

`Logger::fatal(...)` is a fatal-severity normal log; use `reportFatal(...)` or `fatalTerminate(...)` when the failure path must flush, request the fatal popup, or terminate.
