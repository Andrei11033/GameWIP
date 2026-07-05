@page logger_reports Logger reports

`Logger::report(...)` is the synchronous diagnostic path.

Reports are intended for important messages that should not wait behind the asynchronous queue. A report:

- bypasses level and source filters,
- bypasses the asynchronous queue,
- writes directly to active sinks,
- mirrors to platform debug output when enabled,
- flushes sinks before returning,
- does not terminate the process by itself.

`Logger::report(..., timeout, ...)` writes the report first, then attempts a bounded queue/sink drain. If the drain times out, the report line was still already written and the function returns false.

## Convenience APIs

- `reportError(...)` reports `Level::Error`, mirrors to platform debug output, and flushes.
- `reportFatal(...)` reports `Level::Fatal`, mirrors to platform debug output, flushes, and shows the fatal popup when enabled.
- `fatalTerminate(...)` reports fatal, flushes, optionally shows the fatal popup, then terminates the process.

Logger-owned fatal popups are only used by report/fatal-terminate paths that explicitly request or configure them.

## Popup policy

Use `Logger::Types::ReportPopup::Fatal` with `report(...)` when a generic report should request the logger-owned fatal popup. The popup may block and is not appropriate for frame hot paths.

## Ordering

Reports do not enter the asynchronous queue. They are written immediately relative to the calling thread, then Logger attempts to drain accepted queued work when the report overload requires a flush. Other producer threads can still enqueue normal logs while the runtime is accepting them, so report ordering is synchronous for the report line itself, not a global stop-the-world ordering guarantee.
