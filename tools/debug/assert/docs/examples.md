@page assert_examples Assert examples

## Fatal invariant

```cpp
ASSERT(entity != nullptr);
ASSERT_MSG(index < values.size(), "Index out of range");
```

## Always-evaluated verification

```cpp
VERIFY(closeFile(handle));
VERIFY_MSG(writeHeader(file), "Could not write save header");
```

## Recoverable check

```cpp
CHECK_MSG(optionalConfig.isValid(), "Optional config failed validation");
CHECK_MSG(cache.refresh(), "Cache refresh failed; continuing with old data");
```

## Check once

```cpp
CHECK_ONCE_MSG(false, "This warning should only be logged once from this call site");
```

Use `CHECK_ONCE` for noisy recoverable diagnostics. It is scoped to the macro expansion site, not to the condition text.

## Ensure result

```cpp
if (!ENSURE_MSG(loadOptionalConfig(), "Using defaults because optional config failed"))
{
    useDefaultConfig();
}
```

`ENSURE` and `ENSURE_MSG` always evaluate the condition once and return the boolean result.

## Interactive assertion

```cpp
ASSERT_INTERACTIVE_MSG(state.isConsistent(), "Developer-only consistency failure");
VERIFY_INTERACTIVE(rebuildDebugCache());
```

Use interactive assertions only where continuing after a failure is acceptable during development.

## Unreachable and debug break

```cpp
switch (mode)
{
case Mode::A:
    break;
case Mode::B:
    break;
default:
    UNREACHABLE();
}

DEBUG_BREAK();
```

Use `UNREACHABLE` for impossible control-flow paths. Use `DEBUG_BREAK` when a deliberate debugger trap is clearer than an assertion condition.
