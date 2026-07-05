@page assert_test_hooks Assert test hooks

@warning These hooks are supported source-tree maintainer interfaces. They are not installed and are not versioned consumer API.

## Enable

The GameWIP `validation` preset enables hooks automatically because it builds tests. A focused source build can enable them explicitly:

```powershell
-DASSERT_ENABLE_TEST_HOOKS=ON
```

Approved test targets then receive `INTERNAL_ASSERT_TEST_HOOKS=1`.

A source-tree test links the short `Assert` target and includes:

```cpp
#include "debug/assert/internal/assert_test_hooks.h"
```

The build-tree target supplies the source include root and compile definition. This does not work from an installed package by design.

## Deterministic paths

Hooks can:

- force the primary action dialog to fall back;
- force fallback-dialog/default behavior;
- override debugger-attached detection;
- override popup suppression;
- reset all forced state between tests.

## Rules

- Hook declarations remain under `tools/debug/assert/internal/`.
- Hook headers are excluded from installs and public CMake file sets.
- Tests reset state after each forced scenario.
- Consumer or game runtime code must not include or call hooks.

See @ref assert_testing and @ref assert_interactive.
