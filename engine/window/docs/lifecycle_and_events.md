@page window_lifecycle_and_events Lifecycle, threads, and events

## Open and close

An open attempt validates the complete description and queue contract before native creation. Internal storage is allocated once. Native creation is hidden until all requested initial state has been applied. Partial failure releases the class reference, dispatcher registration, IDs, native handles, cursor confinement, icons, and exclusive display state, then leaves the Window closed.

`requestClose()` records intent; it never destroys the native resource. The first request becomes sticky and attempts one typed event. `clearCloseRequest()` permits an application to decline that request. `close()` performs destruction, cursor release, exclusive-mode restoration, dispatcher removal, and queue release.

## Thread ownership

The thread that successfully opens a Window owns it. That thread must mutate it, read or clear its queue, call `close()`, and pump its native messages. Calling those operations elsewhere returns `ResourceBusy` or a neutral queue result. Cached getters are unsynchronized and must be coordinated by the application.

`wakeEventWait()` may be called from another thread. It posts a wake message to the owning dispatcher; it does not pump messages or change Window state. Window creates no worker thread.

Renderer feedback follows the same affinity. Renderer-owned synchronization may carry a presentation result between threads, but attachment, `reportOcclusion()`, and detachment occur on the Window owner thread. Window does not own a cross-thread renderer mailbox.

## Fixed-capacity storage

The internal `open(description, capacity)` overload allocates exactly `capacity` event slots once. The external overload borrows a non-empty `span<Event>` exclusively until close; its backing array must remain alive and unmoved. Move construction transfers that binding.

Queue insertion performs no heap allocation for fixed-size event payloads. A file-drop payload may allocate its path vector because native input is variable-sized.

## Coalescing and overflow

Movement, logical client-size, framebuffer-size, and content-scale/DPI payloads are coalescible. A new compatible payload replaces the latest matching payload only within the coalescible suffix after the most recent noncoalescible event. This preserves semantic barriers such as focus or mode changes.

When full, the queue evicts its oldest coalescible event before retaining a new event. If no coalescible entry exists, the new event is dropped. `droppedEvents` counts both cases. Sequence values are monotonically increasing for newly retained events; a coalesced replacement keeps the existing slot sequence.

Cached state is updated before routing. Queue overflow therefore loses notification history, not current truth. This also applies to renderer-provided occlusion transitions. Close intent is sticky independently from queue capacity.

Repeated occlusion reports with the current value add no event. Detaching an active provider resets a true cached value to false and attempts a final transition event. Closing discards the provider with the rest of the Window state.

## Pump result

`EventPumpResult::eventsQueued` counts retained insertions and coalesces observed during the call. `eventsDropped` reports new drops during that pump. A finite wait that receives no input sets `timedOut`. A pump failure does not invalidate events already routed earlier in the same call.
