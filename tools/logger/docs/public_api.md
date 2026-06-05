@page logger_public_api Logger public API guide

This page explains how the Logger public API fits together and when to choose each API.

## Include files

Use the namespace module API through:

```cpp
#include "logger/logger.h"
```

Use the optional lazy global macros through:

```cpp
#include "logger/logger_macros.h"
```

The namespace module API is the primary API. The macro header is opt-in and should be included only where the global `LOGGER_*` convenience macros are wanted.

## API family map

| Family | Public APIs | Primary behavior |
| --- | --- | --- |
| Source/format helpers | `sourceId`, `defineSource`, `runtimeFormat`, `flushTimeout` | Small wrappers used by registered sources, dynamic format strings, and bounded report flushing. |
| Lifecycle | `defaultConfig`, `lowMemoryConfig`, `throughputConfig`, `init`, `initDefault`, `initConsole`, `initFile`, `shutdown`, `flush` | Configure, start, drain, and stop the process-wide logger. |
| Inspection | `isRunning`, `getMinLevel`, `getOutput`, `getLogFilePath`, `getQueueLimits`, `getLifetimeDroppedLogCount`, `getLastResult`, `getLastPlatformError`, `getStats`, `getMemoryStats`, `resetStats` | Query runtime state, diagnostics, counters, and retained memory. |
| Filters | `shouldLog`, `setSourceFilter`, `clearSourceFilter`, `clearSourceFilters`, `setLevelFilter`, `clearLevelFilter`, `clearLevelFilters` | Check and change runtime level/source filtering. |
| Normal logs | `log`, `trace`, `debug`, `info`, `warn`, `error`, `fatal` | Asynchronous, queue-backed, filterable log entries. |
| Reports | `report`, `reportError`, `reportFatal`, `fatalTerminate` | Synchronous diagnostics that bypass filters and queue pressure. |
| Debug output | `writeDebugOutput` | Direct platform-debug-output mirror only; does not write normal sinks. |
| Lazy macros | `LOGGER_TRACE`, `LOGGER_DEBUG`, `LOGGER_INFO`, `LOGGER_WARN`, `LOGGER_ERROR`, `LOGGER_FATAL`, `LOGGER_FATAL_TERMINATE` | Optional global macros that guard expensive arguments with `shouldLog()`. |

## Public type groups

`GameWIP::Logger::Types` contains configuration, reporting, and statistics shapes. Keeping them under `Types` keeps the public namespace compact and discoverable in IntelliSense.

### Severity and output

| API | Purpose |
| --- | --- |
| `Types::Level` | Log severity: Trace, Debug, Info, Warn, Error, Fatal. Used by min-level filtering, level filters, normal logs, and reports. |
| `Types::Output` | Active sink set: None, Console, File, or Both. |
| `Types::FormatPolicy` | Controls formatted-message memory behavior before queueing. |
| `Types::ReportPopup` | Controls whether a synchronous report requests the logger-owned fatal popup. |

`Fatal` is only a severity. A normal `Logger::fatal(...)` call does not terminate the process and does not force the fatal popup. Use `reportFatal(...)` or `fatalTerminate(...)` when the failure path needs synchronous reporting, optional popup behavior, or process termination.

### Sources and filters

| API | Purpose |
| --- | --- |
| `Types::SourceId` | Stable numeric source key used for hot-path registered sources. |
| `Types::SourceDefinition` | Maps a `SourceId` to a copied display name during `init()`. |
| `Types::SourceFilter` | Initial or runtime source on/off state for registered sources. |
| `Types::LevelFilter` | Initial or runtime on/off state for one exact level. |

Use registered `SourceId` or enum sources for hot code and source filtering. String sources are useful for simple diagnostics, but they are not affected by source filters.

### Results, errors, and counters

| API | Purpose |
| --- | --- |
| `Types::Result` | Result of `init()` and runtime filter changes. |
| `Types::PlatformErrorSource` | Platform subsystem that produced the last native error. |
| `Types::PlatformError` | Last relevant native/platform error. |
| `Types::Stats` | Runtime counters for accepted, written, dropped, diagnostic, unknown-source, and truncation events. |
| `Types::MemoryStats` | Retained memory diagnostics for logger-owned buffers. |
| `Types::QueueLimits` | Effective queue sizing and pressure limits. |

Stats are diagnostic tools, not gameplay data. Queue-drop counters mean queue pressure only. Filtered logs are intentional skips and are not drops.

## Source helpers

### `sourceId(enumValue)`

Converts an enum source to `Types::SourceId`. Use unsigned enum values that fit in `SourceId`.

