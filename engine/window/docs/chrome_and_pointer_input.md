@page window_chrome_and_pointer_input Custom chrome and pointer input

Custom chrome lets an application draw its own frame while preserving native
resize, drag, menu, and caption-button behavior. Pointer policy then decides
which parts of that custom surface accept input.

## Declarative custom chrome

`DecorationMode::Custom` removes the visible system frame while retaining native hit-test roles for resizing, dragging, the system menu, and caption
buttons. `setCustomChromeLayout()` copies `LogicalRect` values; caller spans may expire when the call returns.

On Win32, changing decoration, controls, ownership, pointer policy, or another configurable frame property preserves native state managed by separate
operations. In particular, style recomposition retains visibility, disabled, minimized, and maximized bits, and it retains unrelated extended styles
such as file-drop acceptance. A visible Window therefore remains eligible for taskbar and Alt+Tab presentation across windowed, borderless, and
exclusive style transitions.

System-button rectangles take precedence over draggable rectangles. Resize borders take precedence while the Window is resizable, normally presented,
and windowed. The total region count must not exceed `maximumCustomChromeRegions`.

The native window procedure performs bounded rectangle tests and never calls application code. Replace the complete layout when UI geometry changes.

## Pointer modes

`PointerInputMode` contains `Normal`, whole-window `ClickThrough`, `AcceptRegions`, and `IgnoreRegions`. Region modes require a non-empty logical
rectangle span; non-region modes require an empty span. Rectangles use client-local half-open bounds.

Win32 implements whole-window click-through with the documented top-level layered-window hit-testing combination `WS_EX_LAYERED | WS_EX_TRANSPARENT`;
mouse input reaches arbitrary underlying desktop windows. Returning to `Normal` removes the transparent hit-testing style and removes the layered
style when opacity does not otherwise need it.

The portable enum does not guarantee native region support. Rectangular modes are available only when the backend can route input to arbitrary
underlying desktop windows. Win32 reports `PointerRegions` as unavailable, exposes a zero region limit, and returns `Unsupported` without copying the
layout or changing Window state. It does not use same-thread-only `HTTRANSPARENT` as an approximation.

## Packed pointer mask

The optional @ref window_renderer_integration bridge stores one bit per physical framebuffer pixel. A zero bit passes input through; a one bit accepts
input. Window-generated generations protect asynchronous publication. Movement preserves the mask. Framebuffer changes, explicit clearing, native
destruction, close, and reopen invalidate it. Win32 reports the capability as unavailable because no documented native mechanism provides arbitrary
cross-application per-pixel pass-through.

Window storage of a mask does not imply that the backend can perform genuine per-pixel desktop routing. The Win32 backend does not advertise per-pixel
routing; publication remains an integration and storage contract rather than a pass-through guarantee.

## Cursor display

`CursorMode` controls whether and how the pointer is presented or confined. A selected custom native cursor follows the same visibility rules as a
system `CursorShape`; hidden and relative modes retain the selection without displaying it. System shapes and application-provided image resources
otherwise remain separate surfaces. See @ref window_custom_cursors for selection and restoration behavior.

## Opacity, framebuffer alpha, and backdrop

Whole-window opacity, transparent framebuffer alpha, system backdrop, and pointer routing are separate concepts.

- Opacity scales the complete Window.
- Transparent framebuffer is a creation-time request. On Windows 11 build 26100 or newer, Window enables `DWMWA_REDIRECTIONBITMAP_ALPHA`;
  renderer/native presentation must still supply valid premultiplied-alpha pixels.
- `BackdropEffect` uses `DWMWA_SYSTEMBACKDROP_TYPE` on Windows 11 build 22621 or newer and returns `Unsupported` for a non-`None` request on older
  builds.
- A visually transparent pixel still accepts input unless a supported pointer policy passes it through.

Capability flags reflect the running Windows build. An unsupported creation request fails without leaving a partially open Window, and the status
returned by a runtime setter remains authoritative.

## Related pages

- @ref window_coordinates_and_dpi
- @ref window_custom_cursors
- @ref window_renderer_integration
- @ref window_troubleshooting
