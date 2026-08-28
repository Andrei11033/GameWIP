@page window_lifecycle_events Lifecycle and events

A Window lifetime begins with a checked `open()`, stays bound to its opening
thread, and ends with observable cleanup. Events report changes during that
lifetime without transferring ownership or invoking application callbacks.

## Open lifetime

A default-constructed `Window` is closed and inert. `open()` validates the complete request and event storage before committing a successful native
lifetime. The opening thread becomes the owner thread and a successful lifetime receives a process-local `Types::WindowId`.

Internal event storage is allocated once at open. The external-storage overload borrows a non-empty caller span until close. Window does not create a
worker thread.

## Thread ownership

Except `wakeEventWait()`, operations on an open Window require the owner thread. Cached getters are intentionally unsynchronized. Wrong-thread
explicit close returns `ResourceBusy`.

Wrong-thread destruction does not destroy owner-thread-affine resources directly. Private state is transferred to the owner dispatcher without
allocating, the dispatcher is woken, and cleanup is completed on the owner thread. Dispatcher teardown also drains deferred cleanup and finalizes
registered Windows.

## Close intent

`requestClose()` represents intent, not destruction. It sets the sticky `hasCloseRequest()` state and queues one `Types::Events::CloseRequested`
payload. Repeated requests do not queue duplicate close-intent transitions until `clearCloseRequest()` clears the sticky state.

Native user/system close requests are translated into the same close-intent contract. The application remains responsible for deciding when to call
`close()`.

## Controlled and unexpected destruction

Explicit `close()` performs controlled synchronous finalization and emits no `NativeDestroyed` payload because the caller initiated and observed the
destruction directly.

Unexpected native destruction follows a distinct exceptional lifetime:

1. the native handle disappears;
2. cached state and the event queue remain retained;
3. `lifetimeState()` becomes `NativeDestroyedPendingFinalize` and `isOpen()` becomes false;
4. `Types::Events::NativeDestroyed` is queued and protected from silent loss when a full queue can evict an older coalescible entry;
5. native mutations return `NotOpen` until owner-thread `close()` completes retained finalization.

The same `Window` object may be opened again after finalization.

## Queue behavior

The queue is fixed-capacity. Geometry/DPI events that supersede older adjacent observations may coalesce. When a full queue needs space for a new
event, an older coalescible entry is preferred for eviction; otherwise the incoming event is dropped, except the exceptional `NativeDestroyed`
transition is kept observable.

`Types::Events::QueueInfo` reports storage kind, capacity, pending count, and cumulative drops. `clearDroppedEventCount()` clears only the drop
counter.

## Pumping

`Window::Events::poll()` pumps the calling thread without blocking. `Window::Events::wait()` waits for input up to the requested timeout and then
pumps. Their queued and dropped counts include events routed to top-level Windows and optional ChildSurfaces during the call. Pumping with no open
Window-subsystem object on the calling thread is a successful no-op; recursive pumping returns `ResourceBusy`.

`wakeEventWait()` is the intentionally cross-thread-safe escape hatch for interrupting an owner thread blocked in `Events::wait()`.
