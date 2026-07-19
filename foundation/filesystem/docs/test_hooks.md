@page filesystem_test_hooks Test hooks

@warning These hooks are source-tree maintainer interfaces. They are not installed, exported as consumer API, or covered by installed-package compatibility guarantees.

## Availability

Configure with:

```cmake
-D FILESYSTEM_ENABLE_TEST_HOOKS=ON
```

The short build-tree target exposes `INTERNAL_FILESYSTEM_TEST_HOOKS=1` to source-tree validation consumers. Installed packages do not expose the internal header or definition.

## Include

```cpp
#include "filesystem/internal/filesystem_platform.h"
```

Hooks live under `GameWIP::FileSystem::Detail::Platform::TestHooks`.

## Reset rule

Call `reset()` before and after each scenario that changes hook state. The unlock-failure override is persistent until disabled or reset.

## API reference

### `setFileUnlockFailure(bool enabled)`

When enabled, native file-lock release attempts return a failure. The lock remains active at the public boundary, allowing validation of retry and destructor-cleanup behavior.

### `reset()`

Restores every FileSystem platform hook to its default state.

## Intended protocol

A typical isolated scenario:

1. reset hooks;
2. create and lock a temporary file;
3. enable unlock failure;
4. verify explicit `unlock()` fails and `active()` remains true;
5. destroy the lock owner to exercise best-effort cleanup;
6. reset hooks;
7. verify a competing lock can be acquired;
8. remove temporary state.

Always structure cleanup so an assertion failure cannot leave the process-wide hook enabled for later tests.

## Related pages

- @ref filesystem_testing
- @ref filesystem_file_open_modes
- @ref project_testing
- @ref project_documentation
