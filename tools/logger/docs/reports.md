@page logger_reports Logger reports

`Logger::report(...)` is the synchronous emergency diagnostic path. It is intentionally separate from normal async logging.

A report:

- bypasses `minLevel`, runtime level filters, and source filters;
- bypasses the async queue;
- writes directly to active sinks;
- mirrors to platform debug output when enabled;
- flushes sinks before returning;
- does not terminate the process by itself.

Reports are appropriate for assertion failures, fatal startup failures, save/load failures that must be visible immediately, and shutdown diagnostics.

## Ordering

A report does not guarantee ordering relative to older async queue entries. It writes immediately rather than waiting behind the queue. Use `flush()` before the report if strict ordering with earlier normal logs matters.

## Timeout overloads

Timeout overloads write the report first, then attempt a bounded async queue/sink drain:

```cpp
const bool drained = GameWIP::Logger::report(
    GameWIP::Logger::Types::Level::Error,
    "SaveSystem",
    GameWIP::Logger::flushTimeout(std::chrono::milliseconds{250}),
    "Save failed");
```

A `false` return means the bounded drain/flush did not complete; it does not mean the report line itself was skipped.

## Fatal helpers

- `reportError(...)` reports at Error severity without a fatal popup.
- `reportFatal(...)` reports at Fatal severity and can use the logger-owned fatal popup when enabled.
- `fatalTerminate(...)` reports, flushes, may show the fatal popup, then terminates.

Normal `Logger::fatal(...)` remains a queue-based fatal-severity log only.
