@page logger_messages_sources Messages and sources

Messages carry UTF-8 diagnostic text; sources add a stable identity and local
severity filter. This page explains when each value is validated, copied, and
made visible to the asynchronous worker.

## UTF-8 contract

Logger text is UTF-8. Configuration text and registered source names are validated during `init()`. Reports and direct debugger output are checked
cold-path boundaries. Ordinary hot message submission accepts valid UTF-8 by contract and does not add a whole-message validation scan.

Logger never normalizes Unicode, adds/removes a BOM, or silently repairs malformed input. Platform/native conversion validates as part of conversion
where practical.

String views passed to a call need remain valid only for that call; retained text is copied.

## Sources

String sources use severity filtering only. Registered `SourceId` values additionally support source filters. Unknown runtime IDs are written as
`UnknownSource` and counted. Source definitions must have unique IDs, non-empty valid UTF-8 names, and initial filters must refer to registered IDs.

Source enums use an unsigned underlying type no wider than `SourceId`; the public `Logger::SourceEnum` concept rejects signed, wide, and Logger-owned
enums at overload resolution, before deeper source assertions. `defineSource()` only creates a definition, while `Config::sources` performs
registration at init.

The optional third argument to `defineSource(id, name, color)` sets only the registered area label's foreground color. Use the compile-time
friendly `Logger::Color::rgb(red, green, blue)` factory or the predefined `Logger::Color::...` constants.

## Message forms

Preformatted text, compile-time checked `std::format_string`, and explicit `runtimeFormat()` forms are supported. Formatting happens on the caller
thread. Invalid runtime formatting is contained; normal logging records a format-failure statistic and skips the record, while synchronous report
formatting returns a direct failed `Types::Report::Result`.

Formatted normal-log calls apply their filter before caller-thread formatting. Calls using a registered `SourceId` or source enum apply both the
severity and source filters; calls using a string source apply the severity filter only. The enqueue path rechecks registered-source filters to
handle concurrent filter changes before a record is accepted.

Use `LOGGER_*` or `shouldLog()` around expensive arguments because C++ evaluates direct function arguments before Logger can filter them.

Use `Logger::record()` for opt-in fields and metadata:

```cpp
Logger::info(Area::Input,
    Logger::record().field("device", "Keyboard").field("id", 12).threadId().sourceLocation(),
    "Device connected");
```

Fields accept formattable scalar values, are limited by `maxRecordFields`, `maxFieldKeyLength`, and `maxFieldValueLength`, and appear before
the message in compact output. Record calls support preformatted text, compile-time formats, and explicit `runtimeFormat()` calls. The same
forms work with synchronous reports. `Logger::code(value)`, `path(value)`, `number(value)`, and `quoted(value)` mark exact format arguments for
their configured theme colors. `Logger::color(value, color)` supplies a color for one argument. Markers are removed from file and debugger text.

## Message limits

`maxMessageLength` is a byte budget, but Logger-owned truncation always backs up to a valid UTF-8 scalar boundary before appending the truncation
suffix. Queue copying, inline storage, batching, report formatting, debugger output, and popup forwarding therefore never create a split encoded
scalar from valid input.

`StrictBounded` limits retained formatting growth while formatting; `FastNormal` formats into reusable scratch and bounds the retained result
afterward.

## Related pages

- @ref logger_configuration
- @ref logger_macros
- @ref logger_threading_performance
- @ref logger_stats
