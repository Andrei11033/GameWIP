@page window_renderer_integration Renderer integration

Window owns the cached occlusion state and its event, but only a renderer can reliably determine whether presenting a particular surface is occluded. The installed adapter keeps that ownership boundary explicit:

```cpp
#include <window/renderer.h>
```

## Provider lifecycle

Call `GameWIP::Window::Renderer::attachOcclusionProvider()` after both the Window and its renderer-owned surface exist. Exactly one provider may be attached to an open Window.

Attachment, reporting, and detachment belong to the Window owner thread. Calls from another thread report `ResourceBusy`. Calls on a closed Window report `NotOpen`, and closing implicitly discards the provider and cached state without producing an event.

Detachment is idempotent while the Window is open. If the last reported state was occluded, detachment first restores `isOccluded()` to false and attempts one final `OcclusionChangedEvent{false}`.

## Capability meaning

`GameWIP::Window::supports(Types::Capability::OcclusionReporting)` remains false because the platform backend cannot provide this signal on its own. `window.supports(Types::Capability::OcclusionReporting)` becomes true only while a renderer provider is attached to that Window.

The operation status remains authoritative. `reportOcclusion()` reports `NotOpen` when no provider is attached, even if a stale capability observation suggested otherwise.

## Reporting transitions

The renderer records the authoritative result of presentation. Its owner-thread integration step then calls:

```cpp
namespace RendererFeedback = GameWIP::Window::Renderer;

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

## Packed pointer hit masks

`beginPointerHitMaskUpdate()` returns a Window-generated nonzero generation, a coherent physical framebuffer size, and the exact required word count. Renderer performs GPU work and asynchronous readback outside Window, then passes that generation and the completed words to `publishPointerHitMask()`. Beginning a newer update supersedes older unfinished work; clear, framebuffer changes, native destruction, close, finalization, and reopen invalidate it.

The packed format is row-major, top-left origin, one physical-framebuffer pixel per bit, least-significant bit first in each 64-bit word. Zero requests pass-through and one is interactive; unused trailing bits must be zero. Publication is owner-thread-only and copies before committing. Failures preserve the previous active mask, same-size updates reuse its allocation, clear may retain capacity, movement preserves it, and framebuffer changes invalidate it. Missing or invalid masks fall back to interactive.

The Win32 backend does not advertise `PointerHitMask`: documented Win32 hit testing does not provide stable selected-pixel pass-through to arbitrary underlying applications without visual holes or synthetic input. Selecting `HitMask` and beginning an update therefore return `Unsupported` in production while the deterministic Window-owned bridge remains testable.

Native per-pixel desktop routing is a separate capability. Publishing data does not make an unsupported backend claim passthrough support.

## Related pages

- @ref window_chrome_and_pointer_input
- @ref window_coordinates_and_dpi
- @ref window_native_interop
