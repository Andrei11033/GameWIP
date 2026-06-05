@page logger_public_api Logger public API

Include `logger/logger.h` for the namespace API. Include `logger/logger_macros.h` only where the optional global `LOGGER_*` macros are wanted.

## Choosing an output path

Normal log calls are asynchronous, filterable, and queue-backed. They are intended for regular runtime diagnostics. Formatting occurs on the producer thread before queue insertion.

Reports are synchronous, bypass filters and queue pressure, and write to active sinks before returning. Bounded report overloads return whether the subsequent drain completed; a false result does not mean the report line was skipped.

Use `fatalTerminate()` only when the diagnostic must be followed by process termination. `fatal()` is a normal log at Fatal severity and does not terminate.

The lazy macros guard message and argument evaluation with `shouldLog()`. Prefer them, or an explicit guard, when constructing a filtered message is expensive.

## Sources and formats

Registered `SourceId` or unsigned enum sources support source filtering and avoid repeated dynamic source handling. String sources are convenient but are filtered only by severity.

Compile-time format overloads are preferred. Wrap genuinely dynamic format text with `runtimeFormat()`; invalid runtime formats are counted and skipped rather than escaping from the logging path.

## Lifecycle and diagnostics

`init()` copies the configuration data it retains. Lifecycle calls must be externally serialized; producer logging calls are safe from multiple threads while the logger is running.

`flush(timeout)` may fail to finish while producers continue submitting work or a sink remains blocked. `shutdown()` drains accepted work, stops the worker, and closes sinks.

Statistics and memory snapshots are diagnostic observations, not gameplay state. Filtered calls are intentional skips and are not queue drops.

See @ref logger_lifecycle, @ref logger_configuration, @ref logger_reports, @ref logger_threading_performance, and @ref logger_stats for detailed behavior.
