@page assert_examples Assert examples

## Fatal invariant

```cpp
ASSERT(entity != nullptr);
ASSERT_MSG(index < values.size(), "Index out of range");
```

## Always-evaluated verification

```cpp
VERIFY(closeFile(handle));
VERIFY_MSG(registerSystem(system), "System registration failed");
```

Use `VERIFY` when the expression has side effects that must happen in disabled assert builds.

## Recoverable check

```cpp
CHECK(optionalConfig.isValid());
CHECK_MSG(optionalConfig.isValid(), "Optional config failed validation; using defaults");
```

## Check once

```cpp
CHECK_ONCE(cacheWarningCondition());
CHECK_ONCE_MSG(false, "This warning should only be logged once from this call site");
```

`CHECK_ONCE` suppresses reports after the first failed reporting attempt from the same macro expansion. It does not create a global warning registry.

## Ensure as a boolean guard

```cpp
if (!ENSURE_MSG(socket.isOpen(), "Socket must be open before sending"))
{
    return false;
}
```

## Interactive assertion

```cpp
ASSERT_INTERACTIVE_MSG(state.isConsistent(), "Developer-only consistency failure");
VERIFY_INTERACTIVE_MSG(reloadDebugData(), "Debug reload failed");
```

Use interactive assertions only where continuing after a failure is acceptable during development.
