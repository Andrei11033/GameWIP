@page test_support_test_hooks Source-tree test hooks

@warning These hooks are source-tree maintainer interfaces. They are not installed consumer API.

The validation composition enables `TEST_SUPPORT_ENABLE_TEST_HOOKS`. Approved build-tree consumers receive:

```text
TEST_SUPPORT_INTERNAL_TEST_HOOKS=1
```

Production code must not define that macro manually or depend on hook symbols.

Include:

```cpp
#include "test_support/internal/test_support_test_hooks.h"
```

The existing one-shot child-process, file, filesystem-guard, and environment failure points remain unchanged. Call `GameWIP::TestSupport::TestHooks::reset()` before and after each deterministic failure-injection scenario.

Installed-consumer validation explicitly verifies that `TEST_SUPPORT_INTERNAL_TEST_HOOKS` does not leak through `GameWIP::TestSupport`.

@ref test_support_testing
@ref project_testing
