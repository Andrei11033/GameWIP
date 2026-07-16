@page logger_messages_sources Logger messages and sources

This page owns source selection, formatting, copying, and message-bound behavior for normal logs and reports.

## Text contract

Source names, string sources, messages, format strings, and configured paths use UTF-8 bytes at the Logger boundary. Logger does not validate that arbitrary source/message bytes form well-formed UTF-8 before storing them.

Logger copies source and message text needed after a call returns. Caller-owned `std::string_view` values passed to normal logging and reports need to remain valid only through the call. Configuration views and spans need to remain valid through `init()`.

## Source forms

### String source

```cpp
Logger::info("Network", "Connection established");
```

String sources are copied into normal queue entries. They use `minLevel` and level filters but do not participate in source filters.

### Registered `SourceId`

```cpp
constexpr std::array sources{
    Logger::Types::SourceDefinition{1, "Network"},
};

Logger::Types::Config config = Logger::defaultConfig();
config.sources = sources;
Logger::init(config);
Logger::info(Logger::Types::SourceId{1}, "Connection established");
```

Registered IDs store a compact numeric key in the queue and enable source filtering. Unknown IDs are accepted, displayed as `UnknownSource`, and counted in `Stats::unknownSourceUses` when written.

### Enum source

```cpp
enum class LogSource : Logger::Types::SourceId
{
    Network = 1,
};

constexpr auto definition = Logger::defineSource(LogSource::Network, "Network");
Logger::info(LogSource::Network, "Connection established");
```

A source enum must have an unsigned underlying type no wider than `SourceId`. Logger's own public enums are excluded from the source-enum overloads. Give source enums explicit stable values when their IDs must remain consistent across modules or persisted configuration.

`defineSource()` converts the enum value to `SourceId`; it does not register globally by itself. Registration occurs when the resulting definition is included in `Config::sources` during `init()`.

## Source registration rules

Initialization rejects:

- duplicate source IDs;
- duplicate source names;
- empty source names;
- initial source filters that refer to unregistered IDs.

Logger copies the table during `init()`. Runtime source-filter operations accept only registered IDs.

## Message forms

### Preformatted text

```cpp
Logger::info("Game", existingMessage);
```

Use this when message bytes already exist. The message is copied and bounded before the call returns.

### Compile-time checked format

```cpp
Logger::info("Game", "Loaded {} assets", assetCount);
```

This is the preferred formatted form. `std::format_string` checks the format expression against argument types at compile time. Formatting occurs on the calling thread before queue insertion.

### Runtime format

```cpp
Logger::info("Game", Logger::runtimeFormat(dynamicFormat), assetCount);
```

Use `runtimeFormat()` only when format text is not known at compile time. Invalid runtime formatting is contained by Logger, increments `formatFailures`, and skips that record/report rather than propagating `std::format_error` from Logger's formatting bridge.

Exceptions raised while Logger formats are contained by the public formatting bridges. `std::format_error` increments `formatFailures`; other exceptions increment `allocationFailures`, and that operation is skipped. Custom formatters should still avoid side effects and exceptions because a failed log/report has no per-call error result.

## Argument evaluation

Direct function arguments are evaluated before Logger can check runtime filters:

```cpp
Logger::debug("AI", "state {}", buildExpensiveState());
```

Use `LOGGER_*` or an explicit `shouldLog()` guard when evaluation must be skipped. See @ref logger_macros for exact macro rules.

## Formatting policies

| Policy | Behavior |
| --- | --- |
| `StrictBounded` | Formats through a bounded iterator so retained output never grows beyond the active message limit plus internal suffix handling. Best for predictable peak memory. |
| `FastNormal` | Formats into nesting-safe reusable thread-local scratch, then truncates. Usually faster for ordinary messages but can temporarily hold the complete formatted result. |

Nested Logger calls from a custom formatter use a separate scratch lease. The nested call completes and is queued before the outer formatted call.

## Message limit and truncation

`maxMessageLength` is measured in bytes. Messages beyond the limit are retained with Logger's truncation suffix and increment `Stats::truncated` when accepted by a normal sink. Truncation can split a multi-byte UTF-8 sequence because the contract is byte-based.

Preformatted, compile-time-format, runtime-format, normal-log, report, debugger-output, and popup paths all use the active message bound. `StrictBounded` limits formatting growth; `FastNormal` limits only the retained result.

## Related pages

- @ref logger_configuration
- @ref logger_macros
- @ref logger_threading_performance
- @ref logger_stats
