@page logger_messages_sources Messages and sources

## UTF-8 contract

Logger text is UTF-8. Configuration text and registered source names are validated during `init()`. Reports and direct debugger output are checked cold-path boundaries. Ordinary hot message submission accepts valid UTF-8 by contract and does not add a whole-message validation scan.

Logger never normalizes Unicode, adds/removes a BOM, or silently repairs malformed input. Platform/native conversion validates as part of conversion where practical.

String views passed to a call need remain valid only for that call; retained text is copied.

## Sources

String sources use severity filtering only. Registered `SourceId` values additionally support source filters. Unknown runtime IDs are written as `UnknownSource` and counted. Source definitions must have unique IDs, non-empty valid UTF-8 names, and initial filters must refer to registered IDs.

Source enums use an unsigned underlying type no wider than `SourceId`; `defineSource()` only creates a definition, while `Config::sources` performs registration at init.

## Message forms

Preformatted text, compile-time checked `std::format_string`, and explicit `runtimeFormat()` forms are supported. Formatting happens on the caller thread. Invalid runtime formatting is contained; normal logging records a format-failure statistic and skips the record, while synchronous report formatting returns a direct failed `Types::Report::Result`.

Use `LOGGER_*` or `shouldLog()` around expensive arguments because C++ evaluates direct function arguments before Logger can filter them.

## Message limits

`maxMessageLength` is a byte budget, but Logger-owned truncation always backs up to a valid UTF-8 scalar boundary before appending the truncation suffix. Queue copying, inline storage, batching, report formatting, debugger output, and popup forwarding therefore never create a split encoded scalar from valid input.

`StrictBounded` limits retained formatting growth while formatting; `FastNormal` formats into reusable scratch and bounds the retained result afterward.

## Related pages

- @ref logger_configuration
- @ref logger_macros
- @ref logger_threading_performance
- @ref logger_stats
