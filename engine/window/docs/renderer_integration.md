@page window_renderer_integration Renderer integration

Window owns native display facts and cached Window state. Renderer owns presentation policy and is the only component that can reliably determine whether a particular surface is occluded during presentation. The installed adapter makes those ownership boundaries explicit:

```cpp
#include <window/renderer.h>
```

## Provider lifecycle

Call `GameWIP::Window::Renderer::attachOcclusionProvider()` after both the Window and its renderer-owned surface exist. Exactly one provider may be attached to an open Window.

Attachment, reporting, and detachment belong to the Window owner thread. Calls from another thread report `ResourceBusy`. Calls on a closed Window report `NotOpen`, and closing implicitly discards the provider and cached state without producing an event.

Detachment is idempotent while the Window is open. If the last reported state was occluded, detachment first restores `isOccluded()` to false and attempts one final `OcclusionChangedEvent{false}`.

## Capability meaning

`GameWIP::Window::supports(Types::Capability::OcclusionReporting)` remains false because the platform backend cannot provide this signal on its own. `window.supports(Types::Capability::OcclusionReporting)` becomes true only while a renderer provider is attached to that Window.

The operation status remains authoritative. `reportOcclusion()` reports `NotOpen` when no provider is attached, even if the caller previously cached a positive capability result.

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

`reportOcclusion()` updates `Window::isOccluded()` before attempting to enqueue `OcclusionChangedEvent`. Repeating the current value succeeds without adding an event. If the fixed queue is full, the notification may be dropped and `eventQueueInfo().droppedEvents` increases, but `isOccluded()` still reports the current state.

The adapter intentionally provides no cross-thread mailbox. A render thread may record its result in renderer-owned synchronization, but the application or Renderer bridge must forward that value while pumping work on the Window owner thread. This keeps Window free of polling, worker threads, callbacks, and renderer synchronization policy.

## Display color information

`getDisplayColorInfo()` queries current operating-system color facts for one connected `MonitorId`. `getWindowDisplayColorInfo()` reads an open Window's cached monitor on its owner thread and performs the same query.

A disconnected identity reports `NotFound`, a closed Window reports `NotOpen`, and a wrong-thread Window query reports `ResourceBusy`.

`DisplayColorInfo` separates support, current HDR enablement, and active color-space classification:

- `Srgb` means standard dynamic range.
- `WideColorGamut` means advanced-color SDR with a wider gamut.
- `Hdr10Pq` means HDR10/PQ output.
- `Unknown` means the native state could not be classified reliably.

Zero bit depth, luminance, or SDR white level means the optional metadata was unavailable. Window never infers HDR from bit depth.

On Win32, DisplayConfig supplies HDR/WCG support, enablement, active mode, channel precision, and SDR white level when the running OS supports those queries. `IDXGIOutput6` supplies current output color space and reliable luminance metadata when available. The Windows 11 advanced-color query is selected at runtime with the documented Windows 10 query as fallback; no Windows App SDK dependency is introduced.

The first color query lazily establishes native change observation on that owner thread. Continue pumping Window events. Re-query after `DisplayConfigurationChangedEvent` or `MonitorChangedEvent`, after Window reopen or display reconnect, and after an HDR or advanced-color toggle. No per-frame native metadata query is required.

Window only reports display facts. Renderer still owns swapchain format and presentation color space, HDR metadata, tone mapping, SDR/HDR rendering policy, and mastering decisions. These functions neither change operating-system HDR state nor configure a swapchain or apply an ICC profile.

## Surface ownership

Renderer surface or swapchain creation remains a Renderer responsibility. Obtain the non-owning platform handle through the opt-in native adapter described by @ref window_native_interop, create or destroy the surface in Renderer, and use this feedback adapter only for state that Window exposes to consumers.

## Packed pointer hit masks

`beginPointerHitMaskUpdate()` returns a Window-generated nonzero generation, a coherent physical framebuffer size, and the exact required word count. Renderer performs GPU work and asynchronous readback outside Window, then passes that generation and the completed words to `publishPointerHitMask()`. Beginning a newer update supersedes older unfinished work; clear, framebuffer changes, native destruction, close, finalization, and reopen invalidate it.

The packed format is row-major with a top-left origin and one physical-framebuffer pixel per bit. Bits are stored least-significant first within each 64-bit word. A zero bit passes input through; a one bit accepts input. Unused trailing bits must be zero. Publication is owner-thread-only and copies the data before committing it. Failures preserve the previous active mask, same-size updates reuse its allocation, clearing may retain capacity, movement preserves the mask, and framebuffer changes invalidate it. A missing or invalid mask defaults to interactive input.

The Win32 backend does not advertise `PointerHitMask`: documented Win32 hit testing does not provide stable selected-pixel pass-through to arbitrary underlying applications without visual holes or synthetic input. Selecting `HitMask` and beginning an update therefore return `Unsupported` in production while the deterministic Window-owned bridge remains testable.

Native per-pixel desktop routing is a separate capability. Publishing data does not make an unsupported backend claim passthrough support.

## Related pages

- @ref window_chrome_and_pointer_input
- @ref window_coordinates_and_dpi
- @ref window_native_interop
