@page desktop_renderer_integration Renderer integration

`desktop/renderer_bridge.h` is an optional integration surface. It contains concurrent presentation-read opt-in, renderer-to-Window feedback, and
renderer-published pointer data. Operating-system display/HDR inspection lives in `desktop/display_info.h`.

## Concurrent presentation reads

By default, renderer-facing presentation getters are ordinary owner-thread cached getters. Call
`Renderer::enableConcurrentPresentationReads(window)` on an open Window's owner thread before starting renderer reads. It lazily allocates a stable,
one-way publication sidecar and immediately publishes the current authoritative state. Repeated calls are idempotent; close/reopen reuses it.

The enable operation returns `NotOpen` for a closed Window, `ResourceBusy` on the wrong thread, and `OutOfMemory` when the lazy allocation fails.
`Renderer::concurrentPresentationReadsEnabled(window)` reports the one-way object state and remains true across close/reopen. Do not race enablement
with getter reads or destruction.

Once enabled, `clientSize()`, `framebufferSize()`, `contentScale()`, `effectiveDpi()`, `currentMonitor()`, `presentationState()`, `minimized()`,
`maximized()`, `visible()`, `interactiveMoveResizeActive()`, and `occluded()` are safe for renderer-thread reads while the owner handles native
messages. Individual compound values cannot tear; separate getter calls may observe successive states.

Occlusion reporting remains usable without this facility: owner-thread `occluded()` reads authoritative renderer-integration state. When concurrent
reads are enabled, occlusion transitions are additionally mirrored to the atomic publication. Reads remain observational and never queue events.

During open, an existing sidecar stays reset and unbound while native candidate creation is uncommitted. Successful open binds and publishes the
final authoritative state immediately before commit; failed open exposes only closed defaults. Close and unexpected native destruction reset the
published values without freeing or disabling the sidecar.

This read contract lasts only while the C++ `Window` object remains alive. The application must stop or join the renderer before destroying the
object. Desktop does not create a render thread, schedule frames, resize renderer resources, or invoke renderer callbacks from native messages.

## Occlusion provider

A renderer that can determine presentation occlusion attaches explicitly with `Renderer::attachOcclusionProvider(window)`.
`Renderer::hasOcclusionProvider(window)` reports current attachment state. This state is deliberately separate from
`window.supports(Types::Capability::OcclusionReporting)`, which always answers the stable backend capability question.

After attachment, `Renderer::reportOcclusion(window, value)` updates the cached `Window::occluded()` state and queues
`Types::Events::OcclusionChanged` only when the value changes. `Renderer::detachOcclusionProvider()` clears the provider and restores non-occluded
cached state, queueing the final transition when needed.

Attachment, reporting, and detachment obey Window owner-thread rules. A report without an attached provider is rejected.

## Pointer hit masks

Passive mask values are grouped under `Desktop::Types::Renderer`:

- `PointerHitMaskWord`
- `PointerHitMaskTarget`
- `PointerHitMaskResult`

`Renderer::requiredPointerHitMaskWords()` computes the packed storage requirement for a framebuffer extent. `beginPointerHitMaskUpdate()` returns the
current generation/extent target. The renderer publishes a complete packed mask with `publishPointerHitMask()`; stale generation or incorrect storage
is rejected. `clearPointerHitMask()` removes a published mask and `hasPointerHitMask()` reports publication state.

The bridge retains no renderer object and does not make Renderer depend on Window. It only accepts explicit feedback at the point where the renderer
already has the information.

## Display color

OS monitor color facts are not renderer feedback. Include `desktop/display_info.h` and call:

```cpp
const auto monitorColor = GameWIP::Desktop::Display::getColorInfo(monitor);
const auto windowColor = GameWIP::Desktop::Display::getColorInfo(window);
```

Renderer remains responsible for its own swapchain/format/colorspace choices; Window reports the native desktop/display facts only.
