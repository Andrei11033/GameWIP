@page window_child_surfaces Native child surfaces

`GameWIP::Window::ChildSurface` is an opt-in managed native child host inside one open top-level `Window`. It exists for browser views, native
controls, video hosts, plugin UI, and other external technology that requires its own native child-window region. It is not a GameWIP UI panel,
renderer surface, Input source, layout system, nested portable Window, or third-party SDK lifetime manager.

Include `window/child_surface.h` and link the existing `GameWIP::Window` target. The normal `window/window.h` header does not include this optional
surface.

## Ownership and lifetime

A default `ChildSurface` is closed. It is non-copyable and non-movable, so its address remains stable. `open(parent, description)` requires an open
top-level parent and runs on that parent's owner thread. A successful lifetime inherits that thread and caches the parent's `Types::WindowId`; `parentId()`
continues to report that identity during exceptional finalization even if the parent lifetime has ended.

Explicit `close()` synchronously destroys the host on the owner thread and does not report `NativeDestroyed`. A wrong-thread explicit close returns
`ResourceBusy`; wrong-thread destruction transfers complete cleanup to the existing owner-thread dispatcher, which retains ownership until it can
release the native resources.

Win32 naturally destroys child HWNDs when their parent is destroyed. If the parent closes first, or the host is unexpectedly destroyed,
`isOpen()` becomes false, native handle access returns `NotOpen`, and `lifetimeState()` becomes `NativeDestroyedPendingFinalize`. One
`Types::ChildSurface::Events::NativeDestroyed` notification remains observable under queue pressure. Call `close()` on the owner thread to release
the retained queue and backend bookkeeping before reopening.

GameWIP owns only the host. External SDKs may create native descendants under the borrowed host handle, but must not destroy or reparent the host,
replace or subclass its procedure, overwrite GameWIP-owned native user data, or otherwise take ownership. Shut down external technology before
closing its host whenever that SDK requires explicit teardown:

```cpp
shutdownExternalTechnology();
static_cast<void>(childSurface.close());
static_cast<void>(parentWindow.close());
```

Destroying the host can cause Win32 to destroy native descendants. GameWIP does not replace the SDK's own shutdown contract.

## Geometry and DPI

`Types::ChildSurface::Description::rect` is the authoritative `LogicalRect`, relative to the parent Window client origin. `{0, 0}` is the parent
client top-left. Negative positions, zero width or height, and rectangles extending beyond the parent are valid. Native parent clipping handles the
out-of-bounds portion; parent resize performs no layout.

`setRect()` changes position and size together; `setPosition()` and `setSize()` change one component. Parent movement carries the native child with
the parent without changing its parent-relative logical rectangle. `screenRect()` reports physical virtual-screen geometry. `clientToScreen()` and
`screenToClient()` convert between ChildSurface-local logical positions and physical virtual-screen positions.

`ChildSurface` has no DPI resize policy. Its logical rectangle is preserved at every DPI transition and physical geometry is recalculated from that
authoritative logical value. A logical `800x600` host is `800x600` pixels at 96 DPI and `1200x900` pixels at 144 DPI; prior physical pixels are never
rescaled cumulatively. Win32 synchronizes after the parent transition through `WM_DPICHANGED_AFTERPARENT`. Creation temporarily applies mixed DPI
hosting behavior only while the host HWND is created, then restores the thread's previous behavior so external descendants with different awareness
contexts can be hosted without permanently changing owner-thread policy.

## Events and pumping

Each successful open lifetime has its own fixed-capacity queue and sequence beginning at one. The default overload uses
`Window::Events::kDefaultQueueCapacity`; other overloads allocate a requested capacity or borrow caller-owned
`std::span<Types::ChildSurface::Event>` storage until close.

`PositionChanged`, `SizeChanged`, `PixelSizeChanged`, and `ContentScaleChanged` may coalesce across adjacent compatible observations.
`VisibilityChanged` remains non-coalescible. Full queues prefer evicting an older coalescible event, and preserve terminal `NativeDestroyed` even when
that requires evicting the oldest retained entry. `Types::Events::QueueInfo` and `StorageKind` are shared with top-level Window queues.

ChildSurface adds no `poll()` or `wait()`. Continue using `Window::Events::poll()` and `Window::Events::wait()`; their queued and dropped counts include
events routed to top-level Windows and ChildSurfaces during that pump. Every object still owns and consumes its own queue.

## Visibility, interaction, and sibling order

`show()` and `hide()` control the host's native visibility without requesting activation. `setUserInteractionEnabled()` uses native enablement for the
host and its normal descendant interaction. ChildSurface intentionally has no portable focus or focusability state because descendant technology
owns its own focus behavior.

`bringToFront()`, `sendToBack()`, `placeAbove()`, and `placeBelow()` change native sibling order only. Relative placement rejects the same object, a
closed sibling, different parent Window lifetimes, and different owner threads. No integer z-index or general layering system is implied.

## Native interoperability

Win32 consumers include `window/native/win32.h` and call `Native::Win32::getHandle(surface)`. The returned HWND is borrowed and valid only while native
use cannot race close. A genuine GameWIP child host uses `WS_CHILD`, `WS_CLIPCHILDREN`, and `WS_CLIPSIBLINGS`; external technology creates its own
descendants below that host.

## Related pages

- @ref window_lifecycle_events
- @ref window_coordinates_and_dpi
- @ref window_native_interop
- @ref window_manual_validation
