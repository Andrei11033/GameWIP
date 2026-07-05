@page logger_troubleshooting Logger troubleshooting

## Logs do not appear

Likely causes:

- logger was not initialized,
- message is below `minLevel`,
- source or level filter suppresses it,
- asynchronous queue has not flushed yet,
- file output failed and fallback is disabled.

Check:

```cpp
GameWIP::Logger::isRunning();
GameWIP::Logger::getLastResult();
GameWIP::Logger::getStats();
```

## The log file is empty

Normal logs are asynchronous. Call `flush()` or `shutdown()` before checking the file.

```cpp
GameWIP::Logger::flush();
```

## Logs are dropped

Drops mean queue pressure, not filtering. Check queue limits and stress volume. Consider a larger configuration, lower log volume, source filters, or fewer expensive debug logs in hot paths.

## Reports appear but normal logs do not

Reports bypass normal filters and the asynchronous queue. If reports work but normal logs do not, inspect `minLevel`, source filters, level filters, and queue/drop statistics.

## Flush times out

A timed flush returning false means the bounded wait expired. It does not automatically mean every log was lost. Stop producers before final shutdown when possible.

## Fatal log did not show a popup

`Logger::fatal(...)` is a fatal-severity asynchronous log. Use `reportFatal(...)` or `fatalTerminate(...)` for the synchronous fatal report and popup path.

## UnknownSource appears

A registered-source overload received a `SourceId` that was not present in `Config::sources` during init. The message is intentionally written with the fallback label instead of being dropped. Check `Stats::unknownSourceUses` and the source registration table.

## Filtered logs look like drops

Filtered logs are intentional skips and should not increment drop counters. Queue drops mean Logger accepted too much asynchronous work for the configured queue limits.

## Related pages

- @ref logger_lifecycle
- @ref logger_reports
- @ref logger_stats
