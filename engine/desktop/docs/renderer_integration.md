@page desktop_renderer_integration Renderer integration

`desktop/renderer_bridge.h` is an optional integration surface. It contains only renderer-to-Window feedback and renderer-published pointer data;
operating-system display/HDR inspection lives in `desktop/display_info.h`.

## Occlusion provider

A renderer that can determine presentation occlusion attaches explicitly with `Renderer::attachOcclusionProvider(window)`.
`Renderer::hasOcclusionProvider(window)` reports current attachment state. This state is deliberately separate from
`window.supports(Capability::OcclusionReporting)`, which always answers the stable backend capability question.

After attachment, `Renderer::reportOcclusion(window, value)` updates the cached `Window::occluded()` state and queues
`Types::Events::OcclusionChanged` only when the value changes. `Renderer::detachOcclusionProvider()` clears the provider and restores non-occluded
cached state, queueing the final transition when needed.

Attachment, reporting, and detachment obey Window owner-thread rules. A report without an attached provider is rejected.

## Pointer hit masks

Passive mask values are grouped under `Window::Types::Renderer`:

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
