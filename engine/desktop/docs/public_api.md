@page desktop_public_api Public API

Desktop splits its installed headers by concept under one library and one passive `Types` tree.
Use this map to find the declaration that owns an operation.

## Header ownership

- `desktop/types.h` contains shared primitive/value vocabulary such as `WindowId`, geometry, DPI, limits, and `PresentationState`.
- `desktop/description.h` contains creation/configuration policy and `Types::Description`.
- `desktop/events.h` contains `Types::Events`, queued `Types::Event`, and calling-thread `Desktop::Events` pump operations.
- `desktop/display.h` contains fundamental `Types::Display::MonitorId`, display `Mode`, and mode queries.
- `desktop/display_info.h` is the opt-in rich monitor/color inspection surface.
- `desktop/cursor.h` is the opt-in custom native cursor resource and selection surface.
- `desktop/child_surface.h` is the opt-in managed native child-host resource, passive description, and typed event surface.
- `desktop/accessibility.h` is the opt-in immutable semantic snapshot, authoring/inspection, native provider, and bounded action bridge.
- `desktop/data_transfer.h` is the opt-in shared non-owning/owning transfer vocabulary for Clipboard and drag and drop.
- `desktop/drag_drop.h` is the opt-in native data target/source API, declarative regions, effects, and typed target events.
- `desktop/clipboard.h` is the opt-in stateless synchronous Clipboard service and its operation results.
- `desktop/dialogs.h` owns synchronous `Desktop::Dialogs` operations, the modeless `Desktop::ProgressDialog` resource, and `Types::Dialogs` values.
- `desktop/shell.h` owns shell capability queries and the owner-thread-affine shell event queue.
- `desktop/shell_jump_lists.h` owns stateless complete jump-list and recent-item publication.
- `desktop/shell_notifications.h` owns the owner-thread-affine notification center and notification publication.
- `desktop/shell_registration.h` owns stateless current-user file-extension and URI-scheme registration.
- `desktop/shell_taskbar.h` owns the opt-in per-Window taskbar binding, progress, overlay, and thumbnail-button publication.
- `desktop/shell_tray.h` owns the opt-in process-local tray-icon binding, recursive menu publication, and tray interaction delivery.
- `desktop/shell_types.h` contains the opt-in passive shell identities, targets, launch values, progress state, and event payloads.
- `desktop/window.h` assembles the normal Window object API and includes the fundamental headers above, but not rich display inspection, custom cursor
  resources, accessibility snapshots, Clipboard/data transfer, native drag and drop, dialogs, shell services, renderer integration, or native interop.
- `desktop/renderer_bridge.h` owns concurrent presentation-read opt-in, renderer feedback, and packed pointer publication.
- `desktop/native/win32.h` is explicit Win32 interoperability.

Passive data stays under `Types`; stateless domain operations live in the matching service namespace.

## Shell integration

`desktop/shell.h` is the shell entry point: `Desktop::Shell::getCapabilities()`/`supports()` report cached
backend support, and `Desktop::ShellEventQueue` is the shared fixed-capacity typed event queue. Its three
`open()` overloads use default storage, a nonzero capacity, or a borrowed non-empty `std::span<Types::Shell::Event>`.

`desktop/shell_types.h` is the passive entry point (identities, targets, launch values, progress state,
`Types::Shell::Event` payloads), usable without opening a resource. Shell resources live in focused headers
and bind to a `Desktop::ShellEventQueue`; `Window` carries no shell state:

- `desktop/shell_taskbar.h`: `Desktop::TaskbarItem`, one taskbar publication per open `Window` lifetime.
- `desktop/shell_tray.h`: `Desktop::TrayIcon`, process-local, independent of `Window`, with copied
  recursive menus and passive presentation before one-time queue binding.
- `desktop/shell_notifications.h`: `Desktop::NotificationCenter`, publish/update/dismiss of multiple process-local notifications.
- `desktop/shell_jump_lists.h`: stateless complete publication of tasks, categories, and recent items.
- `desktop/shell_registration.h`: stateless current-user file-extension and URI-scheme registration
  with ownership keys, atomic same-owner replacement, and conflict reporting.