```cpp
enum class LogSource : GameWIP::Logger::Types::SourceId
{
    Game = 1,
    Physics = 2,
};

const auto id = GameWIP::Logger::sourceId(LogSource::Physics);
```

### `defineSource(enumValue, name)`

Creates a source definition for `Config::sources`. The logger copies source names during `init()`.

```cpp
const std::array sources{
    GameWIP::Logger::defineSource(LogSource::Game, "Game"),
    GameWIP::Logger::defineSource(LogSource::Physics, "Physics"),
};
```

### `runtimeFormat(format)`

Wraps a format string that is known only at runtime. Prefer compile-time checked `std::format_string` overloads whenever possible.

### `flushTimeout(milliseconds)`

Wraps a report timeout. Report timeout overloads write the report first, then perform a bounded drain/flush.

## Lifecycle API

| API | Use when |
| --- | --- |
| `defaultConfig()` | Normal starting point for most runs. |
| `lowMemoryConfig()` | Smaller retained queue/message memory matters more than throughput. |
| `throughputConfig()` | Higher-volume logging and reuse matter more than low retained memory. |
| `init(config)` | You have a prepared config. |
| `initDefault()` | You want the default config as-is. |
| `initConsole(minLevel)` | You want a quick console logger. |
| `initFile(directory, minLevel)` | You want a quick file logger. |
| `shutdown()` | You are done logging and want accepted work drained and sinks closed. |
| `flush()` | You need accepted queued work drained and sinks flushed. |
| `flush(timeout)` | You need a bounded drain attempt. |

`init()` copies the configuration data it needs. Mutating the original config object after init does not reconfigure the logger.

`flush(timeout)` can return false if producers keep adding work or a sink cannot complete before the bounded wait. A false return from report timeout overloads means the bounded drain did not finish; it does not mean the report line was skipped.

## Runtime inspection API

| API | Meaning |
| --- | --- |
| `isRunning()` | Whether a runtime logger is currently active. |
| `getMinLevel()` | Startup severity floor. |
| `getOutput()` | Effective active output mode. |
| `getLogFilePath()` | Active log file path when file output is used. |
| `getQueueLimits()` | Effective queue capacity and pressure limits. |
| `getLifetimeDroppedLogCount()` | Lifetime queue-pressure drops; not reset by `resetStats()`. |
| `getLastResult()` | Last important logger result. |
| `getLastPlatformError()` | Last relevant platform error. |
| `getStats()` | Current runtime counters. |
| `getMemoryStats()` | Current memory diagnostics. |
| `resetStats()` | Clears resettable counters while preserving lifetime drop count. |

Use these APIs for diagnostics, tests, tools, and failure investigation. Avoid using them as gameplay control signals.

## Filtering API

| API | Meaning |
| --- | --- |
| `shouldLog(level)` | Checks startup min-level and runtime level filters. |
| `shouldLog(level, stringSource)` | Checks severity filters for a string source. |
| `shouldLog(level, SourceId)` | Checks severity filters and registered source filters. |
| `setSourceFilter(source, enabled)` | Enables/disables a registered source. |
| `clearSourceFilter(source)` | Removes a runtime filter for one source. |
| `clearSourceFilters()` | Removes all runtime source filters. |
| `setLevelFilter(level, enabled)` | Enables/disables one exact severity. |
| `clearLevelFilter(level)` | Removes one runtime level filter. |
| `clearLevelFilters()` | Removes all runtime level filters. |

Convenience logging macros call `shouldLog()` before evaluating message arguments. This is the preferred way to avoid expensive message construction in filtered hot paths.

## Normal logging API

Normal logging APIs are asynchronous, queue-based, filterable, and intended for regular runtime information.

### Normal log overload matrix

| Source form | Message form | API shape | Filtering | Blocking/return |
| --- | --- | --- | --- | --- |
| String source | Preformatted text | `log(level, sourceText, message)` and severity helpers | Level filters only; string sources are not source-filtered. | Non-blocking producer path; returns `void`. |
| Registered `SourceId` | Preformatted text | `log(level, sourceId, message)` and severity helpers | Level filters and registered source filters. | Non-blocking producer path; returns `void`. |
| Enum source | Preformatted text | Template overloads where the enum has an unsigned underlying type | Converted to `SourceId`, then filtered like registered sources. | Non-blocking producer path; returns `void`. |
| Any supported source | Compile-time checked format | `log(level, source, "value {}", value)` and severity helpers | Same as the selected source form. | Formats on the caller thread before queue insertion; returns `void`. |
| Any supported source | Runtime format | `log(level, source, runtimeFormat(text), args...)` and severity helpers | Same as the selected source form. | Formats on the caller thread; invalid runtime formats are counted and skipped. |

