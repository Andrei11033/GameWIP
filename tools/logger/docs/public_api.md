@page logger_public_api Public API

Include `logger/logger.h`. Installed consumers link `GameWIP::Logger`; source-tree consumers link `Logger`. Include `logger/logger_macros.h` only for the optional global macros. See @ref logger_quick_start for complete CMake usage.

## Public types

`GameWIP::Logger::Types` contains the passive configuration and result shapes:

| Type | Contract |
| --- | --- |
| `Level` | `Trace`, `Debug`, `Info`, `Warn`, `Error`, and `Fatal` severity values. |
| `Output` | Normal sink selection: `None`, `Console`, `File`, or `Both`. |
| `FormatPolicy` | `StrictBounded` or `FastNormal` caller-thread formatting policy. |
| `Result` | Initialization, filter-update, and runtime diagnostic result category. |
| `PlatformErrorSource` | Native-error origin: `None`, `DebugOutput`, `FatalPopup`, `TimeConversion`, or `File`. |
| `SourceId` | `std::uint32_t` key used for registered source names and source filters. |
| `SourceDefinition` | Source ID/name pair with fields `id` and `name`, copied during initialization. |
| `SourceFilter` | Registered `source` ID plus its initial or runtime `enabled` state. |
| `LevelFilter` | Exact `level` plus its initial or runtime `enabled` state. |
| `RuntimeFormat` | Explicit wrapper whose `text` field holds a format string known only at runtime. |
| `FlushTimeout` | Best-effort end-to-end deadline wrapper for timed report APIs; already-started native sink I/O is not cancellable. |
| `ReportPopup` | Selects no popup or the logger-owned fatal popup for a report. |
| `PlatformError` | Snapshot with `source` and backend-specific `nativeCode` fields. |
| `Config` | Complete startup configuration. |
| `QueueLimits` | Effective `softQueueSize`, `hardQueueSize`, `hardQueueMultiplier`, `maxMessageLength`, `inlineMessageCapacity`, and `workerBatchSize`. |
| `Stats` | Resettable queue, sink, failure, and truncation counters. |
| `MemoryStats` | Best-effort retained Logger storage and process-memory snapshot. |

@ref logger_configuration owns every `Config` field and preset. @ref logger_stats owns counter meanings.

### `Result` values

| Value | Meaning |
| --- | --- |
| `Success` | The operation completed without a recorded validation issue or fallback. |
| `AlreadyRunning` | Initialization was requested while an asynchronous Logger runtime was already active. |
| `InvalidOutputMode` | `Config::output` contained an undefined `Output` value. |
| `InvalidQueueSize` | Queue settings were invalid, sanitized, or could not be allocated as requested. Logger may still start with effective fallback limits. |
| `InvalidMessageLength` | A zero message limit was replaced with the runtime fallback. Logger may still start. |
| `InvalidLogDirectory` | The requested file-output path could not be accepted as a directory selection. |
| `InvalidSourceDefinition` | A source definition was empty, duplicated, or otherwise invalid. |
| `InvalidSourceFilter` | An initial or runtime source filter referenced an unregistered source. |
| `InvalidLevelFilter` | A minimum level or exact-level filter contained an undefined severity. |
| `FileOpenFailed` | No collision-safe file candidate could be opened. |
| `FileWriteFailed` | A runtime file write or flush failed. |
| `FileSetupFailed` | File-path conversion or directory preparation failed. |
| `ThreadStartFailed` | The asynchronous worker could not be started. |
| `PlatformCallFailed` | A time-conversion, debugger-output, popup, or other platform operation failed. |

Some initialization results describe a fallback or sanitized configuration rather than a completely unavailable logger. @ref logger_configuration explains how to inspect the effective state.

## Helper functions

| API | Purpose |
| --- | --- |
| `defineSource(enumValue, name)` | Creates a `SourceDefinition` for an unsigned source enum. |
| `runtimeFormat(text)` | Marks dynamic text as a runtime format string. |
| `flushTimeout(duration)` | Creates the explicit timeout wrapper used by report overloads. |

## Lifecycle and inspection

| Family | APIs |
| --- | --- |
| Configuration factories | `defaultConfig()`, `lowMemoryConfig()`, `throughputConfig()` |
| Initialization | `init()`, `initDefault()`, `initConsole()`, `initFile()` |
| Finalization | `flush()`, `flush(timeout)`, `shutdown()` |
| Runtime state | `isRunning()`, `getMinLevel()`, `getOutput()`, `getLogFilePath()`, `getQueueLimits()` |
| Diagnostics | `getLastResult()`, `getLastPlatformError()`, `getStats()`, `getMemoryStats()`, `getLifetimeDroppedLogCount()`, `resetStats()` |

@ref logger_lifecycle defines state transitions, result interpretation, and synchronization.

## Runtime filters

`shouldLog()` has severity-only, string-source, `SourceId`, and enum-source forms. String sources use severity filtering only. Registered sources additionally use source filters.

Filter mutation is provided by:

- `setSourceFilter()`, `clearSourceFilter()`, and `clearSourceFilters()`;
- `setLevelFilter()`, `clearLevelFilter()`, and `clearLevelFilters()`.

Initial filters are supplied through `Config::sourceFilters` and `Config::levelFilters`. Filtered records are intentional skips and do not count as queue drops.

## Normal logging

The normal logging surface has three message forms:

| Form | Example | Notes |
| --- | --- | --- |
| Preformatted | `info(source, message)` | Message bytes already exist. |
| Compile-time format | `info(source, "value {}", value)` | Preferred formatted form; format syntax is checked by `std::format_string`. |
| Runtime format | `info(source, runtimeFormat(text), value)` | Use only when format text cannot be known at compile time. |

Each form supports a string source, `SourceId`, or valid source enum. The generic `log(level, ...)` overloads accept an explicit severity; `trace()`, `debug()`, `info()`, `warn()`, `error()`, and `fatal()` provide fixed-severity convenience families.

Normal logging is asynchronous, filterable, and queue-backed. `fatal()` is only a normal `Fatal`-severity record; it neither shows a popup nor terminates the process.

@ref logger_messages_sources owns overload selection, source constraints, message copying, formatting, and truncation. @ref logger_threading_performance owns queue behavior.

## Reports and termination

The report surface includes:

- generic `report()` overloads;
- `reportError()` and `reportFatal()` convenience families;
- optional `FlushTimeout` and `ReportPopup` selections;
- preformatted, compile-time-format, runtime-format, string-source, `SourceId`, and enum-source forms;
- `fatalTerminate()` overloads, which end in `std::terminate()`;
- `writeDebugOutput()`, which targets only the platform debugger channel.

Reports are synchronous, bypass runtime filters and the asynchronous queue, and are not equivalent to normal logging. See @ref logger_reports.

## Optional global macros

`logger/logger_macros.h` defines `LOGGER_TRACE`, `LOGGER_DEBUG`, `LOGGER_INFO`, `LOGGER_WARN`, `LOGGER_ERROR`, `LOGGER_FATAL`, and `LOGGER_FATAL_TERMINATE`. See @ref logger_macros for compile-out and evaluation rules.

## Package and ABI boundary

Logger is installed as a shared library. Its supported package boundary includes the two public headers and generated export header. Exported `Detail::Core` functions support public templates but are not consumer APIs. See @ref logger_abi.

## Related pages

- @ref logger_configuration
- @ref logger_lifecycle
- @ref logger_messages_sources
- @ref logger_output
- @ref logger_reports
- @ref logger_stats
