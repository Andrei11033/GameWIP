@page desktop_chrome_and_pointer_input Custom chrome and pointer input

Custom chrome draws the application's own frame while keeping native resize, drag, menu, and
caption-button behavior; pointer policy decides which parts accept input.

## Declarative custom chrome

`DecorationMode::Custom` removes the visible system frame but keeps native hit-test roles for resizing, dragging, the system menu, and caption
buttons. `setCustomChromeLayout()` copies `LogicalRect` values, so caller spans may expire after the call.

On Win32, changing decoration, controls, ownership, pointer policy, or another frame property preserves state managed by separate
operations: style recomposition retains visibility, disabled, minimized, and maximized bits and unrelated extended styles
such as file-drop acceptance, so a visible Window stays eligible for taskbar and Alt+Tab across windowed, borderless, and
exclusive transitions.

System-button rectangles take precedence over draggable ones. Resize borders take precedence while the Window is resizable, normally presented,
and windowed. The region count must not exceed `maximumCustomChromeRegions`.

The native window procedure does bounded rectangle tests and never calls application code. Replace the whole layout when UI geometry changes.

## Pointer modes

`PointerInputMode` is `Normal`, whole-window `ClickThrough`, `AcceptRegions`, `IgnoreRegions`, or `HitMask`. Region modes need a non-empty logical
rectangle span, other modes an empty one. Rectangles use client-local half-open bounds.

Win32 click-through uses top-level `WS_EX_LAYERED | WS_EX_TRANSPARENT`, so
mouse input reaches arbitrary underlying desktop windows. `Normal` removes the transparent style and removes the layered
style when opacity does not need it.

Rectangular modes need a backend that can route input to arbitrary
underlying desktop windows. Win32 reports `PointerRegions` unavailable with a zero region
limit and returns `Unsupported` without copying the layout or changing state; it does not approximate with same-thread-only `HTTRANSPARENT`.

## Packed pointer mask

The optional @ref desktop_renderer_integration bridge stores one bit per physical framebuffer pixel: zero passes input through, one accepts
it. Window-generated generations protect asynchronous publication. Movement preserves the mask; framebuffer changes, clearing, native
destruction, close, and reopen invalidate it. Win32 has no documented mechanism for arbitrary
cross-application per-pixel pass-through, so it does not advertise `PointerHitMask`:
`Renderer::beginPointerHitMaskUpdate()` and `PointerInputMode::HitMask` return `Unsupported`.
Source-tree validation hooks exercise the storage path; it is not a public routing facility.

## Cursor display

`CursorMode` controls whether and how the pointer is presented or confined. A custom native cursor follows the same visibility rules as a
system `CursorShape`; hidden and relative modes retain the selection without displaying it. See @ref desktop_custom_cursors.

## Opacity, framebuffer alpha, and backdrop

Opacity, transparent framebuffer alpha, system backdrop, and pointer routing are separate.

- Opacity scales the complete Window.
- Transparent framebuffer is a creation-time request. On Windows 11 build 26100 or newer, Window enables `DWMWA_REDIRECTIONBITMAP_ALPHA`;
  renderer/native presentation must still supply valid premultiplied-alpha pixels.
- `BackdropEffect` uses `DWMWA_SYSTEMBACKDROP_TYPE` on Windows 11 build 22621 or newer and returns `Unsupported` for a non-`None` request on older
  builds.
- A visually transparent pixel still accepts input unless a supported pointer policy passes it through.

Capability flags reflect the running build. An unsupported creation request fails without a partially open Window; a runtime setter's status is authoritative.

## Related pages

- @ref desktop_coordinates_and_dpi
- @ref desktop_custom_cursors
- @ref desktop_renderer_integration
- @ref desktop_troubleshooting
