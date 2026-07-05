@page logger_configuration Logger configuration

Logger configuration controls output sinks, queue size, message storage behavior, filtering defaults, and fatal popup behavior.

## Built-in configurations

- `Logger::defaultConfig()` is the normal general-purpose configuration.
- `Logger::lowMemoryConfig()` reduces retained memory.
- `Logger::throughputConfig()` favors high-volume logging and reuse.

Each helper returns a `Logger::Types::Config` value that can be edited before `Logger::init(config)`. Logger copies the configuration it needs during initialization; later edits to the local configuration object do not reconfigure Logger.

## Output modes

`Config::output` controls the active sinks. The supported modes are `None`, `Console`, `File`, and `Both`.

File output uses `Config::logDirectory` as UTF-8 text. Its default is `logs`, resolved against the process current directory when Logger initializes; an empty value selects the same runtime default. Applications can choose another directory through `Config` or `initFile()` without rebuilding Logger. Logger converts paths through FileSystem, keeps one persistent `FileWriter`, and allows other processes to read the active log. If file setup fails and `fallbackToConsoleOnFileFailure` is true, startup can continue with console output and `getLastResult()` / `getLastPlatformError()` describe the file failure.

Console output goes through the shared Terminal runtime. `enableConsoleColor` requests portable severity colors for supported interactive terminals; redirected streams receive plain UTF-8 text.

## Filtering

`Config::minLevel` is the startup severity floor. `Config::sourceFilters` and `Config::levelFilters` apply initial runtime filters. Runtime APIs can then update filters:

```cpp
Logger::setLevelFilter(Logger::Types::Level::Debug, false);
Logger::setSourceFilter(Logger::sourceId(LogSource::AI), false);
Logger::clearLevelFilters();
Logger::clearSourceFilters();
```

Level and source filters apply to normal logs. String sources are severity-filtered only. Registered `SourceId` / enum sources can use both severity and source filters. Unknown `SourceId` values are accepted and written as `UnknownSource`; they increment `Stats::unknownSourceUses`.

Filtered normal logs are intentional skips. They must not count as dropped logs.

## Queue and message storage

`maxQueueSize`, `hardQueueLimitMultiplier`, `maxMessageLength`, `formatPolicy`, `preallocatedMessageBytes`, and `workerBatchSize` tune queue pressure, truncation, retained memory, and worker batching. Use @ref logger_threading_performance when changing these values.

## Fatal popup

`Config::enableFatalPopup` controls logger-owned fatal popup behavior used by report paths that request it. Normal `Logger::fatal(...)` does not request a popup and does not terminate the process.