See @ref desktop_shell for lifecycle, capability, ownership, and validation contracts.

## Accessibility

`Capability::Accessibility` reports backend support. The non-owning
`Window::accessibility()` façade requires `desktop/accessibility.h` to use its
definition; enablement is lazy and per-open-lifetime. Values belong under
`Types::Accessibility` and operations under `Desktop::Accessibility`.
Per-activation `features()` and per-node native pattern availability are distinct
from the global capability. See @ref desktop_accessibility for publication,
threading, ownership, transport, limits, and native mapping.

`Desktop::Accessibility::SnapshotBuilder` owns authoring storage,
`SnapshotReader` retains immutable published data, and `Facade` borrows a Window.
`Facade::enabled()` reports activation state; `SnapshotReader::isValid()` reports
whether a reader retains a snapshot.

## Library and Window capabilities

`getCapabilities()` and `supports()` report backend/environment capability. `Window::supports()` has the same capability semantics; it does not report
whether a custom cursor is selected or a renderer provider is attached. Custom cursor selection state is queried with
`hasCustomCursor()`, and renderer attachment state with `Renderer::hasOcclusionProvider()`.

## Window ownership and state

`Window` is default-constructible, non-copyable, and non-movable. `open()` establishes the owner thread and one process-local `Types::WindowId`.
`WindowId::isValid()` only reports a nonzero ID, not native liveness; `id()` retains the ended
identity during `NativeDestroyedPendingFinalize`. Thread and concurrent-read contracts: @ref desktop_lifecycle_events, @ref desktop_renderer_integration.

Cached getters issue no native queries. Expected failures are returned as `IO::Types::Status` or typed result structs; `close()` is
synchronous and reports through its status.

Plain cached properties use direct names (`visible()`, `focused()`,
`interactiveMoveResizeActive()`, `resizable()`, `userInteractionEnabled()`,
`ownedByCurrentThread()`). `isOpen()` keeps its prefix to distinguish the
lifetime query from `open()`; `isValid()` classifies identity and resource values;
presence/capability queries keep forms like `hasCloseRequest()`, `hasCustomCursor()`, `supports()`.

`hasCloseRequest()` reports sticky close intent; see @ref desktop_lifecycle_events.

## Configuration vocabulary

`Types::WindowMode` is the cached top-level Window presentation. Creation and
runtime changes use the type-safe `Types::WindowModeRequest` factories:
`WindowModes::windowed()`, `borderlessFullscreen()` (optionally with a
`MonitorId`), and `exclusiveFullscreen()` (optionally with a physical
`Display::Mode`, and optionally a monitor).
`Types::Description::fileDropEnabled` configures initial file-drop delivery; runtime state is queried by `Window::fileDropEnabled()` and changed by
`setFileDropEnabled()`.

Configuration/request types live with `description.h`; shared primitive values remain in `types.h`; live Window state and call-scoped layouts remain
in `window.h`.

Creation options must agree: `activation.requestFocus` requires both
`activation.visible` and `activation.focusable`, hidden Windows require
`PresentationState::Normal`, and `windowed.clientSize` must satisfy
`resizing.limits`. A non-resizable Window must also set
`resizing.controls.maximizable = false`. At runtime, disable that control with
`setControls()` before `setResizable(false)`; enable resizing before restoring
the maximize control. Contradictory options return `InvalidArgument`.

The concepts are intentionally separate: `windowed` is the logical initial and
restored client geometry; `initialWindowMode` is the presentation request; and
an exclusive request's `displayMode` is a physical monitor-mode request. Only
centered placement accepts `windowed.placement.monitor`; borderless and
exclusive requests may target a monitor independently. A missing target monitor
selects the current/nearest monitor, then the primary monitor. Initial pointer
policy accepts `Normal` or `ClickThrough`; region and mask modes require runtime
configuration and backend support.

## Custom cursors

