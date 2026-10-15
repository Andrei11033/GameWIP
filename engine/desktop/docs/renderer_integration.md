@page desktop_renderer_integration Renderer integration

`desktop/renderer_bridge.h` is the optional renderer integration surface: concurrent presentation-read opt-in, renderer-to-Window feedback, and
renderer-published pointer data. OS display/HDR inspection lives in `desktop/display_info.h`.

## Concurrent presentation reads

Presentation getters read the owner-thread cache by default. Call
`Renderer::enableConcurrentPresentationReads(window)` on an open Window's owner
thread before starting renderer reads; it lazily allocates stable publication
storage and publishes the current state (default Windows pay nothing). Enablement is one-way for the C++ object,
survives close/reopen, and repeated calls succeed. Do not race it with getter reads or destruction.

It returns `NotOpen` for a closed Window, `ResourceBusy` on the wrong thread, and `OutOfMemory` on allocation failure.
`Renderer::concurrentPresentationReadsEnabled(window)` reports the state.

Once enabled, `clientSize()`, `framebufferSize()`, `contentScale()`, `effectiveDpi()`, `currentMonitor()`, `presentationState()`, `minimized()`,
`maximized()`, `visible()`, `interactiveMoveResizeActive()`, and `occluded()` are safe for renderer-thread reads while the owner handles native
messages. Compound values cannot tear; separate calls may observe successive states.

`occluded()` works without this facility (owner-thread, authoritative); when enabled, transitions are also mirrored to the atomic publication. Reads never queue events.

During open, an existing sidecar stays reset and unbound until success, which binds and publishes the
final state just before commit; failed open exposes only closed defaults. Close and unexpected native destruction reset the
published values without freeing or disabling the sidecar.

The read contract lasts only while the C++ `Window` is alive: stop or join the renderer before destroying it. Desktop creates no render thread, schedules no frames, resizes no renderer resources, and invokes no renderer callbacks from native messages.
Renderer or worker results that need owner-thread Desktop mutation must go through an application-owned queue applied by the Window owner; there
is no library-owned worker and no generic `post()` API.

## Occlusion provider

A renderer that can determine occlusion attaches with `Renderer::attachOcclusionProvider(window)`;
`Renderer::hasOcclusionProvider(window)` reports attachment. This is separate from
`window.supports(Types::Capability::OcclusionReporting)`, the stable backend capability.

`Renderer::reportOcclusion(window, value)` updates the cached `Window::occluded()` and queues
`Types::Events::OcclusionChanged` only on change; a report without a provider is rejected.
`Renderer::detachOcclusionProvider()` restores non-occluded state, queueing the final transition if needed.
All three obey owner-thread rules.

## Pointer hit masks

Passive mask values are grouped under `Desktop::Types::Renderer`:

- `PointerHitMaskWord`
- `PointerHitMaskTarget`
- `Types::Result<PointerHitMaskTarget>`

`Renderer::requiredPointerHitMaskWords()` computes the packed storage requirement for a framebuffer extent. `beginPointerHitMaskUpdate()` returns the
current generation/extent target. `publishPointerHitMask()` copies a complete
mask on the Window owner thread. Renderer workers must send their result back
to that thread through application-owned synchronization. `clearPointerHitMask()`
removes a published mask and invalidates pending targets; `hasPointerHitMask()`
reports publication state.

Rows run top to bottom and each row starts on a new 32-bit word. Pixel `(x, y)`
uses bit `x % 32`, least-significant bit first, in word
`y * ceil(width / 32) + x / 32`. Unused high bits in every final row word must
be zero. For example, a 33-by-2 framebuffer needs four words. Empty extents or
overflow return zero from `requiredPointerHitMaskWords()`.

Every successful begin supersedes the previous uncommitted target. Publication
consumes its target on success; zero, stale, and already-consumed generations
return `Interrupted`. Incorrect word counts or nonzero padding return
`InvalidArgument`. A failed publication preserves the current mask. Framebuffer
changes, clear, native destruction, and close invalidate pending targets.

Check `Capability::PointerHitMask` before beginning. The current Win32 backend
does not advertise this capability, so `beginPointerHitMaskUpdate()` returns
`Unsupported`. Source-tree validation exercises storage through internal hooks;
public consumers cannot enable native routing through those hooks.

On a backend that advertises the capability, this owner-thread snippet publishes
a mask accepting only the central pixel:

```cpp
#include "desktop/renderer_bridge.h"
#include <vector>

const auto update = GameWIP::Desktop::Renderer::beginPointerHitMaskUpdate(window);
if (update.ok())
{
    using Word = GameWIP::Desktop::Types::Renderer::PointerHitMaskWord;
    const auto &target = *update;
    std::vector<Word> words(target.requiredWordCount, Word{0});
    const std::size_t width = target.framebufferSize.width;
    const std::size_t wordsPerRow = width / 32 + (width % 32 != 0);
    const std::size_t x = width / 2;
    const std::size_t y = target.framebufferSize.height / 2;
    words[y * wordsPerRow + x / 32] = Word{1} << (x % 32);
    const auto published = GameWIP::Desktop::Renderer::publishPointerHitMask(
        window, target.generation, words);
    // Inspect published; geometry may have changed since begin.
}
```

The bridge retains no renderer object and Renderer does not depend on Window; it only accepts explicit feedback.

## Display color

OS monitor color facts are not renderer feedback. Include `desktop/display_info.h` and call:

```cpp
const auto monitorColor = GameWIP::Desktop::Display::getColorInfo(monitor);
const auto windowColor = GameWIP::Desktop::Display::getColorInfo(window);
```

The renderer owns its swapchain/format/colorspace choices; Window reports only native display facts.
