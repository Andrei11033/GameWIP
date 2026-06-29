@page logger Logger

Logger is the runtime diagnostics library for normal asynchronous logs and important synchronous reports.

Use normal logs for regular runtime information. Use reports for failures where the message must be written immediately, such as assertion failures, startup errors, or shutdown errors.

## User manual

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

## Developer validation

- @subpage logger_testing
- @subpage logger_test_hooks

## Generated API reference

Use @ref GameWIP::Logger for lifecycle, filtering, logging, reporting, statistics, and formatting helpers, and @ref GameWIP::Logger::Types for configuration and result shapes. The generated header reference documents every public type, enum value, field, function, overload, macro, and constant; the manual pages above explain runtime behavior and selection rules.

## Key behavior

Normal log calls are asynchronous, queue-based, and filterable. Report calls are synchronous, bypass filters and the async queue, write to active sinks immediately, and flush before returning.

Logger uses FileSystem and IO for its file sink and the shared Terminal runtime for stdout/stderr, portable styling, Unicode output, redirection, and line endings. Its private platform backend owns only Logger-specific debugger output, fatal UI, time, process-memory, and thread-scratch behavior.

`Logger::fatal(...)` is a fatal-severity normal log; use `reportFatal(...)` or `fatalTerminate(...)` when the failure path must flush, request the fatal popup, or terminate.
