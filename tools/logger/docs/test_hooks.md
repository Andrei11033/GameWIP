@page logger_test_hooks Logger internal test hooks

@warning These hooks are supported source-tree maintainer interfaces. They are not installed and are not versioned consumer API.

## Enable

The GameWIP `validation` preset enables hooks automatically because it builds tests. A focused source build can enable them explicitly:

```powershell
-DLOGGER_ENABLE_TEST_HOOKS=ON
```

Approved test targets then receive `INTERNAL_LOGGER_TEST_HOOKS=1`.

A source-tree test links `Logger` (or `GameWIP::Logger`) and includes:

```cpp
#include "logger/internal/logger_test_hooks.h"
```

The build-tree target supplies the source include root and compile definition. This does not work from an installed package by design.

## Deterministic paths

Hooks can force:

- the next file-open failure;
- the next queue-entry allocation/copy failure;
- the next file-flush failure;
- the next file-write failure;
- the next fatal-popup failure;
- a timed-flush timeout;
- complete reset after a scenario.

## Rules

- Hook declarations remain under `tools/logger/internal/`.
- Hook headers are excluded from installs and public CMake file sets.
- One-shot hooks use `forceNext...`; persistent overrides use paired set/clear operations.
- Tests reset state even when an expectation fails.
- Consumer or game runtime code must not include or call hooks.

See @ref logger_testing for the complete validation split.
