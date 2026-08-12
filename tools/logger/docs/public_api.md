@page logger_public_api Public API

Include `logger/logger.h` and link `GameWIP::Logger`. `logger/logger_macros.h` is opt-in.

## Types

`GameWIP::Logger::Types` is a real namespace. Important public vocabulary:

- `Level`: `Trace`, `Debug`, `Info`, `Warn`, `Error`, `Fatal`.
- `OutputMode`: `None`, `Console`, `File`, `Both`.
- `InitResult`: operation `status`, final `outcome`, adjustment bitmask, requested/effective output, and directly associated `outputSetupStatus`.
- `FlushResult`: IO `status` plus `Completed`/`TimedOut`.
- `ReportResult`: IO `status`, `Completed`/`TimedOut`, and `None`/`Partial`/`Complete` delivery.
- `HealthSnapshot`: coherent current health and compact last-failure metadata.
- `Config`, `QueueLimits`, `Stats`, and `MemoryStats` retain their existing roles.

`Types::Result`, `Types::PlatformError`, `FlushTimeout`, `ReportPopup`, `getLastResult()`, and `getLastPlatformError()` are removed.

## Lifecycle

`init()`, `initDefault()`, `initConsole()`, and `initFile()` return `InitResult`. Calling init while an active Logger exists is rejected with `IO::Types::ErrorCode::AlreadyOpen` and leaves the existing runtime untouched.

`flush(std::optional<std::chrono::milliseconds> timeout = std::nullopt)` is the single complete drain/flush operation. `std::nullopt` waits indefinitely, zero is a poll/no-wait deadline, positive values are finite, and negative values return `InvalidArgument`.

`shutdown()` returns `IO::Types::Status`, performs best-effort draining/flushing/close, and always leaves Logger disabled.

## Runtime filters

`setSourceFilter`, `clearSourceFilter`, `clearSourceFilters`, `setLevelFilter`, `clearLevelFilter`, and `clearLevelFilters` return `IO::Types::Status`. `shouldLog()` stays a lightweight boolean query.

## Normal logging

`log`, `trace`, `debug`, `info`, `warn`, `error`, and `fatal` remain filterable asynchronous queue-backed calls. Formatted overloads retain compile-time and explicit runtime-format forms. Accepted hot-path message text is valid UTF-8 by contract and is not rescanned unconditionally.

## Reports

`report`, `reportError`, and `reportFatal` are the synchronous emergency path. They bypass filtering and queue pressure, try the current normal sinks plus enabled emergency channels, flush normal sinks, and do not drain older queued work. Timed overloads take plain `std::chrono::milliseconds` before the message/format argument.

`reportFatal` owns fatal-popup behavior when enabled. `fatalTerminate` performs that path and then terminates. `writeDebugOutput` returns direct IO status.

## Diagnostics

Use `getHealth()` for coherent current sink/platform health and `getStats()` for relaxed resettable counters. Direct synchronous operations carry their own status; there is no process-wide mutable last-operation result.

@ref logger_configuration
@ref logger_lifecycle
@ref logger_reports
@ref logger_stats
