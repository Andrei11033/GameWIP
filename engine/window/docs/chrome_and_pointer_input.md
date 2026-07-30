@page window_chrome_and_pointer_input Custom chrome and pointer input

## Declarative custom chrome

`DecorationMode::Custom` removes the visible system frame while retaining native resize, move, snap, system-menu, and caption-button hit testing. The application supplies logical client rectangles through `setCustomChromeLayout()`.

Caption and system-button rectangles override draggable rectangles. Draggable rectangles override ordinary client behavior. Resize borders remain native while the Window is resizable, normally presented, and windowed. Rectangle arrays are copied; the caller's spans may expire when the call returns. The total region count must not exceed `maximumCustomChromeRegions`.

The native window procedure performs only bounded rectangle tests. It never calls application code. UI code may replace the complete declarative layout whenever its own layout changes.

## Pointer input policy

`PointerInputMode::Normal` accepts input across the client. `ClickThrough` passes the complete client through to windows below. `AcceptRegions` accepts only inside copied rectangles; `IgnoreRegions` passes through inside copied rectangles.

Region modes require a non-empty layout. Non-region modes require an empty region span. The count is bounded by `maximumPointerInputRegions`. Coordinates are logical client coordinates and use half-open rectangle bounds.

Custom caption/system buttons and resize borders take precedence over pointer pass-through. This keeps native window manipulation available even for overlay-style clients.

## Layering and opacity

Opacity is a global value in `[0, 1]`. A transparent framebuffer is a creation-time compositing request. Backdrop blur is a separate runtime effect and may be unsupported for a particular Window state; inspect `Window::supports()` and the setter status.

Pointer click-through is hit-test policy, not opacity. A fully transparent Window still accepts pointer input unless a click-through policy is selected.

## Current boundary

Only whole-window and rectangular policies are implemented. Renderer-driven alpha-mask hit testing is intentionally deferred; see @ref window_future_extensions.
