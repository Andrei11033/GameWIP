@page window Window

`GameWIP::Window` is the standalone, portable owner of native top-level desktop windows. It provides checked lifecycle and mutation operations, fixed-capacity typed event queues, cached state, monitor and display-mode discovery, and an explicit native interoperability boundary.

Window is usable without Input, Action, WindowManager, Renderer, UI, or the game executable. It creates no event thread and calls no user callback from a native window procedure.

## Consumer manual

- @subpage window_quick_start
- @subpage window_public_api
- @subpage window_lifecycle_and_events
- @subpage window_chrome_and_pointer_input
- @subpage window_fullscreen_and_monitors
- @subpage window_native_interop
- @subpage window_renderer_feedback
- @subpage window_examples
- @subpage window_troubleshooting
- @subpage window_future_extensions

## Maintainer validation

- @subpage window_testing
- @subpage window_test_hooks
- @subpage window_manual_validation

## Generated API reference

Use @ref GameWIP::Window for library operations and the move-only @ref GameWIP::Window::Window owner. Use @ref GameWIP::Window::Types for passive descriptions, geometry, monitor snapshots, capabilities, events, and result values. Win32 consumers that deliberately need native handles use @ref GameWIP::Window::Native::Win32. Renderer integrations report authoritative occlusion transitions through the `GameWIP::Window::Integration::Renderer` adapter.

## Key behavior

Every successful `open()` creates one process-local `WindowId` and one fixed event queue. Cached getters are allocation-free and never query the operating system. Native callbacks update cached state before attempting event insertion, so an overflow never makes state stale. Close requests are sticky even if their queue event cannot be retained.

The opening thread owns native mutation, queue consumption, and event pumping. `wakeEventWait()` is the only intentionally cross-thread object operation. A thread-local dispatcher pumps every Window opened by that thread; no WindowManager is involved.

Public text is UTF-8 and rejects invalid sequences and embedded NUL. Geometry and custom-region values use logical client units. Framebuffer sizes and display-mode resolutions use physical pixels.

## Dependency boundary

The portable installed header is `window/window.h`; it does not include native platform headers or expose native handle types. Installed consumers link `GameWIP::Window`. Window publicly depends only on IO and FileSystem. The explicit `window/native/win32.h` adapter includes `windows.h` and is opt-in. The portable `window/integration/renderer_feedback.h` adapter is also opt-in and adds no Renderer dependency.

Window owns top-level native window state and event translation. Input state, action mapping, renderer surfaces, swapchains, UI layout, application loops, and multi-window coordination remain outside this library. The renderer-feedback adapter accepts only the occlusion truth that Window caches and publishes.
