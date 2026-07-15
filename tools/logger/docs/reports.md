@page logger_reports Logger reports

Reports are the synchronous diagnostic path. Use them when a message must bypass runtime filtering and queue pressure.

## Core behavior

Every report:

1. bounds/formats the message on the calling thread;
2. acquires Logger's lifecycle serialization boundary;
3. writes directly to each active normal sink;
4. mirrors to platform debugger output when enabled;
5. flushes active sinks;
6. optionally shows the logger-owned fatal popup.

Reports bypass `minLevel`, runtime level filters, source filters, and the asynchronous queue. They can block and should not be used as routine high-frequency logging.

## Untimed versus timed reports

An untimed report writes and flushes the report itself but does not wait for older asynchronous queue entries to drain:

```cpp
Logger::reportError("Startup", "Configuration is invalid");
```

A timed report writes and flushes the report first. If that initial observable sink flush succeeds, it then attempts a timeout-parameterized drain of accepted asynchronous work and another sink flush:

```cpp
const bool drained = Logger::reportError(
    "Startup",
    Logger::flushTimeout(std::chrono::milliseconds{250}),
    "Configuration is invalid");
```

The returned `bool` describes those observable sink flushes and the queue drain. If the initial file flush fails, the current implementation returns false without attempting the queue drain. Otherwise, the timeout value bounds only the queue condition wait after internal serialization; lock acquisition and synchronous sink operations can extend the total call beyond it. Logger requests console flushes, but console-flush status is not reflected in the boolean. The value is not a delivery receipt for the report line and does not indicate whether a normal sink accepted that line.

## Generic report overloads

`report()` accepts:

- an explicit `Level`;
- a string source, `SourceId`, or valid source enum;
- preformatted, compile-time checked, or runtime format text;
- optional `FlushTimeout`;
- optional `ReportPopup`.

`ReportPopup::Fatal` requests the popup after flushing, subject to `Config::enableFatalPopup`. It does not require `Level::Fatal`, although pairing it with a fatal diagnostic is the normal use.

## Convenience paths

| API | Behavior |
| --- | --- |
| `reportError()` | Reports at `Error` without requesting a popup. |
| `reportFatal()` | Reports at `Fatal` and requests the fatal popup. |
| `fatalTerminate()` | Performs the fatal report path, then calls `std::terminate()`. |

`fatalTerminate()` is `[[noreturn]]`. It does not provide normal stack unwinding. The untimed form does not drain older queued entries; the timeout form attempts a queue drain when the initial observable sink flush succeeds before termination proceeds.

## Ordering and concurrency

The report line is synchronous relative to its caller. Reports do not establish a global ordering barrier with concurrent producers. Other threads can continue to enqueue normal records before or after the report while the runtime accepts them.

Because untimed reports do not drain the asynchronous queue, an older normal record can appear in a sink after the report. Use a timed report or an explicit `flush()` when a best-effort ordering boundary is required.

## Disabled or failed normal sinks

Reports cannot create a missing normal sink. With effective `Output::None`, no console/file report is written. Debugger mirroring and popup attempts remain separate configured channels.

Report APIs generally return no per-sink status. Use statistics and `getLastPlatformError()` for diagnostics. The timed `bool` remains a drain/flush result, not a sink-acceptance result.

## Related pages

- @ref logger_output
- @ref logger_lifecycle
- @ref logger_stats
- @ref logger_troubleshooting
