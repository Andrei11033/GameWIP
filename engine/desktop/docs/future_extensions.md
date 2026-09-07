@page desktop_future_extensions Future extension boundaries

The core `desktop/window.h` contract intentionally models only native top-level windows. Additive platform capabilities must preserve that focused
ownership model: they belong in opt-in headers within the existing `GameWIP::Desktop` target and `Window` package, not in new top-level engine
libraries or an expanded umbrella header. Active implementation work remains tracked in GitHub issues rather than on this page.

## Platform shell integration

Taskbar features, notifications, tray icons and their operating-system menus, jump lists, file associations, recent files, and application shell
policy belong in a focused Window shell header. They must not introduce a separate platform-services library or place shell-specific state in every
core `Window` instance.

## Accessibility provider bridge

A future UI system owns the semantic accessibility tree. Window may expose a focused platform bridge that publishes snapshots from that owner, but it
must not invent or retain a second semantic model.

## Native pointer-mask routing

The packed Renderer-to-Window mask bridge exists in `desktop/renderer_bridge.h`. Genuine cross-application rectangular and per-pixel routing remains
backend work and stays `Unsupported` until a documented stable native API can satisfy the contract.

## Data transfer, drag and drop, and dialogs

Clipboard and its shared transfer vocabulary are implemented in the focused `desktop/clipboard.h` and `desktop/data_transfer.h` headers. Native drag
and drop reuses that contract through `desktop/drag_drop.h` rather than duplicating format, ownership, or lifetime rules. Native dialogs still belong
in their own focused Window headers. Existing file-drop events remain the narrow core path-only capability.

Renderer attachment already has an explicit non-owning native-handle boundary, and renderer occlusion reporting has an explicit owner-thread
feedback boundary. Concurrent presentation reads have a separate explicit one-way opt-in and do not transfer mutation or resource ownership.
Renderer surface lifetime, swapchains, and synchronization remain Renderer responsibilities.

Text input and IME belong to the future Input public API with an internal native Window/Input bridge. Application and editor menus belong to the
future UI system; only tray-icon operating-system menus belong to Window shell integration. Window does not freeze either owning library's policy in
advance.

## Related pages

- @ref desktop_public_api
- @ref desktop_native_interop
- @ref desktop_renderer_integration
- @ref project_extending
