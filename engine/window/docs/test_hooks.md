@page window_test_hooks Test hooks

## Availability

Hooks are enabled by the source-tree `WINDOW_ENABLE_TEST_HOOKS` option. The repository sets it from `GAMEWIP_TESTS_REQUIRED`. The build-interface definition is `INTERNAL_WINDOW_TEST_HOOKS`; it is absent from installed targets.

## Include

Validation code conditionally includes:

```cpp
#if INTERNAL_WINDOW_TEST_HOOKS
#include "window/internal/window_test_hooks.h"
#endif
```

The header is source-tree-only and is not installed.

## Reset rule

Fault injection is one-shot and thread-local. A matching production boundary consumes the armed point; `resetFailures()` clears an unconsumed point. Tests reset at suite boundaries so one case cannot affect another thread or later case.

Each `openPortable()` call creates state owned by the supplied Window and borrows the supplied event array. Call `close()` or destroy that Window before its storage leaves scope. A Window already holding hook or native state is rejected.

## Hook groups

`failNext()` exercises real production error and rollback boundaries for allocation, dispatcher registration, native creation, partial open, title/region/icon/cursor mutation, monitor/display/display-color queries, fullscreen transitions and restoration, close, and event pumping. The hook changes only the selected boundary's result; ordinary rollback and retry code remains active.

`pumpReentrantly()` activates the owning-thread dispatcher guard around the production pump so its `ResourceBusy` contract can be checked without callbacks or visible UI.

`openPortable()` creates a backend-free queue fixture. It intentionally leaves native-only operations and `isOpen()` in their normal closed-native state. Public queue inspection and consumption remain available on the creating thread.

`enqueue()` routes an arbitrary typed payload through the production coalescing, sequence, eviction, and drop-count algorithm. It does not synthesize cached property changes; native routing tests must update state through actual backend messages or operations.

`requestClose()` uses the production sticky close-request path so repeated-source and full-queue behavior can be deterministic.

`destroyNativeWindow()` enters the real unexpected `WM_NCDESTROY` path. Tests verify `ClosedEvent`, `NativeDestroyedPendingFinalize`, native-operation rejection, controlled finalization, and reopening. `simulateFullscreenMonitorRemoval()` exercises the production fullscreen recovery transaction and event order without changing the physical display topology.

`makeDisplayColorInfo()` validates backend-neutral classification, numeric sanitization, precision saturation, and the DisplayConfig SDR-white conversion without requiring HDR hardware. `makeNextDisplayColorMetadataUnavailable()` verifies that runtime API/metadata absence succeeds with documented unknown/zero fields. `simulateDisplayColorConfigurationChange()` makes the next owner-thread pump exercise the production `DisplayConfigurationChangedEvent` route used by stale native color state.

The packed-mask accessors expose active word count, values, revision, and storage identity for first allocation, same-size reuse, stale-publication, trailing-bit, movement, resize, and clear checks. They never mutate state.

`calculateDpiTransition()`, `refreshRateMillihertz()`, and `exactNativeDisplayModeMatches()` expose the production arithmetic/comparison seams without synthesizing an invalid OS DPI or display transition.

## API reference

The hook declarations are documented in `window/internal/window_test_hooks.h`. They are validation interfaces, not installed compatibility promises.

## Example

```cpp
std::array<GameWIP::Window::Types::Event, 2> storage;
GameWIP::Window::Window fixture;
if (!GameWIP::Window::TestHooks::openPortable(fixture, storage).ok()) return;

static_cast<void>(GameWIP::Window::TestHooks::enqueue(
    fixture, GameWIP::Window::Types::MovedEvent{{10, 20}}));

GameWIP::Window::Types::Event event;
static_cast<void>(fixture.popEvent(event));
static_cast<void>(fixture.close());
```

## Related pages

See @ref window_lifecycle_and_events for the production queue contract and @ref window_testing for validation commands.
