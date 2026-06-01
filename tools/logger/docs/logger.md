@page logger Logger

The GameWIP Logger is the runtime diagnostics library for normal asynchronous logs and important synchronous reports.

Use normal logs for regular runtime information. Use reports for failures where the message must be written immediately, such as assertion failures, startup errors, or shutdown errors.

## User manual

- @subpage logger_quick_start
- @subpage logger_public_api
- @subpage logger_lifecycle
- @subpage logger_configuration
- @subpage logger_macros
- @subpage logger_reports
- @subpage logger_stats
- @subpage logger_threading_performance
- @subpage logger_troubleshooting
- @subpage logger_examples

## Developer validation references

These pages are for maintainers validating the library, not for normal logger use:

- @subpage logger_testing
- @subpage logger_test_hooks

## Key behavior

Normal log calls are asynchronous, queue-based, and filterable. Report calls are synchronous, bypass filters and the async queue, write to active sinks immediately, and flush before returning.

`Logger::fatal(...)` is a fatal-severity normal log; use `reportFatal(...)` or `fatalTerminate(...)` when the failure path must flush, request the fatal popup, or terminate.
