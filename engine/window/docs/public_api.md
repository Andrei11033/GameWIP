@page window_public_api Public API

## Library operations

Library-level operations provide four groups of behavior:

- `getCapabilities()` and `supports()` report backend features and region limits.
- `pollEvents()` and `waitEvents()` pump every Window owned by the calling thread.
- Monitor functions enumerate current snapshots and resolve process-local monitor IDs.
- Display-mode functions report physical monitor modes.

`pollEvents()` never blocks. `waitEvents()` accepts zero, a finite non-negative timeout, or `kWaitForever`. Pumping with no open Window on the calling thread is a successful no-op. Recursive pumping returns `ResourceBusy`.

## Window ownership

`Window` is default-constructible, non-copyable, and non-movable. Use stable `std::unique_ptr<Window>` storage when a container needs indirection. `open()` either allocates one internal queue or borrows a caller-provided non-empty `span<Event>` until close.

Placement monitor IDs are meaningful only for centered placement, and mode monitor IDs are meaningful only for fullscreen requests. Focus or non-normal presentation requires an initially visible Window; a focus request also requires `focusable`. Contradictory combinations are rejected instead of being silently ignored.

`id()`, `ownerId()`, `setOwner()`, and `isOwnedByCurrentThread()` expose process-local identity and same-thread native ownership. An invalid owner ID removes ownership. Unknown, self, cross-thread, and cyclic owner relationships are rejected.

## Cached state

Geometry, DPI, content scale, monitor, mode, presentation, decorations, controls, limits, cursor, pointer policy, opacity, visibility, focus, occlusion, and option getters return cached values. They do not synchronize or issue native queries.

The title view is invalidated by title replacement, close, or destruction. Closed-object getters return documented neutral values. Operations requiring native state report `NotOpen`. Queue consumers return false or zero. Repeated close succeeds.

## Mutation

Checked mutations cover title and icons; logical client size, physical screen placement, conversion, and DPI policy; visibility, focus, attention, and presentation; modes; controls; opacity and backdrop; custom chrome and pointer policy; and cursor behavior.

Windowed geometry and constraint setters return `ResourceBusy` in either fullscreen mode. `maximizable == true` requires `resizable == true`; invalid descriptions and runtime transitions fail instead of silently changing another property.

Input spans for icons and regions are call-scoped. Window copies the required data before returning. Region count is bounded by the advertised capabilities. Icon candidates are tightly packed RGBA8 and require exactly `width * height * 4` bytes after overflow-safe validation.

## Events

`Types::Events::Payload` is a typed variant, and `Event::getIf<T>()` provides non-throwing typed access. Events report close intent, unexpected native destruction, visibility, geometry, focus, presentation, content scale and DPI, monitor, mode, owner, display configuration, cursor presence, file drops, supported occlusion changes, and redraw requests.

Explicit `close()` is synchronous and emits no `ClosedEvent` because it intentionally destroys the Window and releases the queue. Observe user/system intent through sticky `closeRequested()` and `CloseRequestedEvent`.

Unexpected native destruction sets `lifetimeState()` to `NativeDestroyedPendingFinalize` and makes `isOpen()` false. Cached state and the event queue remain available, and a typed `ClosedEvent` is retained even when a full queue has no coalescible entry. Native mutations then report `NotOpen`; another `open()` reports `AlreadyOpen` until owner-thread `close()` completes controlled finalization. The object may reopen afterward.

## Renderer bridge

The optional `window/renderer_bridge.h` bridge accepts renderer occlusion feedback and packed pointer masks and exposes opt-in per-monitor display-color facts without making Renderer a Window dependency. Display color remains a dynamic query because support and active HDR/WCG state can differ by monitor and change while the process runs. See @ref window_renderer_integration.

## Status authority

Capability flags describe availability, but the status returned by the requested operation remains authoritative. Native errors retain a portable category plus useful native code and diagnostic text when available. Failed setters preserve the previous valid state where the backend can roll back atomically.

## Related pages

- @ref window_coordinates_and_dpi
- @ref window_lifecycle_and_events
- @ref window_package_abi
- @ref window_renderer_integration
