@page logger Logger

The GameWIP Logger is the runtime diagnostics library for normal asynchronous logs and important synchronous reports.

Use normal logs for regular runtime information. Use reports for failures where the message must be written immediately, such as assertion failures, startup errors, or shutdown errors.

## Documentation sections

### User manual

- @subpage logger_getting_started
- @subpage logger_quick_start
- @subpage logger_public_api
- @subpage logger_examples
- @subpage logger_troubleshooting
- @subpage logger_configuration
- @subpage logger_lifecycle
- @subpage logger_macros
- @subpage logger_reports
- @subpage logger_stats

### Reference and concepts

- @subpage logger_api_reference
- @subpage logger_runtime_behavior
- @subpage logger_threading_performance

### Developer validation

- @subpage logger_testing
- @subpage logger_test_hooks
- @subpage logger_developer_validation

## Normal user path

Use the getting started and API reference pages for public integration. Runtime behavior pages cover configuration, lifecycle, queue pressure, threading, and performance details. Developer validation pages are for maintainers and rare-path testing.

## Key behavior

Normal log calls are asynchronous, queue-based, and filterable. Report calls are synchronous, bypass filters and the async queue, write to active sinks immediately, and flush before returning.

`Logger::fatal(...)` is a fatal-severity normal log; use `reportFatal(...)` or `fatalTerminate(...)` when the failure path must flush, request the fatal popup, or terminate.
