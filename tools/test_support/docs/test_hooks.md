@page test_support_test_hooks Source-tree test hooks

@warning These hooks are source-tree maintainer interfaces. They are not installed, not consumer API, and not covered by package compatibility guarantees.

## Availability

The validation composition enables `TEST_SUPPORT_ENABLE_TEST_HOOKS`. Approved build-tree consumers receive `INTERNAL_TEST_SUPPORT_TEST_HOOKS=1`. Production code must not define that macro manually or depend on hook symbols.

## Include and link

```cpp
#include "test_support/internal/test_support_test_hooks.h"
```

Link the source-tree `TestSupport` target. The header is absent from installed packages.

## Reset rule

Call `GameWIP::TestSupport::TestHooks::reset()` before and after each scenario. It clears every pending one-shot child-process, file, filesystem-guard, and environment failure.

## One-shot failures

- `forceNextChildProcessFailure()` covers allocation, unsupported-backend reporting, generic platform handling, process setup, handles, pipes, launch, job assignment, capture setup/read, reader-thread creation, thread resume, wait, inspection, and cleanup. The unsupported injection validates the public result contract on the repository's supported Win32 build without claiming that another platform backend exists.
- `forceNextFileFailure()` covers reads, writes, existence inspection, directory creation, removal, temporary-directory construction, and current-path construction.
- `forceNextEnvironmentFailure()` covers environment reads, sets, and unsets.

Each hook is consumed atomically by the next matching operation and preserves the supplied synthetic native code. Arm a hook immediately before its target operation. Hook state is process-wide and is not automatically isolated between tests.

## Related pages

- @ref test_support_testing
- @ref project_testing
- @ref project_extending
