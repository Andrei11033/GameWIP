@page assert_quick_start Assert quick start

## Include

Include the public header:

```cpp
#include "debug/assert/assert.h"
```

## Installed CMake

Use the package target. The package resolves Logger when Assert was built with runtime support:

Set `GAMEWIP_REQUIRED_VERSION` from the consuming project's dependency lock; see @ref project_library_compatibility.

```cmake
find_package(Assert ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Assert)
```

## Source-tree CMake

When building inside the repository, link the short target:

```cmake
target_link_libraries(MyTarget PRIVATE Assert)
```

## Minimal usage

Use fatal assertions for development invariants:

```cpp
ASSERT(player != nullptr);
ASSERT_MSG(resource.isLoaded(), "Resource should be loaded before use");
```

Use `VERIFY` when the expression must run even when assertion handling is disabled:

```cpp
VERIFY(closeFile(handle));
VERIFY_MSG(commitTransaction(), "Transaction commit failed");
```

Use recoverable checks when execution can continue:

```cpp
CHECK_MSG(config.isValid(), "Invalid optional config; using defaults");
CHECK_ONCE_MSG(false, "This warning should only appear once from this call site");
```

Use `ENSURE` when the caller needs the boolean result:

```cpp
if (!ENSURE_MSG(socket.isOpen(), "Socket should be open"))
{
    return;
}
```

## Failure handling

Fatal failures report through Logger, may show Assert-owned UI, break only when the path requires a debugger break, and then either abort or follow the selected interactive action. Recoverable checks report through Logger and continue. Disabled macro behavior is documented in @ref assert_macro_behavior.

Automated tests must not depend on real dialog interaction. Deterministic validation paths are documented in @ref assert_testing and @ref assert_test_hooks.

## Where to go next

- @ref assert_public_api for the complete public surface.
- @ref assert_configuration for build options and compile definitions.
- @ref assert_macro_behavior for side-effect and message-evaluation rules.
- @ref assert_examples for common usage patterns.
- @ref assert_troubleshooting for common configuration and runtime symptoms.
