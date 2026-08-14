@page assert_test_hooks Test hooks

@warning Assert test hooks are supported source-tree maintainer interfaces. They are not installed, not consumer API, and not public compatibility promises.

## Availability

Enable hooks with:

```powershell
-DASSERT_ENABLE_TEST_HOOKS=ON
```

Approved source-tree test targets then receive `ASSERT_INTERNAL_TEST_HOOKS=1`. The repository validation preset enables this path for Assert validation.

## Include

Source-tree validation code includes:

```cpp
#include "debug/assert/internal/assert_test_hooks.h"
```

The header is under `tools/debug/assert/internal/` and is excluded from installed public header file sets.

## Reset rule

Call `GameWIP::Debug::Assert::TestHooks::reset()` after every forced scenario. Reset clears one-shot dialog failures and persistent debugger/popup overrides.

## Hook groups

| Group | Hooks | Lifetime |
| --- | --- | --- |
| Dialog fallback | `forceNextActionDialogFailure`, `forceNextFallbackActionDialogFailure` | One-shot. |
| Debugger state | `setDebuggerAttachedOverride`, `clearDebuggerAttachedOverride`, `debuggerAttachedForTest` | Persistent until clear or reset. |
| Popup suppression | `setPopupSuppressedOverride`, `clearPopupSuppressedOverride` | Persistent until clear or reset. |
| Backend exercise | `showFailureActionDialogForTest`, `showErrorPopupForTest` | Direct test adapter calls. |

## API reference

| API | Purpose |
| --- | --- |
| `reset()` | Clears all pending hook state. |
| `forceNextActionDialogFailure()` | Forces the next primary action-dialog attempt to use fallback behavior. |
| `forceNextFallbackActionDialogFailure()` | Forces the next fallback action-dialog attempt to return the default action. |
| `setDebuggerAttachedOverride(bool)` | Overrides debugger detection. |
| `clearDebuggerAttachedOverride()` | Removes the debugger override. |
| `setPopupSuppressedOverride(bool)` | Overrides popup suppression checks. |
| `clearPopupSuppressedOverride()` | Removes the popup suppression override. |
| `debuggerAttachedForTest()` | Queries debugger state through the Assert backend. |
| `showFailureActionDialogForTest(...)` | Exercises the platform interactive action dialog path. |
| `showErrorPopupForTest(...)` | Exercises the platform error-popup path. |

## Example

```cpp
using namespace GameWIP::Debug::Assert;

TestHooks::setDebuggerAttachedOverride(false);
// Run the scenario that must observe no debugger.
TestHooks::reset();
```

## Restrictions

Installed consumers and production code must not include internal hook headers or call hook functions. Hook symbols may exist in validation builds only to make rare platform and process-control paths deterministic.

## Related pages

- @ref assert_testing
- @ref assert_interactive
- @ref assert_configuration
