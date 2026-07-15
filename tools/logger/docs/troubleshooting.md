@page logger_troubleshooting Logger troubleshooting

## `init()` returned an error but logging still works

Queue/message options may have been sanitized, or file output may have fallen back to console. Check:

```cpp
Logger::isRunning();
Logger::getOutput();
Logger::getQueueLimits();
Logger::getLastResult();
Logger::getLastPlatformError();
```

Do not interpret every non-`Success` result as an all-or-nothing startup failure.

## `init()` returned `Success` but `isRunning()` is false

`Output::None` is a successful disabled configuration and does not start the asynchronous worker.

## Normal logs do not appear

Check the effective output, startup `minLevel`, runtime level filters, source filters for registered IDs, queue-drop counters, and final flush/shutdown. Normal calls made before initialization or after shutdown are not persistent logging guarantees.

## Reports appear but normal logs do not

Reports bypass normal filters and queue pressure. Inspect `getOutput()`, `isRunning()`, filters, and `Stats::queueDropsSoft` / `queueDropsHard`.

## File output became console output

File setup failed while fallback was allowed. Inspect `getLastResult()`, `getLastPlatformError()`, `getOutput()`, and `getLogFilePath()`.

## The file is empty or missing recent records

Normal records are asynchronous and file writes are batched. Call `flush()` before inspecting during tests, or `shutdown()` during final teardown. Also check `fileWriteFailures`.

## Error or Fatal normal logs were dropped

The hard queue limit applies to every normal severity. Use a synchronous report for a diagnostic that must bypass queue capacity.

## A filtered macro still evaluated the source

Active `LOGGER_*` normal macros evaluate the source expression once before `shouldLog()`. Only message/format arguments are guarded by the first filter check. Compiled-out Trace/Debug macros evaluate neither.

## A direct formatted call evaluated expensive work

C++ evaluates function arguments before Logger checks filters. Use a lazy macro or explicit `shouldLog()` guard.

## `UnknownSource` appears

A `SourceId`/enum value was not present in `Config::sources` for the active initialization. Unknown IDs are intentionally accepted. Check `unknownSourceUses` and the registration table.

## `flush(timeout)` or a timed report returned false

The best-effort deadline expired during Logger-owned locking/draining, or an observable file flush failed. Producers may still be active or the worker may still have queued work. Timed reports attempt the later queue drain even when the initial flush fails. Native sink I/O that already started is not cancellable and may outlast the deadline.

## Fatal logging did not show a popup or terminate

`fatal()` and `LOGGER_FATAL` are normal asynchronous logs. Use `reportFatal()` for the popup-report path or `fatalTerminate()` / `LOGGER_FATAL_TERMINATE` for termination. Popup behavior also requires `enableFatalPopup`.

## Debugger output appears with `Output::None`

Normal sink output and platform debugger output are separate channels. Set `enableDebugOutput = false` to disable debugger writes.

## Runtime format text produced no record

Invalid runtime formatting is contained, increments `formatFailures`, and skips the operation. Prefer compile-time checked format strings when possible.

## Related pages

- @ref logger_configuration
- @ref logger_lifecycle
- @ref logger_messages_sources
- @ref logger_reports
- @ref logger_stats
