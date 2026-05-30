@page logger Logger

The GameWIP Logger provides two diagnostic paths:

- **normal logs** for regular runtime information, and
- **reports** for important synchronous diagnostics.

Use normal logs for high-volume runtime output. Use reports for failures where the message must be written immediately, such as assertion failures, startup errors, or shutdown errors.

Manual pages:

- @subpage logger_quick_start
- @subpage logger_lifecycle
- @subpage logger_configuration
- @subpage logger_macros
- @subpage logger_reports
- @subpage logger_stats
- @subpage logger_threading_performance
- @subpage logger_testing
- @subpage logger_test_hooks
- @subpage logger_troubleshooting
- @subpage logger_examples

Key behavior: normal log calls are asynchronous, queue-based, and filterable. Report calls are synchronous, bypass filters and the async queue, write to active sinks immediately, and flush before returning. `Logger::fatal(...)` is a fatal-severity normal log; use `reportFatal(...)` or `fatalTerminate(...)` when the failure path must flush, request the fatal popup, or terminate.
