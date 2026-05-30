@page logger_troubleshooting Logger troubleshooting

## My logs do not appear

Likely causes:

- logger was not initialized,
- message is below `minLevel`,
- source or level filter suppresses it,
- async queue has not flushed yet,
- file output failed and fallback is disabled.

Check:

```cpp
GameWIP::Logger::isRunning();
GameWIP::Logger::getLastResult();
GameWIP::Logger::getStats();
```

## My file is empty

Normal logs are asynchronous. Call `flush()` or `shutdown()` before checking the file.

```cpp
GameWIP::Logger::flush();
```

## I get dropped logs

Drops mean queue pressure, not filtering. Check queue limits and stress volume. Consider a larger config, lower log volume, source filters, or using fewer expensive debug logs in hot paths.

## Reports appear but normal logs do not

Reports bypass normal filters and the async queue. If reports work but normal logs do not, inspect `minLevel`, source filters, level filters, and queue/drop stats.

## Flush times out

A timed flush returning false means the bounded wait expired. It does not automatically mean every log was lost. Stop producers before final shutdown when possible.

## Fatal log did not show a popup

`Logger::fatal(...)` is a fatal-severity async log. Use `reportFatal(...)` or `fatalTerminate(...)` for the synchronous fatal report/popup path.

## UnknownSource appears

A registered-source overload received a `SourceId` that was not present in `Config::sources` during init. The message is intentionally written with the fallback label instead of being dropped. Check `Stats::unknownSourceUses` and the source registration table.

## Filtered logs look like drops

Filtered logs are intentional skips and should not increment drop counters. Queue drops mean the logger accepted too much async work for the configured queue limits.

## Related pages

- @ref logger_lifecycle
- @ref logger_reports
- @ref logger_stats
