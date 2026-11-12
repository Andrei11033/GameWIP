@page logger_public_api Public API

Include `logger/logger.h` and link `GameWIP::Logger`. `logger/logger.h` is the
complete umbrella. For focused use, include `logger/record.h` for record
metadata, `logger/message_format.h` for message-color helpers,
`logger/logging.h` for lifecycle, filtering, and asynchronous logging, or
`logger/reports.h` for synchronous diagnostics.
`logger/types.h` and `logger/config.h` remain focused entry headers;
`logger/logger_macros.h` is opt-in.

## Types

`GameWIP::Logger::Types` is a real namespace. Important public vocabulary:

- `Level`: `Trace`, `Debug`, `Info`, `Warn`, `Error`, `Fatal`.
- `OutputMode`: `None`, `Console`, `File`, `Both`.
- `Init::Result`: operation `status`, final `outcome`, adjustment bitmask, requested/effective output, and directly associated `outputSetupStatus`.
- `FlushResult`: IO `status` plus `Completed`/`TimedOut`.
- `Report::Result`: IO `status`, `Completed`/`TimedOut`, and `None`/`Partial`/`Complete` delivery.
- `Health::Snapshot`: coherent current health and compact last-failure metadata.
- `Config`, `QueueLimits`, `Stats`, and `MemoryStats`: configuration and runtime observation values.
- `LogFilePathResult`: status-bearing UTF-8 path query result; the path is
  meaningful only when its status succeeds.

## Lifecycle

`init()`, `initDefault()`, `initConsole()`, and `initFile()` return `Types::Init::Result`. Calling init while an active Logger exists is rejected with
`IO::Types::ErrorCode::AlreadyOpen` and leaves the existing runtime untouched.

`flush(std::optional<std::chrono::milliseconds> timeout = std::nullopt)`: `nullopt` waits indefinitely, zero polls, negative returns `InvalidArgument`
(see `logging.h`).

`shutdown()` returns `IO::Types::Status`, drains/flushes/closes best effort, and always leaves Logger
disabled. `running()` reports whether the worker accepts normal records.

All queries are non-throwing and return bounded snapshots or safe defaults on failure.
`getLogFilePath()` returns `Types::LogFilePathResult` so allocation or conversion failure is a
status, not an exception or an ambiguous empty path.

## Runtime filters

`setSourceFilter`, `resetSourceFilter`, `resetSourceFilters`, `setLevelFilter`, `resetLevelFilter`, and `resetLevelFilters` return
`IO::Types::Status`. `shouldLog()` is a lightweight boolean query with three forms: `(level)` and `(level, std::string_view)` apply severity
filtering only; `(level, SourceId)` and enum-source forms also apply the registered-source filter.

## Normal logging

`log`, `trace`, `debug`, `info`, `warn`, `error`, and `fatal` are filterable asynchronous queue-backed calls. Formatted overloads provide compile-time
and explicit runtime-format forms. Accepted hot-path message text is valid UTF-8 by contract and is not rescanned unconditionally.

The default text layout is `[HH:MM:SS.mmm][Area][LEVEL] message`; area labels align to the widest registered source, and the five-character
severity column is padded. For example, `[14:01:10.241][Startup    ][INFO ] Logger initialized`. Trace is bright green by default.
Console output colors timestamp, severity, area, message,
and explicitly marked format arguments independently. By default, unmarked message text follows the severity color; set
`Theme::messageUsesSeverityColor` to `false` to use `Theme::message`, which is normal white by default. Timestamps and field names use muted gray.
File and debugger output contain the same text without terminal color sequences.

`Color` is a constexpr-friendly basic or RGB foreground color. Use `Logger::Color::rgb(120, 150, 255)` or a predefined value such as
`Logger::Color::Cyan`; `Logger::Color::basicColor(Logger::Color::Basic::Cyan)` creates a basic color directly. `Theme` in
`Types::Config::theme` configures the timestamp, message, severity, area, field, and marked-value colors.
`Types::Config::timestampFormat` controls displayed local time with `strftime` directives and Logger's `%f` extension for milliseconds; the default
`%H:%M:%S.%f` preserves `HH:MM:SS.mmm`. `%%` emits a literal percent sign. File-name timestamps use a separate format.
`defineSource(id, name, color)` assigns one optional area-label color; the existing `defineSource(id, name)` form uses the neutral area color.

