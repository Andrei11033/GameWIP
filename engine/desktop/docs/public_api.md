@page desktop_public_api Public API

Window splits its installed headers by concept while keeping one library and
one passive `Types` tree. Use this map to find the declaration that owns a
lifecycle, event, display, renderer, or native-interop operation.

## Header ownership

Window exposes one public `GameWIP::Desktop::Types` tree and focused headers by conceptual ownership:

- `desktop/types.h` contains shared primitive/value vocabulary such as `WindowId`, geometry, DPI, limits, and `PresentationState`.
- `desktop/description.h` contains creation/configuration policy and `Types::Description`.
- `desktop/events.h` contains `Types::Events`, queued `Types::Event`, and calling-thread `Window::Events` pump operations.
- `desktop/display.h` contains fundamental `Types::Display::MonitorId`, display `Mode`, and mode queries.
- `desktop/display_info.h` is the opt-in rich monitor/color inspection surface.
- `desktop/cursor.h` is the opt-in custom native cursor resource and selection surface.
- `desktop/child_surface.h` is the opt-in managed native child-host resource, passive description, and typed event surface.
- `desktop/data_transfer.h` is the opt-in shared non-owning/owning transfer vocabulary for Clipboard and future drag and drop.
- `desktop/clipboard.h` is the opt-in stateless synchronous Clipboard service and its operation results.
- `desktop/window.h` assembles the normal Window object API and includes the fundamental headers above, but not rich display inspection, custom cursor
  resources, Clipboard/data transfer, renderer integration, or native interop.
- `desktop/renderer_bridge.h` owns concurrent presentation-read opt-in, renderer feedback, and packed pointer publication.
- `desktop/native/win32.h` is explicit Win32 interoperability.

Passive data stays under `Types`; stateless domain operations live in the matching service namespace.

## Library and Window capabilities

`getCapabilities()` and `supports()` report backend/environment capability. `Window::supports()` has the same capability semantics; it does not report
whether a custom cursor is selected or a renderer provider is currently attached. Custom cursor selection state is queried with
`hasCustomCursor()`, and renderer attachment state with `Renderer::hasOcclusionProvider()`.

## Window ownership and state

`Window` is default-constructible, non-copyable, and non-movable. `open()` establishes one owner thread and one process-local `Types::WindowId`.
`WindowId::isValid()` reports whether an ID is nonzero. Mutations, queue operations, and most cached getters require the owner thread.

Cached getters do not issue native queries. Expected failures are returned as `IO::Types::Status` or typed result structs. Explicit `close()` is
synchronous and observable through its return status.

By default, all cached getters are owner-thread-only. After `Renderer::enableConcurrentPresentationReads()` succeeds, `clientSize()`,
`framebufferSize()`, `contentScale()`, `effectiveDpi()`, `currentMonitor()`, `presentationState()`, `minimized()`, `maximized()`, `visible()`,
`interactiveMoveResizeActive()`, and `occluded()` may also be read concurrently while the owner updates the Window. Each compound return value is
coherent, but separate getter calls may observe successive states.

The opt-in allocation is lazy, stable, one-way for the C++ object, and reused across close/reopen. It must complete before renderer reads begin.
Concurrent object destruction remains unsafe; the caller must keep the `Window` alive through all renderer reads.

Plain cached properties use direct names such as `visible()`, `focused()`,
`interactiveMoveResizeActive()`, `resizable()`, `userInteractionEnabled()`,
and `ownedByCurrentThread()`. `isOpen()` retains its prefix to distinguish the lifetime query from the checked
`open()` operation; `isValid()` remains a
classification query on identity and resource values. Presence, capability, and support queries retain meaningful forms such as `hasCloseRequest()`,
`hasCustomCursor()`, and `supports()`.

`hasCloseRequest()` reports sticky close intent. `requestClose()` queues one `Types::Events::CloseRequested` transition and `clearCloseRequest()`
clears the sticky flag.

## Configuration vocabulary

`Types::Mode` is the top-level Window mode. `Types::Controls` describes standard close/minimize/maximize availability.
`Types::Description::fileDropEnabled` configures initial file-drop delivery; runtime state is queried by `Window::fileDropEnabled()` and changed by
`setFileDropEnabled()`.

Configuration/request types live with `description.h`; shared primitive values remain in `types.h`; live Window state and call-scoped layouts remain
in `window.h`.

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
arbitrary named opaque bytes. `desktop/clipboard.h` owns `Types::Clipboard` results and the stateless `Window::Clipboard` operations. It adds no
`Window` capability because operations require no Window object or event pump. See @ref desktop_clipboard for timeout, transaction, native ownership,
and custom-format semantics.

## Events

Event payloads live under `Types::Events` and do not repeat the `Event` suffix. Examples are `CloseRequested`, `ClientPositionChanged`,
`InteractiveMoveResizeStarted`, `InteractiveMoveResizeEnded`, `FilesDropped`, and `NativeDestroyed`.

`Types::Events::Payload` is the payload variant. `Types::Event` remains the queued envelope and carries a monotonic sequence plus typed `getIf<T>()`
access. Queue metadata lives in `Types::Events::StorageKind` and `QueueInfo`; pump results use `Types::Events::PumpResult`.

Calling-thread pumping is grouped under `Window::Events`:

- `Events::poll()` is non-blocking.
- `Events::wait(timeout)` accepts zero, a finite non-negative timeout, or `Events::kWaitForever`.
- `Events::kDefaultQueueCapacity` is used by the default `Window::open()` overload.

Explicit `close()` emits no destruction event. Unexpected native destruction queues `Types::Events::NativeDestroyed`, changes `lifetimeState()` to
`NativeDestroyedPendingFinalize`, and keeps owner-thread cached state and queued events available until controlled finalization. When concurrent
presentation reads are enabled, their atomic publication resets to closed defaults when the native resource is destroyed.

## Displays

Fundamental display mode operations are under `Window::Display`:

- `getModes()`
- `getCurrentMode()`
- `getPreferredMode()`

Rich inspection from `desktop/display_info.h` adds:

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
`enableConcurrentPresentationReads()` opts one Window object into the narrow atomic getter contract, and
`concurrentPresentationReadsEnabled()` reports that one-way state. Passive bridge values live under `Window::Types::Renderer`.
