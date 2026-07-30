@page window_chrome_and_pointer_input Custom chrome and pointer input

## Declarative custom chrome

`DecorationMode::Custom` removes the visible system frame while retaining native hit-test roles for resizing, dragging, the system menu, and caption buttons. `setCustomChromeLayout()` copies `LogicalRect` values; caller spans may expire when the call returns.

System-button rectangles take precedence over draggable rectangles. Resize borders take precedence while the Window is resizable, normally presented, and windowed. The total region count must not exceed `maximumCustomChromeRegions`.

The native window procedure performs bounded rectangle tests and never calls application code. Replace the complete layout when UI geometry changes.

## Pointer modes

`PointerInputMode` contains `Normal`, whole-window `ClickThrough`, `AcceptRegions`, and `IgnoreRegions`. Region modes require a non-empty logical rectangle span; non-region modes require an empty span. Rectangles use client-local half-open bounds.

Win32 implements whole-window click-through with the documented top-level layered-window hit-testing combination `WS_EX_LAYERED | WS_EX_TRANSPARENT`; mouse input reaches arbitrary underlying desktop windows. Returning to `Normal` removes the transparent hit-testing style and removes the layered style when opacity does not otherwise need it.

The portable enum does not by itself prove native region support. Rectangular modes may be enabled only when the backend can route to arbitrary underlying desktop windows. Win32 does not advertise `PointerRegions`, exposes a zero region limit, and returns `Unsupported` without copying or changing state. It does not use same-thread-only `HTTRANSPARENT` as an approximation.

## Packed pointer mask

The optional @ref window_renderer_integration bridge stores one bit per physical framebuffer pixel. Zero requests pass-through and one accepts. Window-generated generations protect asynchronous publication. Movement preserves the mask; framebuffer changes, clear, destruction, close, and reopen invalidate it. Win32 keeps the capability false because it lacks documented arbitrary cross-application per-pixel pass-through.

Window storage of a mask does not imply that the backend can perform genuine per-pixel desktop routing. The Win32 backend does not advertise per-pixel routing; publication remains an integration/storage contract rather than a passthrough guarantee.

## Opacity, framebuffer alpha, and backdrop

Whole-window opacity, transparent framebuffer alpha, system backdrop, and pointer routing are separate concepts.

- Opacity scales the complete Window.
- Transparent framebuffer is a creation-time request. On Windows 11 build 26100 or newer, Window enables `DWMWA_REDIRECTIONBITMAP_ALPHA`; renderer/native presentation must still supply valid premultiplied-alpha pixels.
- `BackdropEffect` uses `DWMWA_SYSTEMBACKDROP_TYPE` on Windows 11 build 22621 or newer and returns `Unsupported` for a non-`None` request on older builds.
- A visually transparent pixel still accepts input unless a supported pointer policy passes it through.

Capability flags are computed from the actual runtime Windows build. Unsupported creation requests are rejected before HWND/class/dispatcher allocation, and runtime setter status remains authoritative.

## Related pages

- @ref window_coordinates_and_dpi
- @ref window_renderer_integration
- @ref window_troubleshooting
