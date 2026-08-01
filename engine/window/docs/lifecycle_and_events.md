@page window_lifecycle_and_events Lifecycle, threads, and events

## Open and close

An open attempt validates the complete description and queue contract before native creation. Internal storage is allocated once. Native creation is hidden until all requested initial state has been applied. Partial failure releases the class reference, dispatcher registration, IDs, native handles, cursor confinement, icons, and exclusive display state, then leaves the Window closed.

`requestClose()` records intent; it never destroys the native resource. The first request becomes sticky and attempts one typed event. `clearCloseRequest()` permits an application to decline that request. `close()` performs destruction, cursor release, exclusive-mode restoration, dispatcher removal, and queue release.

## Stable ownership and thread affinity

The thread that successfully opens a Window owns it. The object cannot be copied or moved. Its owner thread performs mutations, consumes or clears the event queue, calls `close()`, accesses native handles, publishes renderer feedback, and pumps native messages. Checked operations called from another thread return `ResourceBusy`; queue operations return their documented neutral value. Cached getters are unsynchronized, so the application must coordinate cross-thread reads.

`wakeEventWait()` may be called from another thread. It posts a wake message to the owning dispatcher; it does not pump messages or change Window state. Window creates no worker thread.

Renderer feedback follows the same affinity. Renderer-owned synchronization may carry a presentation result or completed GPU mask readback between threads, but publication into Window occurs on its owner thread. Window does not own a renderer mailbox or perform GPU work.

An explicit wrong-thread `close()` returns `ResourceBusy` without changing state. If destruction occurs on another thread, the destructor atomically transfers complete private-state ownership to the registered owner-thread dispatcher and wakes it. The transfer does not allocate. The next pump destroys the native window, restores exclusive display state, releases native and process-local resources, clears retained external event payloads, and deletes the state on the owner thread.

The thread-local dispatcher also drains deferred ownership and closes every still-registered Window during owner-thread exit. The dispatcher registry stays synchronized through shutdown, so a concurrent late destructor either completes its transfer first or observes already-finalized native state afterward. Platform state is reset after cleanup, preventing duplicate destruction. At process shutdown the same thread-local teardown path runs before process resources are reclaimed.

Unexpected `WM_NCDESTROY` restores exclusive state, clears live visibility/focus/cursor state, sets `LifetimeState::NativeDestroyedPendingFinalize`, and queues `ClosedEvent`. The public object and queue remain available until owner-thread `close()` finalizes IDs, icons, class and dispatcher registrations, and event storage. Explicit close marks destruction in advance and therefore never emits `ClosedEvent`.

## Fixed-capacity storage

The internal `open(description, capacity)` overload allocates exactly `capacity` event slots once. The external overload borrows a non-empty `span<Event>` exclusively until close; its backing array must remain alive and unmoved.

Queue insertion performs no heap allocation for fixed-size event payloads. A file-drop payload may allocate its path vector because native input is variable-sized.

## Coalescing and overflow

Movement, logical client-size, framebuffer-size, and content-scale/DPI payloads are coalescible. A new compatible payload replaces the latest matching payload only within the coalescible suffix after the most recent noncoalescible event. This preserves semantic barriers such as focus or mode changes.

When full, the queue evicts its oldest coalescible event before retaining a new event. If no coalescible entry exists, a new event other than `ClosedEvent` is dropped. `ClosedEvent` instead evicts the oldest entry so unexpected destruction remains observable. `droppedEvents` counts every eviction or drop. Sequence values increase monotonically for newly retained events; a coalesced replacement keeps the existing slot sequence.

Cached state is updated before routing. Queue overflow therefore loses notification history, not current state. This also applies to renderer-provided occlusion transitions. Close intent remains sticky independently of queue capacity.

Repeated occlusion reports with the current value add no event. Detaching an active provider resets a true cached value to false and attempts a final transition event. Closing discards the provider with the rest of the Window state.

## Pump result

`EventPumpResult::eventsQueued` counts retained insertions and coalesces observed during the call. `eventsDropped` reports new drops during that pump. A finite wait that receives no input sets `timedOut`. A pump failure does not invalidate events already routed earlier in the same call.

## Related pages

- @ref window_quick_start
- @ref window_renderer_integration
- @ref window_testing