Generic form:

```cpp
Logger::log(Logger::Types::Level::Info, source, "message");
Logger::log(Logger::Types::Level::Info, source, "loaded {} assets", assetCount);
Logger::log(Logger::Types::Level::Info, source, Logger::runtimeFormat(formatText), value);
```

Severity helpers:

```cpp
Logger::trace(source, "...");
Logger::debug(source, "...");
Logger::info(source, "...");
Logger::warn(source, "...");
Logger::error(source, "...");
Logger::fatal(source, "...");
```

Every severity helper has string-source, `SourceId`, enum-source, compile-time format, and runtime format forms. Use compile-time format overloads when the format text is known at compile time. Use `runtimeFormat()` only for dynamic format text.

Direct formatted calls cannot prevent normal C++ argument evaluation before the function call. Use `LOGGER_*` macros or an explicit `shouldLog()` guard when building arguments is expensive.

## Synchronous report API

Reports are for important diagnostics that must be written immediately. They bypass level/source filters and the async queue.

### Report overload matrix

| Source form | Message form | Popup option | Timeout option | Return |
| --- | --- | --- | --- | --- |
| String source | Preformatted, compile-time format, or runtime format | Optional `Types::ReportPopup` on generic `report` | Optional `flushTimeout(...)` overloads | Blocking forms return `void`; bounded forms return whether the post-report drain/flush completed. |
| Registered `SourceId` | Preformatted, compile-time format, or runtime format | Optional `Types::ReportPopup` on generic `report` | Optional `flushTimeout(...)` overloads | Same as string source. |
| Enum source | Compile-time format or runtime format | Optional `Types::ReportPopup` on generic `report` | Optional `flushTimeout(...)` overloads | Same as `SourceId` after enum conversion. |
| Convenience error | Compile-time format or runtime format | No popup | Optional `flushTimeout(...)` overloads | `reportError` writes Error severity immediately. |
| Convenience fatal | Compile-time format or runtime format | Fatal popup requested when enabled | Optional `flushTimeout(...)` overloads | `reportFatal` writes Fatal severity immediately and may request UI. |
| Terminating fatal | Compile-time format or runtime format | Fatal popup requested when enabled | Optional `flushTimeout(...)` overloads | `fatalTerminate` does not return. |

Generic report forms:

```cpp
Logger::report(Logger::Types::Level::Error, "Save", "could not write save file");
Logger::report(Logger::Types::Level::Fatal, "Startup", Logger::Types::ReportPopup::Fatal, "critical startup failure");
const bool drained = Logger::report(Logger::Types::Level::Error, "Save", Logger::flushTimeout(250ms), "failed");
```

Convenience report forms:

```cpp
Logger::reportError("Save", "could not write save file");
Logger::reportFatal("Startup", "critical startup failure");
Logger::fatalTerminate("Startup", "cannot continue");
```

Use reports for assertions, startup failures, shutdown failures, and diagnostics that must survive queue pressure. Do not use reports as high-frequency normal logging; they are intentionally synchronous and may block.

`writeDebugOutput(level, source, message)` is separate from reports. It writes only to the platform debug output path when debug output is enabled, and it does not write to console/file sinks.

## Lazy macros

`logger/logger_macros.h` provides:

- `LOGGER_TRACE`
- `LOGGER_DEBUG`
- `LOGGER_INFO`
- `LOGGER_WARN`
- `LOGGER_ERROR`
- `LOGGER_FATAL`
- `LOGGER_FATAL_TERMINATE`

The macros are global by design, opt-in by include, and useful when message construction is expensive. `LOGGER_TRACE` and `LOGGER_DEBUG` can compile out in release-style builds unless their enable macros are defined.

## Choosing the right API

| Situation | Prefer |
| --- | --- |
| Normal runtime information | `info`, `debug`, `warn`, etc. |
| Expensive message construction on a filtered path | `LOGGER_*` macro |
| Dynamic format string | `runtimeFormat()` overload |
| Important diagnostic that must be immediate | `report()` |
| Fatal diagnostic that should request the logger popup | `reportFatal()` or report with `ReportPopup::Fatal` |
| Fatal diagnostic that must terminate | `fatalTerminate()` |
| Tests/tools inspecting logger behavior | `getStats()`, `getMemoryStats()`, `getLastResult()` |

## Related pages

- @ref logger_quick_start
- @ref logger_lifecycle
- @ref logger_configuration
- @ref logger_reports
- @ref logger_stats
- @ref logger_examples
