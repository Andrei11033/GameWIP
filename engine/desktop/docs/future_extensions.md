@page desktop_future_extensions Future extension boundaries

`desktop/window.h` models only native top-level windows. Additive platform capabilities belong in
opt-in headers within the existing `GameWIP::Desktop` target and package, not new top-level engine
libraries or a larger umbrella header. Active work is tracked in GitHub issues, not here.

## Platform shell boundary

Taskbar, notifications, tray icons and menus, jump lists, file
associations, and recent files live in the focused `desktop/shell*.h` headers within `GameWIP::Desktop`, with no
separate library and no shell state in `Window`. Future work is limited to advertised optional capabilities and
more backends; the ownership and queue contracts are stable.

## Accessibility provider bridge

A future UI system owns the semantic tree. `desktop/accessibility.h` copies immutable snapshots from it and
provides native providers and bounded transport, not a second editable model (@ref desktop_accessibility). Future backends keep that ownership,
advertise feature differences, and add no toolkit or renderer dependencies.

## Native pointer-mask routing

The packed mask bridge exists in `desktop/renderer_bridge.h`. Cross-application rectangular and per-pixel routing remains
backend work and stays `Unsupported` until a documented stable native API can satisfy it.

## Data transfer, drag and drop, and dialogs

Clipboard and the shared transfer vocabulary live in `desktop/clipboard.h` and
`desktop/data_transfer.h`; `desktop/drag_drop.h` reuses that contract. Dialogs and modeless
progress use `desktop/dialogs.h` (@ref desktop_dialogs). Lightweight file-drop events remain the core path-only capability.

Renderer surface lifetime, swapchains, and synchronization remain Renderer responsibilities; the bridge's boundaries are in @ref desktop_renderer_integration.

Text input and IME belong to the future Input API with an internal native Window/Input bridge. Application and editor menus belong to the
future UI system; only tray-icon menus belong to Desktop shell integration.

## Related pages

- @ref desktop_public_api
- @ref desktop_shell
- @ref desktop_native_interop
- @ref desktop_renderer_integration
- @ref project_extending
