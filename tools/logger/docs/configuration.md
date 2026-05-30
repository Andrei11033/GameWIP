@page logger_configuration Logger configuration

Logger configuration controls output sinks, queue limits, message storage behavior, filtering defaults, platform debug output, and fatal popup behavior. `Logger::init(...)` copies the configuration. Mutating the original `Config` object after initialization has no effect.

## Built-in configurations

- `Logger::defaultConfig()` is the normal general-purpose configuration.
- `Logger::lowMemoryConfig()` reduces retained memory and is useful for small test runs or constrained tools.
- `Logger::throughputConfig()` favors high-volume logging by increasing reuse and batching.

## Output modes

`Types::Output` controls normal async sinks:

- `None`: no normal sink output.
- `Console`: write to stdout/stderr.
- `File`: write to the configured log directory/file.
- `Both`: write to console and file.

When file setup fails, `fallbackToConsoleOnFileFailure` can keep diagnostics visible by falling back to console output. `getLastResult()` and `getLastPlatformError()` expose the most recent setup or platform failure.

## Queue and message limits

`maxQueueSize` is the soft queue limit. Low-priority messages may drop at this point. The hard limit is derived from `hardQueueMultiplier`; every severity may drop at the hard limit.

`maxMessageLength` bounds stored message text. Long messages are truncated with a visible suffix and counted in `Stats::truncated`.

`inlineMessageCapacity`, `workerBatchSize`, `releaseMessageMemoryAfterWrite`, and `releaseStorageOnShutdown` control the memory/performance tradeoff. For normal builds, prefer the built-in configs unless a test or tool has a specific reason to tune them.

## Source and level filters

Startup filters are copied from `Config::sourceFilters` and `Config::levelFilters`. Runtime filters can be changed with:

```cpp
GameWIP::Logger::setSourceFilter(LogSource::Renderer, false);
GameWIP::Logger::setLevelFilter(GameWIP::Logger::Types::Level::Debug, false);
```

Filtering rules:

- `minLevel` is a startup severity floor. Runtime level filters cannot re-enable severities below it.
- Source filters affect registered `SourceId` / enum sources only.
- String sources are filtered by level only.
- Unknown `SourceId` values are allowed and written as `UnknownSource`; they are counted in `Stats::unknownSourceUses`.
- Filtered normal logs are intentional skips and do not count as queue drops.

## Formatting policy

Compile-time checked overloads use `std::format_string`. Dynamic format strings must be wrapped explicitly with `Logger::runtimeFormat(...)` so runtime formatting is visible at the call site. Formatting happens on the caller thread before queue insertion.
