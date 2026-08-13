@page filesystem_test_hooks Test hooks

@warning These hooks are source-tree maintainer interfaces. They are not installed, exported as consumer API, or covered by installed-package compatibility guarantees.

## Availability

Configure with:

```cmake
-D FILESYSTEM_ENABLE_TEST_HOOKS=ON
```

The short build-tree target exposes `FILESYSTEM_INTERNAL_TEST_HOOKS=1` to source-tree validation consumers. Installed packages do not expose the internal header or definition.

## Include

```cpp
#include "filesystem/internal/filesystem_platform.h"
```

Hooks live under `GameWIP::FileSystem::Detail::Platform::TestHooks`.

## Reset rule

Call `reset()` before and after each scenario that changes hook state. Checked-operation failures are one-shot. Move pauses are armed for one matching phase. The unlock-failure override is persistent until disabled or reset.

## API reference

### `forceNextCheckedFailure(operation, failure, code, nativeCode)`

Arms one matching checked file operation. `CheckedFileOperation` selects read, write, flush, close, position, size, seek, resize, or native diagnostic-message construction. `CheckedFailure` selects an injected status, allocation failure, or unexpected exception.

Status injection preserves the supplied portable and native codes. Allocation and unexpected exceptions must be contained as `OutOfMemory` and `Unknown`. A diagnostic-message failure instead preserves the original native operation code and native value with an empty message.

### `setFileUnlockFailure(bool enabled)`

When enabled, native file-lock release attempts return a failure. The lock remains active at the public boundary, allowing validation of retry and destructor-cleanup behavior.

### Move pause hooks

`armMoveDestinationValidatedPause()` and `armMoveCommittedPause()` stop a move at deterministic backend phases. `waitForMovePause()` observes the pause and `releaseMovePause()` allows it to continue. These hooks validate destination races and post-commit behavior.

### `reset()`

Restores every FileSystem platform hook to its default state.

## Intended protocol

A typical checked-operation scenario:

1. reset hooks;
2. open a temporary file with the required access;
3. arm one matching status or exception failure;
4. invoke the public operation once and verify its returned status and progress;
5. for close failure, verify the handle remains open and a retry succeeds;
6. reset hooks and remove temporary state.

Always structure cleanup so an assertion failure cannot leave the process-wide hook enabled for later tests.

## Related pages

- @ref filesystem_testing
- @ref filesystem_file_open_modes
- @ref project_testing
- @ref project_documentation
