@page window_public_api Public API

## Library operations

`getCapabilities()` and `supports()` expose backend feature flags and region limits. `pollEvents()` and `waitEvents()` operate on every Window owned by the calling thread. Monitor functions enumerate snapshots and resolve process-local monitor IDs. Display-mode functions return physical mode information.

`pollEvents()` never blocks. `waitEvents()` accepts zero, a finite non-negative timeout, or `kWaitForever`. Pumping with no open Window on the calling thread is a successful no-op. Recursive pumping returns `ResourceBusy`.

## Window ownership

`Window` is default-constructible, move-constructible, non-copyable, and deliberately not move-assignable. Move construction transfers stable private state, event storage, and native callback routing; it does not transfer native thread affinity. `open()` may allocate its internal queue once or borrow a caller-provided non-empty `span<Event>` until close.

Placement monitor IDs are meaningful only for centered placement, and mode monitor IDs are meaningful only for fullscreen requests. Focus or non-normal presentation requires an initially visible Window; a focus request also requires `focusable`. Contradictory combinations are rejected instead of being silently ignored.

`id()`, `ownerId()`, `setOwner()`, and `isOwnedByCurrentThread()` expose process-local identity and same-thread native ownership. An invalid owner ID removes ownership. Unknown, self, cross-thread, and cyclic owner relationships are rejected.

## Cached state

Geometry, DPI, content scale, monitor, mode, presentation, decorations, controls, limits, cursor, pointer policy, opacity, visibility, focus, occlusion, and option getters return cached values. They do not synchronize or issue native queries. A title view is invalidated by title replacement, close, move, or destruction.

Closed-object getters return documented neutral values. Operations requiring native state report `NotOpen`. Queue consumers return false or zero. Repeated close succeeds.

## Mutation

Checked mutations cover title and icons; logical client geometry and conversions; visibility, focus, attention, and presentation; windowed/fullscreen modes; decoration, controls, resizability, focusability, interaction, topmost policy, opacity, backdrop, and file drops; custom chrome and pointer regions; and cursor mode, shape, position, confinement, and relative policy.

Input spans for icons and regions are call-scoped. Window copies the required data before returning. Region count is bounded by the advertised capabilities. Icon candidates are tightly packed RGBA8 and require exactly `width * height * 4` bytes after overflow-safe validation.

## Events

`EventData` is a typed variant. `Event::getIf<T>()` performs non-throwing typed access. Events represent close intent, visibility, geometry, focus, presentation, content scale/DPI, monitor, mode, owner, display configuration, cursor presence, file drops, occlusion where supported, and redraw requests.

There is no public `ClosedEvent`: successful close destroys the queue that would contain it, making such an event unobservable. Observe destruction through `close()` and `isOpen()`; observe user/system intent through sticky `closeRequested()` and `CloseRequestedEvent`.

## Renderer feedback

The optional `window/integration/renderer_feedback.h` adapter lets the renderer provide authoritative occlusion transitions without making Renderer a Window dependency. Global capability discovery reports no platform-native occlusion support. An individual Window advertises `OcclusionReporting` only while its owner thread has attached a provider. See @ref window_renderer_feedback for the lifecycle and queue contract.

## Status authority

Capability flags describe availability, but the status returned by the requested operation remains authoritative. Native errors retain a portable category plus useful native code and diagnostic text when available. Failed setters preserve the previous valid state where the backend can roll back atomically.
