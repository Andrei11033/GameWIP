@page desktop_lifecycle_events Lifecycle and events

A Window lifetime begins with a checked `open()`, stays bound to its opening
thread, and ends with observable cleanup. Events report changes during that
lifetime without transferring ownership or invoking application callbacks.

## Open lifetime

A default-constructed `Window` is closed and inert. `open()` validates the complete request and event storage before committing a successful native
lifetime. The opening thread becomes the owner thread and a successful lifetime receives a process-local `Types::WindowId`.

Internal event storage is allocated once at open. The external-storage overload
borrows a non-empty caller span through lifetime finalization, including
`NativeDestroyedPendingFinalize`. If destruction transfers cleanup to another
thread's dispatcher, the caller's storage must also survive that deferred
cleanup. Close on the owner thread before releasing external storage.
Window does not create a worker thread.

## Thread ownership

Except `wakeEventWait()`, operations on an open Window require the owner thread by default. Wrong-thread explicit close returns `ResourceBusy`.
Cached getters are owner-thread-only and unsynchronized unless the renderer-facing presentation facility is explicitly enabled.

After owner-thread `Renderer::enableConcurrentPresentationReads(window)` succeeds, the following cached getters are also safe for concurrent reads:

- `clientSize()` and `framebufferSize()`
- `contentScale()` and `effectiveDpi()`
- `currentMonitor()`
- `presentationState()`, `minimized()`, and `maximized()`
- `visible()` and `interactiveMoveResizeActive()`
- `occluded()`

Enabling is one-way for the C++ object and persists across close/reopen. It allocates and publishes the current authoritative state once; default
Windows pay no allocation or publication cost. Call it before starting renderer reads. Enabling concurrently with reads is unsupported.

Each compound result is internally coherent. Separate calls may observe successive valid states. This contract does not make concurrent destruction
of the C++ `Window` object safe. The application must keep the object alive until the renderer thread has stopped or joined.

The optional accessibility façade has a separate threading contract. Complete
owner-thread `enable()` before sharing it; publication, snapshot inspection,
queue telemetry, and announcements may then run on other threads. Activation,
deactivation, action draining, and native notification pumping remain on the
owner thread. Join façade users before destroying the borrowed Window. See
@ref desktop_accessibility for publication and close-race behavior.

Wrong-thread destruction does not destroy owner-thread-affine resources directly. Private state is transferred to the owner dispatcher without
allocating, the dispatcher is woken, and cleanup is completed on the owner thread. Dispatcher teardown also drains deferred cleanup and finalizes
registered Windows.

Display-color queries retain a current-thread DXGI factory only while that thread owns an open Window. Standalone queries release the factory before
returning, and closing the final Window releases it during controlled Desktop execution. If an owner thread exits with a Window still registered,
dispatcher teardown drops the remaining factory reference without entering DXGI from thread-detach cleanup; the operating system reclaims that
exceptional-path reference at process termination.

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
2. owner-thread cached state and the event queue remain retained, while enabled renderer-facing publication resets to closed defaults;
3. `lifetimeState()` becomes `NativeDestroyedPendingFinalize` and `isOpen()` becomes false;
4. `Types::Events::NativeDestroyed` is queued and protected from silent loss when a full queue can evict an older coalescible entry;
5. native mutations return `NotOpen` until owner-thread `close()` completes retained finalization.

The same `Window` object may be opened again after finalization.

## Queue behavior

The queue is fixed-capacity. `ClientPositionChanged`, `ClientSizeChanged`,
`FramebufferSizeChanged`, and `ContentScaleChanged` may replace an earlier event
of the same type within the trailing run of geometry/DPI events. A durable
event ends that run. Replacement keeps the earlier slot and sequence number,
so a sequence identifies queue insertion rather than the age of every field.
When full, the queue evicts the oldest coalescible entry; if there is none, it
drops the incoming event. `NativeDestroyed` instead evicts the oldest entry
when necessary so native loss remains observable.

`Types::Events::QueueInfo` reports storage kind, capacity, pending count, and cumulative drops. `clearDroppedEventCount()` clears only the drop
counter.

## Pumping

`Desktop::Events::poll()` pumps the calling thread without blocking. `Desktop::Events::wait()` waits for input up to the requested timeout and then
pumps. Their queued and dropped counts include events routed to top-level Windows and optional ChildSurfaces during the call. Pumping with no open
Window, ChildSurface, ProgressDialog, tray icon, or notification center on the
calling thread is a successful no-op; recursive pumping returns `ResourceBusy`.
Shell interactions use their separate `ShellEventQueue` and are not included
in the Window-subsystem queued/dropped counts.

`wakeEventWait()` may run on another thread to interrupt the owner's wait while
the native lifetime remains open. Synchronize wake callers with open, close,
native destruction, and C++ object destruction. A wake carries no application
payload; publish work through application-owned synchronization before waking.

This snippet keeps the lifetime stable until the notifying thread has joined:

```cpp
#include <atomic>
#include <thread>

std::atomic_bool workReady = false;
std::thread notifier([&]
{
    workReady.store(true, std::memory_order_release);
    static_cast<void>(window.wakeEventWait());
});
const auto pump = GameWIP::Desktop::Events::wait(std::chrono::milliseconds{100});
notifier.join();
if (pump.status.ok() && workReady.load(std::memory_order_acquire))
{
    // Consume application-owned work before returning to the event loop.
}
// The notifying thread no longer borrows window; close may now proceed.
```

## Interactive move and resize

The native interactive move/resize lifecycle is exposed through the cached `interactiveMoveResizeActive()` state and the non-coalescible
`Types::Events::InteractiveMoveResizeStarted` and `Types::Events::InteractiveMoveResizeEnded` transition payloads. It is owner-thread-only by default
and becomes safe for renderer-thread reads after concurrent presentation reads are enabled. Only entry into and exit from the operating system's
interactive loop changes this state; ordinary programmatic geometry and presentation changes do not synthesize the lifecycle.

Desktop continues to process and report native geometry, DPI, and monitor changes during the interactive loop. Frame scheduling, simulation policy,
and renderer resource resizing remain application and renderer responsibilities.