`desktop/cursor.h` contains the shared `Cursor` resource, passive image values under `Types::Cursor`, `createCursor()` overloads, and the free
`setCursor()` and `hasCustomCursor()` Window operations. It remains separate from `desktop/window.h` because application-provided cursor images are an
opt-in resource surface. See @ref desktop_custom_cursors for image validation, DPI selection, lifetime, and system-shape interaction.

## Child surfaces

`desktop/child_surface.h` contains the non-movable `ChildSurface` RAII owner and passive values under `Types::ChildSurface`. It reuses shared
`Types::LifetimeState`, `Types::Events::QueueInfo`, and `Types::Events::StorageKind`; there is no public ChildSurface ID. See
@ref desktop_child_surfaces for native-host ownership, parent loss, logical geometry, DPI, queues, and sibling ordering.

## Clipboard and data transfer

`desktop/data_transfer.h` owns `Types::DataTransfer` views and owning values for UTF-8 text, ordered paths, sRGB straight-alpha RGBA8 images, and
arbitrary named opaque bytes. `desktop/clipboard.h` owns the stateless `Desktop::Clipboard` operations: query/read calls return `Types::Result<T>`
with an optional value, while writes and clear use progress-specific result types. Clipboard adds no `Window` capability because operations require no
Window object or event pump. See @ref desktop_clipboard for timeout, transaction, native ownership, and custom-format semantics.

`desktop/drag_drop.h` owns the non-movable `DragDropTarget`, passive `Types::DragDrop` contract, and synchronous `DragDrop::beginDrag()`. See
@ref desktop_drag_drop for lifecycle, queue, region, effect, and OLE rules.

## Events

Event payloads live under `Types::Events` and do not repeat the `Event` suffix. Examples are `CloseRequested`, `ClientPositionChanged`,
`InteractiveMoveResizeStarted`, `InteractiveMoveResizeEnded`, `FilesDropped`, and `NativeDestroyed`.

`Types::Events::Payload` is the payload variant. Every queued envelope (Window,
ChildSurface, DragDrop, Shell) carries a monotonic sequence plus `kind()`,
`is<T>()`, and `getIf<T>()`. Queue metadata: `Types::Events::StorageKind`, `QueueInfo`; pump results: `Types::Events::PumpResult`.

Pumping lives in `Desktop::Events` (@ref desktop_lifecycle_events):

- `Events::poll()` is non-blocking.
- `Events::wait(timeout)` accepts zero, a finite non-negative timeout, or `Events::kWaitForever`.
- `Events::kDefaultQueueCapacity` is used by the default `Window::open()` overload.
- `Events::owned(capacity)` and `Events::borrowed(storage)` make queue ownership explicit for the configured `open()` overload.

Explicit `close()` emits no destruction event; unexpected native destruction queues `Types::Events::NativeDestroyed`. See @ref desktop_lifecycle_events.

## Displays

Fundamental display mode operations are under `Desktop::Display`:

- `getModes()`
- `getCurrentMode()`
- `getPreferredMode()`

All display and rich monitor/color queries return `Types::Result<T>`; test
`ok()` before dereferencing, as failed queries expose no default-looking payload.

Rich inspection from `desktop/display_info.h` adds:

- `getMonitors()`
- `getPrimaryMonitor()`
- `getMonitor()`
- `getColorInfo(Types::Display::MonitorId)`
- `getColorInfo(const Window&)`

OS HDR/WCG/color facts belong to Desktop display inspection, not to the renderer bridge.

## Text contract

Public Desktop text is UTF-8, converted strictly to/from UTF-16 at native boundaries by the Unicode
foundation library. Native title operations also reject embedded U+0000 (Win32 consumes
NUL-terminated strings).

## Renderer bridge

`Desktop::Renderer` holds renderer-to-Window integration only: occlusion-provider attachment/reporting, pointer hit-mask publication, and the
concurrent-read opt-in (`enableConcurrentPresentationReads()`,
`concurrentPresentationReadsEnabled()`). Passive values live under `Desktop::Types::Renderer`.
See @ref desktop_renderer_integration.
