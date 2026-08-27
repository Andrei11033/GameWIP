@page window_public_api Public API

Window splits its installed headers by concept while keeping one library and
one passive `Types` tree. Use this map to find the declaration that owns a
lifecycle, event, display, renderer, or native-interop operation.

## Header ownership

Window exposes one public `GameWIP::Window::Types` tree and focused headers by conceptual ownership:

- `window/types.h` contains shared primitive/value vocabulary such as `WindowId`, geometry, DPI, limits, and `PresentationState`.
- `window/description.h` contains creation/configuration policy and `Types::Description`.
- `window/events.h` contains `Types::Events`, queued `Types::Event`, and calling-thread `Window::Events` pump operations.
- `window/display.h` contains fundamental `Types::Display::MonitorId`, display `Mode`, and mode queries.
- `window/display_info.h` is the opt-in rich monitor/color inspection surface.
- `window/cursor.h` is the opt-in custom native cursor resource and selection surface.
- `window/window.h` assembles the normal Window object API and includes the fundamental headers above, but not rich display inspection, custom cursor
  resources, renderer integration, or native interop.
- `window/renderer_bridge.h` is the opt-in renderer feedback bridge.
- `window/native/win32.h` is explicit Win32 interoperability.

Passive data stays under `Types`; stateless domain operations live in the matching service namespace.

## Library and Window capabilities

`getCapabilities()` and `supports()` report backend/environment capability. `Window::supports()` has the same capability semantics; it does not report
whether a custom cursor is selected or a renderer provider is currently attached. Custom cursor selection state is queried with
`hasCustomCursor()`, and renderer attachment state with `Renderer::hasOcclusionProvider()`.

## Window ownership and state

`Window` is default-constructible, non-copyable, and non-movable. `open()` establishes one owner thread and one process-local `Types::WindowId`.
`WindowId::isValid()` reports whether an ID is nonzero. Except `wakeEventWait()`, open-object operations require the owner thread.

Cached getters do not issue native queries. Expected failures are returned as `IO::Types::Status` or typed result structs. Explicit `close()` is
synchronous and observable through its return status.

`hasCloseRequest()` reports sticky close intent. `requestClose()` queues one `Types::Events::CloseRequested` transition and `clearCloseRequest()`
clears the sticky flag.

## Configuration vocabulary

`Types::Mode` is the top-level Window mode. `Types::Controls` describes standard close/minimize/maximize availability.
`Types::Description::fileDropEnabled` configures initial file-drop delivery; runtime state is queried by `Window::isFileDropEnabled()` and changed by
`setFileDropEnabled()`.

Configuration/request types live with `description.h`; shared primitive values remain in `types.h`; live Window state and call-scoped layouts remain
in `window.h`.

## Custom cursors

`window/cursor.h` contains the shared `Cursor` resource, passive image values under `Types::Cursor`, `createCursor()` overloads, and the free
`setCursor()` and `hasCustomCursor()` Window operations. It remains separate from `window/window.h` because application-provided cursor images are an
opt-in resource surface. See @ref window_custom_cursors for image validation, DPI selection, lifetime, and system-shape interaction.

## Events

Event payloads live under `Types::Events` and do not repeat the `Event` suffix. Examples are `CloseRequested`, `ClientPositionChanged`,
`FilesDropped`, and `NativeDestroyed`.

`Types::Events::Payload` is the payload variant. `Types::Event` remains the queued envelope and carries a monotonic sequence plus typed `getIf<T>()`
access. Queue metadata lives in `Types::Events::StorageKind` and `QueueInfo`; pump results use `Types::Events::PumpResult`.

Calling-thread pumping is grouped under `Window::Events`:

- `Events::poll()` is non-blocking.
- `Events::wait(timeout)` accepts zero, a finite non-negative timeout, or `Events::kWaitForever`.
- `Events::kDefaultQueueCapacity` is used by the default `Window::open()` overload.

Explicit `close()` emits no destruction event. Unexpected native destruction queues `Types::Events::NativeDestroyed`, changes `lifetimeState()` to
`NativeDestroyedPendingFinalize`, and keeps cached state and queued events available until controlled finalization.

## Displays

Fundamental display mode operations are under `Window::Display`:

- `getModes()`
- `getCurrentMode()`
- `getPreferredMode()`

Rich inspection from `window/display_info.h` adds:

- `getMonitors()`
- `getPrimaryMonitor()`
- `getMonitor()`
- `getColorInfo(Types::Display::MonitorId)`
- `getColorInfo(const Window&)`

OS HDR/WCG/color facts belong to Window display inspection, not to the renderer bridge.

## Text contract

Public Window text is UTF-8. Window uses the Unicode foundation library for strict UTF-8/UTF-16 conversion at native boundaries rather than
maintaining a second UTF-8 decoder. Native title operations additionally reject embedded U+0000 because the Win32 APIs consume NUL-terminated strings.

## Renderer bridge

`Window::Renderer` contains renderer-to-Window integration behavior only: occlusion-provider attachment/reporting and pointer hit-mask publication.
Passive bridge values live under `Window::Types::Renderer`.
