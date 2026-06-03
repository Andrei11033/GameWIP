@page logger Logger

The GameWIP Logger is the runtime diagnostics library for normal asynchronous logs and important synchronous reports.

Use normal logs for regular runtime information. Use reports for failures where the message must be written immediately, such as assertion failures, startup errors, or shutdown errors.

## Documentation sections

- @subpage logger_getting_started
- @subpage logger_api_reference
- @subpage logger_runtime_behavior
- @subpage logger_developer_validation

## Normal user path

Most users should read Logger getting started first, then Logger API reference. Runtime behavior pages are for configuration, lifecycle, queue pressure, threading, and performance details. Developer validation pages are generated for maintainers and rare-path testing.

## Key behavior

Normal log calls are asynchronous, queue-based, and filterable. Report calls are synchronous, bypass filters and the async queue, write to active sinks immediately, and flush before returning.

`Logger::fatal(...)` is a fatal-severity normal log; use `reportFatal(...)` or `fatalTerminate(...)` when the failure path must flush, request the fatal popup, or terminate.

Logger builds as a shared library target.
