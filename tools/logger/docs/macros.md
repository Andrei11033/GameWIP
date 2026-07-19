@page logger_macros Macros

`logger/logger_macros.h` provides optional global lazy shortcuts. The namespace API remains the primary public surface.

## Macro families

| Macro | Normal severity/path | Release-style compile behavior |
| --- | --- | --- |
| `LOGGER_TRACE` | Asynchronous `Trace` | Compiles out under `NDEBUG` unless `LOGGER_ENABLE_TRACE_LOGS` is defined. |
| `LOGGER_DEBUG` | Asynchronous `Debug` | Compiles out under `NDEBUG` unless `LOGGER_ENABLE_DEBUG_LOGS` is defined. |
| `LOGGER_INFO` | Asynchronous `Info` | Always present. |
| `LOGGER_WARN` | Asynchronous `Warn` | Always present. |
| `LOGGER_ERROR` | Asynchronous `Error` | Always present. |
| `LOGGER_FATAL` | Asynchronous `Fatal`; does not terminate | Always present. |
| `LOGGER_FATAL_TERMINATE` | Synchronous fatal report then `std::terminate()` | Always present; not a normal lazy log. |

Each normal macro accepts the same message forms as its corresponding namespace function: preformatted text, compile-time checked formats, and explicit runtime formats.

## Evaluation rules

For an active normal macro:

```cpp
LOGGER_INFO(sourceExpression(), "value {}", expensiveValue());
```

1. `sourceExpression()` is evaluated exactly once and captured.
2. `shouldLog()` checks severity and, for registered IDs/enums, source filtering.
3. Message/format arguments are evaluated only when that first check passes.
4. The called namespace function performs its normal filter check again before queueing.

The second check is intentional because filters can change concurrently.

For a compiled-out `LOGGER_TRACE` or `LOGGER_DEBUG`, neither source nor message arguments are evaluated.

`LOGGER_FATAL_TERMINATE` does not use `shouldLog()`. Its arguments are evaluated and it enters the synchronous fatal-termination path regardless of normal runtime filters.

## Choosing macros or direct calls

Use direct calls for cheap values:

```cpp
Logger::info("Game", "Frame {}", frameIndex);
```

Use macros or an explicit guard when producing an argument is expensive or has side effects that must be skipped when filtered:

```cpp
LOGGER_DEBUG(LogSource::Physics, "contact graph {}", buildContactGraphDump());
```

Equivalent explicit guard:

```cpp
if (Logger::shouldLog(Logger::Types::Level::Debug, LogSource::Physics))
{
    Logger::debug(LogSource::Physics, "contact graph {}", buildContactGraphDump());
}
```

Do not pass an already formatted `std::format(...)` result merely to obtain laziness; use the Logger format overload so formatting occurs only inside the guarded branch.

## Translation-unit consistency

`NDEBUG`, `LOGGER_ENABLE_TRACE_LOGS`, and `LOGGER_ENABLE_DEBUG_LOGS` are evaluated while each translation unit includes the macro header. Define them consistently through target compile definitions. Inconsistent settings can make the same macro call exist in one translation unit and disappear in another.

## Macro hygiene

The normal macros use a `do { ... } while (false)` statement wrapper and capture the source with `auto&&`. They should be used as statements and followed by a semicolon.

## Related pages

- @ref logger_messages_sources
- @ref logger_threading_performance
- @ref logger_reports
