@page assert_quick_start Assert quick start

## Include

Include the library's public header:

```cpp
#include "debug/assert/assert.h"
```

## Installed CMake

Use the package's namespaced imported target. The package resolves its Logger dependency when runtime assertions are enabled:

```cmake
find_package(Assert CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Assert)
```

## Source-tree CMake

When Assert is part of the same source tree, use its short build target:

```cmake
target_link_libraries(MyTarget PRIVATE Assert)
```

## Minimal usage

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

Use `ENSURE` when the caller needs a boolean result and exactly-one evaluation:

```cpp
if (!ENSURE_MSG(socket.isOpen(), "Socket should be open"))
{
    return;
}
```

Use interactive assertions only for developer workflows where continuing may be acceptable after inspecting the failure.

Automated tests must not depend on real dialog interaction. Maintainer-only deterministic validation is documented under @ref assert_testing.
