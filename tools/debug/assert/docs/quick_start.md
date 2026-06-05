@page assert_quick_start Assert quick start

Use fatal assertions for invariants that should stop execution when they fail:

```cpp
ASSERT(player != nullptr);
ASSERT_MSG(resource.isLoaded(), "Resource should be loaded before use");
```

Use `VERIFY` when the expression must run even when assertions are disabled:

```cpp
VERIFY(closeFile(handle));
VERIFY_MSG(commitTransaction(), "Transaction commit failed");
```

Use recoverable checks when execution can continue:

```cpp
CHECK_MSG(config.isValid(), "Invalid optional config; using defaults");
CHECK_ONCE_MSG(false, "This warning should only appear once from this call site");
```

Use `ENSURE` when you want a boolean result and exactly-one evaluation:

```cpp
if (!ENSURE_MSG(socket.isOpen(), "Socket should be open"))
{
    return;
}
```

Use interactive assertions only for developer workflows where continuing may be acceptable after inspecting the failure.

Automated tests must use `INTERNAL_ASSERT_TEST_ACTION` or test hooks for interactive paths. Real dialogs are reserved for runtime-gated manual UI tests.