`Logger::record()` starts a fluent `Record`. Call `.field("device", "Keyboard")` once per field, then optionally `.file()`, `.function()`,
`.line()`, `.sourceLocation()`, `.threadId()`, `.processId()`, or `.layout(Logger::Layouts::expanded())`. Pass the resulting record between
the source and message arguments. Record calls support preformatted, compile-time formatted, and `runtimeFormat()` messages:

```cpp
Logger::info(Area::Input,
    Logger::record().field("device", "Keyboard").field("id", 12).threadId().file().line(),
    "Device connected");
```

`Layouts::adaptive()`, `compact()`, `expanded()`, and `parenthesized()` select field presentation globally with `Config::fieldLayout` or for a
single `Record`. Adaptive is the default: short fields appear before the message; long or multiline fields move to continuation lines.

For advanced output, `makeLayout()` creates a compile-time-validated `LayoutTemplate`. The built-in layout uses the same renderer. Set
`Config::layout` for all records or pass the template to `Record::layout()` for one record. Supported names are `time`, `level`, `area`,
`message`, `source_file`, `source_function`, `source_line`, `thread_id`, `process_id`, and `fields`.
Logger compiles configured templates during `init()` and compiles a per-record template when `.layout()` is called, so the output worker executes
prepared instructions without reparsing placeholders and scopes for every line.

```cpp
constexpr auto diagnosticLayout = Logger::makeLayout(
    "{@timestamp}[{time}]{/} {@severity}[{level:<5}]{/} {@area}[{area:<auto}]{/} {source_file}:{source_line} {message} {fields}");
config.layout = diagnosticLayout;
Logger::error(Area::Input, Logger::record().field("device", "Keyboard").layout(diagnosticLayout), "Device connection failed");
```

Use `{@timestamp}`, `{@severity}`, `{@area}`, `{@message}`, `{@field}`, `{@code}`, `{@path}`, `{@number}`, and `{@quoted}` to color literal text
with a semantic theme role. Close each scope with `{/}`; scopes can nest up to eight levels, and explicitly marked format values keep their own
colors. Alignment uses `<` for left, `^` for center, and `>` for right, followed by a minimum byte width or `auto`. For example,
`{area:<auto}` pads to the widest registered name and `{level:^5}` centers severity labels in five bytes. Invalid placeholders and unbalanced
scopes fail during constant evaluation.
Double braces emit literal braces, as in `"{{code}} {message}"`.

Use `Logger::code(value)`, `path(value)`, `number(value)`, or `quoted(value)` to color exactly one formatted argument with the corresponding
theme role. `Logger::color(value, Logger::Color::rgb(...))` applies a one-call color instead. These wrappers preserve the normal formatted text:

```cpp
Logger::info(Area::Input, "Loaded code {} from {}", Logger::code(errorCode), Logger::path(configPath));
```

## Reports

`report`, `reportError`, and `reportFatal` are the synchronous emergency path. They bypass filtering and queue pressure, try the current normal sinks
plus enabled emergency channels, flush normal sinks, and do not drain older queued work. Timed overloads take plain `std::chrono::milliseconds` before
the message/format argument.

`reportFatal` owns fatal-popup behavior when enabled. `fatalTerminate` performs that path and then terminates. `writeDebugOutput` returns direct IO
status.

`report`, `reportError`, and `reportFatal` also accept `Record` before preformatted, compile-time formatted, or `runtimeFormat()` messages, using
the same fields and metadata layout as normal logging. `reportFatal` retains its fatal-popup behavior when that channel is enabled.

## Diagnostics

Use `getHealth()` for coherent current sink/platform health and `getStats()` for relaxed resettable counters. `resetStats()` clears those
counters without changing health; `getLifetimeDroppedLogCount()` is the process-lifetime queue-drop total and survives `resetStats()`. Direct
synchronous operations carry their own status; there is no process-wide mutable last-operation result.

@ref logger_configuration
@ref logger_lifecycle
@ref logger_reports
@ref logger_stats
