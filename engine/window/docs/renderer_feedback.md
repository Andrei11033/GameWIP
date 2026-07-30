@page window_renderer_feedback Renderer feedback

Window owns the cached occlusion state and its event, but only a renderer can reliably determine whether presenting a particular surface is occluded. The installed adapter keeps that ownership boundary explicit:

```cpp
#include "window/integration/renderer_feedback.h"
```

## Provider lifecycle

Call `GameWIP::Window::Integration::Renderer::attachOcclusionProvider()` after both the Window and its renderer-owned surface exist. Exactly one provider may be attached to an open Window. A duplicate attachment reports `AlreadyOpen`; use `detachOcclusionProvider()` before replacing the provider.

Attachment, reporting, and detachment belong to the Window owner thread. Calls from another thread report `ResourceBusy`. Calls on a closed Window report `NotOpen`, and closing implicitly discards the provider and cached state without producing an event.

Detachment is idempotent while the Window is open. If the last reported state was occluded, detachment first restores `isOccluded()` to false and attempts one final `OcclusionChangedEvent{false}`.

## Capability meaning

`GameWIP::Window::supports(Types::Capability::OcclusionReporting)` remains false because the platform backend cannot provide this signal on its own. `window.supports(Types::Capability::OcclusionReporting)` becomes true only while a renderer provider is attached to that Window.

The operation status remains authoritative. `reportOcclusion()` reports `NotOpen` when no provider is attached, even if a stale capability observation suggested otherwise.

## Reporting transitions

The renderer records the authoritative result of presentation. Its owner-thread integration step then calls:

```cpp
namespace RendererFeedback = GameWIP::Window::Integration::Renderer;

if (!RendererFeedback::attachOcclusionProvider(window).ok())
    return false;

// Called on the Window owner thread after consuming a renderer presentation result.
if (!RendererFeedback::reportOcclusion(window, presentWasOccluded).ok())
    return false;
```

`reportOcclusion()` updates `Window::isOccluded()` before attempting to enqueue `OcclusionChangedEvent`. Repeating the same value succeeds without adding an event. If the fixed queue is full, the notification may be dropped and `eventQueueInfo().droppedEvents` increases, but `isOccluded()` still contains the latest truth.

The adapter intentionally provides no cross-thread mailbox. A render thread may record its result in renderer-owned synchronization, but the application or Renderer bridge must forward that value while pumping work on the Window owner thread. This keeps Window free of polling, worker threads, callbacks, and renderer synchronization policy.

## Surface ownership

Renderer surface or swapchain creation remains a Renderer responsibility. Obtain the non-owning platform handle through the opt-in native adapter described by @ref window_native_interop, create or destroy the surface in Renderer, and use this feedback adapter only for state that Window exposes to consumers.
