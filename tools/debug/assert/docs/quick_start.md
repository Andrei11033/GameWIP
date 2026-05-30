@page assert_quick_start Assert quick start

Use fatal assertions for invariants that should stop execution when they fail:

```cpp
#include "debug/assert/assert.h"

ASSERT(player != nullptr);
ASSERT_MSG(resource.isLoaded(), "Resource should be loaded before use");
```

Use `VERIFY` when the expression has side effects that must happen even when assertions are disabled:

```cpp
VERIFY(closeFile(handle));
VERIFY_MSG(registerSystem(system), "System registration failed");
```

Use recoverable checks when execution can continue:

```cpp
CHECK(config.isValid());
CHECK_MSG(optionalConfig.isValid(), "Optional config failed validation; using defaults");
CHECK_ONCE(false); // reports only the first failure from this call site
```

Use `ENSURE` when you want exactly-one expression evaluation and a boolean result:

```cpp
if (!ENSURE(socket.isOpen()))
{
    return false;
}
```

Use interactive assertions only for developer workflows where continuing after a failure can be safe enough for debugging.
