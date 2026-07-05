@page logger_macros Logger macros

`logger/logger_macros.h` provides opt-in global convenience macros. They keep hot paths cheaper by checking `shouldLog()` before evaluating the message expression.

## Severity macros

- `LOGGER_TRACE(source, message)`
- `LOGGER_DEBUG(source, message)`
- `LOGGER_INFO(source, message)`
- `LOGGER_WARN(source, message)`
- `LOGGER_ERROR(source, message)`
- `LOGGER_FATAL(source, message)`
- `LOGGER_FATAL_TERMINATE(source, message)`

Example:

```cpp
LOGGER_DEBUG(LogSource::Physics, std::format("body count {}", bodyCount));
```

If Debug is disabled or the source is filtered, the message expression is skipped.

`LOGGER_TRACE` and `LOGGER_DEBUG` can compile out in release-style builds unless `LOGGER_ENABLE_TRACE_LOGS` or `LOGGER_ENABLE_DEBUG_LOGS` is defined. `LOGGER_FATAL_TERMINATE` intentionally calls the synchronous fatal-terminate path and does not use the lazy normal-log queue.

## When to prefer macros

Use macros when:

- constructing the message is expensive,
- argument side effects must be skipped when filtered,
- the call is in a frequently executed path.

Use direct `Logger::info(...)` style calls when the message is already cheap or already built.

## Direct lazy guard

For custom patterns, use `shouldLog()` yourself:

```cpp
if (GameWIP::Logger::shouldLog(GameWIP::Logger::Types::Level::Trace, LogSource::Physics))
{
    GameWIP::Logger::trace(LogSource::Physics, expensiveTraceMessage());
}
```

## Common mistake

Do not assume direct formatted calls are lazy:

```cpp
// expensiveValue() is evaluated before Logger::debug sees the call.
GameWIP::Logger::debug("AI", "value {}", expensiveValue());
```

Use `LOGGER_DEBUG` or `shouldLog()` for lazy evaluation.

Direct `Logger::debug(source, "value {}", expensiveValue())` checks filters inside the logger, after C++ has evaluated the function arguments. Use a macro or explicit `shouldLog()` guard when argument evaluation matters.

## Related pages

- @ref logger_threading_performance
- @ref logger_examples
