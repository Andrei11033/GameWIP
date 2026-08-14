@page window_library Window

`GameWIP::Window` is the standalone, portable owner of native top-level desktop windows. It provides checked lifecycle and mutation operations, fixed-capacity typed event queues, cached state, display discovery and inspection, and an explicit native interoperability boundary.

Window is usable without Input, Action, WindowManager, Renderer, UI, or the game executable. It creates no event thread and calls no user callback from a native window procedure.

## Consumer manual

- @subpage window_quick_start
- @subpage window_public_api
- @subpage window_package_abi
- @subpage window_coordinates_and_dpi
- @subpage window_lifecycle_events
- @subpage window_chrome_and_pointer_input
- @subpage window_fullscreen_monitors
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

Use @ref GameWIP::Window for library-wide capability operations and the non-copyable, non-movable @ref GameWIP::Window::Window owner. Passive values live under @ref GameWIP::Window::Types, with event payloads under `Types::Events`, display values under `Types::Display`, and renderer-bridge values under `Types::Renderer`. Global event pumping lives under `Window::Events`, display inspection under `Window::Display`, and renderer feedback under `Window::Renderer`. Win32 consumers use @ref GameWIP::Window::Native::Win32 deliberately.

## Key behavior

Every successful `open()` creates one process-local `Types::WindowId` and one fixed event queue. `Window` is non-copyable and non-movable, keeping its address and thread affinity stable. Cached getters are allocation-free and never query the operating system.

Native callbacks update cached state before inserting events, so queue overflow loses notification history without making current state stale. Close requests remain sticky even when their `Types::Events::CloseRequested` payload cannot be retained.

The opening thread owns native mutation, queue consumption, and event pumping. `wakeEventWait()` is the only intentionally cross-thread object operation. A thread-local dispatcher pumps every Window opened by that thread; no WindowManager is involved.

Destruction on another thread transfers complete state ownership to that dispatcher without allocation. Dispatcher or thread shutdown closes remaining native windows and restores exclusive-mode, cursor, class, and identity resources. Unexpected native destruction retains portable state and a typed `Types::Events::NativeDestroyed` payload until controlled owner-thread finalization. Explicit `close()` does not emit that payload.

Public text is valid UTF-8. Window uses the foundational Unicode library for strict native-boundary conversion instead of maintaining a duplicate decoder. Embedded U+0000 remains invalid for native Window titles because the Win32 title APIs are NUL-terminated. Client-local values use logical units; desktop placement and monitor rectangles use physical virtual-screen coordinates; framebuffer and display-mode extents use physical pixels. Screen coordinates may be negative. A per-Window DPI resize policy controls whether a monitor transition preserves logical client size or physical client pixels.

## Public header boundary

The normal portable surface is assembled by `window/window.h` from focused `window/types.h`, `window/description.h`, `window/events.h`, and `window/display.h`. Rich monitor/color inspection is opt-in through `window/display_info.h`. Renderer integration is opt-in through `window/renderer_bridge.h`, and Win32 interoperability is opt-in through `window/native/win32.h`.

Installed consumers link `GameWIP::Window`. Window is intentionally built as a shared library: process-local Window and monitor identities, native class ownership, dispatchers, and registries must remain coherent through one runtime instance rather than being duplicated across statically linked modules.

## Dependency boundary

Window owns top-level native state, cached geometry, event translation, queried native display/color facts, and the persistent packed pointer mask published through `window/renderer_bridge.h`. Input state, action mapping, renderer surfaces, swapchains, renderer HDR metadata, tone mapping, GPU readback, UI layout, application loops, and multi-window coordination remain outside this library.
