@page window_library Window

`GameWIP::Window` is the standalone, portable owner of native top-level desktop windows. It provides checked lifecycle and mutation operations, fixed-capacity typed event queues, cached state, monitor and display-mode discovery, and an explicit native interoperability boundary.

Window is usable without Input, Action, WindowManager, Renderer, UI, or the game executable. It creates no event thread and calls no user callback from a native window procedure.

## Consumer manual

- @subpage window_quick_start
- @subpage window_public_api
- @subpage window_package_abi
- @subpage window_coordinates_and_dpi
- @subpage window_lifecycle_and_events
- @subpage window_chrome_and_pointer_input
- @subpage window_fullscreen_and_monitors
- @subpage window_native_interop
- @subpage window_renderer_integration
- @subpage window_examples
- @subpage window_troubleshooting
- @subpage window_future_extensions

## Maintainer validation

- @subpage window_testing
- @subpage window_test_hooks
- @subpage window_manual_validation

## Generated API reference

Use @ref GameWIP::Window for library operations and the non-copyable, non-movable @ref GameWIP::Window::Window owner. Use @ref GameWIP::Window::Types for passive values. Win32 consumers use @ref GameWIP::Window::Native::Win32 deliberately; renderer integrations use @ref GameWIP::Window::Renderer.

## Key behavior

Every successful `open()` creates one process-local `WindowId` and one fixed event queue. `Window` is non-copyable and non-movable, keeping its address and thread affinity stable. Cached getters are allocation-free and never query the operating system.

Native callbacks update cached state before inserting events, so queue overflow loses notification history without making current state stale. Close requests remain sticky even when their event cannot be retained.

The opening thread owns native mutation, queue consumption, and event pumping. `wakeEventWait()` is the only intentionally cross-thread object operation. A thread-local dispatcher pumps every Window opened by that thread; no WindowManager is involved.

Destruction on another thread transfers complete state ownership to that dispatcher without allocation. Dispatcher or thread shutdown closes remaining native windows and restores exclusive-mode, cursor, class, and identity resources. Unexpected native destruction retains portable state and a typed `ClosedEvent` until controlled owner-thread finalization.

Public text is UTF-8 and rejects invalid sequences and embedded NUL. Client-local values use logical units; desktop placement and monitor rectangles use physical virtual-screen coordinates; framebuffer and display-mode extents use physical pixels. Screen coordinates may be negative. A per-Window DPI resize policy controls whether a monitor transition preserves logical client size or physical client pixels.

## Dependency boundary

The portable installed header is `window/window.h`; it does not include native platform headers. Installed consumers link `GameWIP::Window`. The Win32-only `window/native/win32.h` adapter is opt-in. The portable `window/renderer_bridge.h` bridge is also opt-in and adds no Renderer dependency.

Window owns top-level native state, cached geometry, event translation, queried native display-color facts, and the persistent packed pointer mask published through `window/renderer_bridge.h`. Input state, action mapping, renderer surfaces, swapchains, HDR metadata, tone mapping, GPU readback, UI layout, application loops, and multi-window coordination remain outside this library.
