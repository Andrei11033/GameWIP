@page logger Logger

The GameWIP Logger is a process-wide C++20 diagnostic library. It has two intentionally separate output paths:

- **Normal logs**: asynchronous, queue-based, filterable messages for regular runtime telemetry.
- **Reports**: synchronous, filter-bypassing diagnostics for failures that should be written immediately.

Use normal logs for high-volume information such as startup progress, asset loading, frame diagnostics, and non-fatal warnings. Use reports for assertion failures, fatal startup errors, shutdown problems, or any path where the diagnostic must not wait behind the async queue.

## Core rules

- Call `Logger::init(...)` before normal runtime logging.
- Call `Logger::shutdown()` during application teardown; it is idempotent.
- `Logger::fatal(...)` is only a fatal-severity normal log. It does not terminate and does not force a popup.
- `Logger::report(...)` writes synchronously, bypasses filters and the async queue, mirrors to platform debug output when enabled, and flushes.
- `Logger::fatalTerminate(...)` is the terminating convenience path.
- Logger macros in `logger/logger_macros.h` guard `shouldLog(...)` before evaluating message or format arguments.

## Guide pages

- @ref logger_quick_start
- @ref logger_configuration
- @ref logger_reports
- @ref logger_stats
- @ref logger_testing
- @ref logger_examples

## API reference

The main public API is the `GameWIP::Logger` class in `logger/logger.h`. The optional lazy macro API is in `logger/logger_macros.h`.
