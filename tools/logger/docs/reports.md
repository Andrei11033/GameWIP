@page logger_reports Reports

Reports are Logger's synchronous emergency path for assertions, fatal failures, failed initialization, and crash-adjacent diagnostics. They are not async-signal-safe.

Each report bypasses normal filters and queue pressure, builds the diagnostic on the caller thread, attempts every eligible normal sink, mirrors to debug output when enabled, flushes active normal sinks, and for `reportFatal()` attempts the fatal popup when enabled. One failed channel never prevents attempts to remaining eligible channels.

Reports intentionally do **not** drain older asynchronous queue entries. Use `flush()` separately when preserving the backlog is desired.

Timed overloads take `std::chrono::milliseconds` before message/format text. `0ms` is no-wait, positive values bound Logger-owned waits, and negative durations return `InvalidArgument`. Native operations already entered cannot be cancelled.

`ReportResult::delivery` describes enabled emergency channels: `None`, `Partial`, or `Complete`. Delivery and flush completion are separate: a report can be delivered and then encounter a flush failure. A successful flush reports only the guarantee provided by the underlying IO flush mode; it does not claim physical-media durability. `status` carries the first real operation failure while other channels are still attempted. `outcome` independently reports `Completed` or `TimedOut`.

`reportError()` fixes severity to Error. `reportFatal()` fixes severity to Fatal and owns popup behavior. `fatalTerminate()` performs the fatal report attempt and then calls `std::terminate()` regardless of report result.
