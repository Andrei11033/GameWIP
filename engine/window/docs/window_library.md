@page window_library Window

`GameWIP::Window` is the standalone, portable owner of native top-level desktop windows. It provides checked lifecycle and mutation operations,
fixed-capacity typed event queues, cached state, display discovery and inspection, and an explicit native interoperability boundary.

Window is usable without Input, Action, WindowManager, Renderer, UI, or the game executable. It creates no event thread and calls no user callback
from a native window procedure.

## How the library is organized

A `Window` owns one native top-level window, its cached portable state, and a
fixed-capacity event queue. The thread that opens it also mutates it, pumps its
events, and consumes that queue. Native callbacks first update cached state and
then publish typed events, allowing getters to remain current even if the queue
overflows. Display APIs describe monitors and modes independently of a Window;
opt-in headers expose renderer feedback and deliberate native interoperation.

## Consumer manual

- @subpage window_quick_start — Include, link, open, pump, inspect events, and
  close a Window.
- @subpage window_public_api — Find headers, namespaces, owners, passive types,
  capability groups, and results.
- @subpage window_package_abi — Understand why Window is shared and how its
  package, exports, manifest, and runtime identity work.
- @subpage window_coordinates_and_dpi — Relate logical client units, physical
  pixels, desktop coordinates, framebuffers, scale, and DPI policy.
- @subpage window_lifecycle_events — Understand thread ownership, dispatch,
  queue overflow, close requests, waits, and native destruction.
- @subpage window_chrome_and_pointer_input — Configure system/custom chrome,
  drag regions, caption controls, cursor modes, and pointer capture.
- @subpage window_fullscreen_monitors — Choose windowed, borderless, and
  exclusive modes and handle monitor or topology changes.
- @subpage window_native_interop — Access a native handle without taking
  ownership or bypassing portable lifetime rules.
- @subpage window_renderer_integration — Attach renderer feedback and consume
  the packed pointer snapshot.
- @subpage window_examples — See lifecycle, events, displays, fullscreen,
  custom chrome, and renderer integration in context.
- @subpage window_troubleshooting — Diagnose ownership, capabilities, queue
  pressure, display transitions, native destruction, and renderer feedback.
- @subpage window_future_extensions — Understand where proposed child surfaces,
  accessibility, clipboard, drag/drop, dialogs, and related features belong.

## Maintainer validation

- @subpage window_testing — See automated, package, ABI, and platform coverage.
- @subpage window_test_hooks — Understand source-tree-only fault and state
  inspection seams.
- @subpage window_manual_validation — Run and interpret the visual behaviors
  that automation cannot prove.

## Generated API reference

Use @ref GameWIP::Window for library-wide capability operations and the non-copyable, non-movable @ref GameWIP::Window::Window owner. Passive values
live under @ref GameWIP::Window::Types, with event payloads under `Types::Events`, display values under `Types::Display`, and renderer-bridge values
under `Types::Renderer`. Global event pumping lives under `Window::Events`, display inspection under `Window::Display`, and renderer feedback under
`Window::Renderer`. Win32 consumers use @ref GameWIP::Window::Native::Win32 deliberately.

## Key behavior

Every successful `open()` creates one process-local `Types::WindowId` and one fixed event queue. `Window` is non-copyable and non-movable, keeping its
address and thread affinity stable. Cached getters are allocation-free and never query the operating system.

Native callbacks update cached state before inserting events, so queue overflow loses notification history without making current state stale. Close
requests remain sticky even when their `Types::Events::CloseRequested` payload cannot be retained.

The opening thread owns native mutation, queue consumption, and event pumping. `wakeEventWait()` is the only intentionally cross-thread object
operation. A thread-local dispatcher pumps every Window opened by that thread; no WindowManager is involved.

Destruction on another thread transfers complete state ownership to that dispatcher without allocation. Dispatcher or thread shutdown closes remaining
native windows and restores exclusive-mode, cursor, class, and identity resources. Unexpected native destruction retains portable state and a typed
`Types::Events::NativeDestroyed` payload until controlled owner-thread finalization. Explicit `close()` does not emit that payload.

Public text is valid UTF-8. Window uses the foundational Unicode library for
strict native-boundary conversion instead of maintaining another decoder.
Embedded U+0000 is invalid in a native Window title because the Win32 title APIs
are NUL-terminated.

Client-local positions and sizes use logical units. Desktop placement and
monitor rectangles use physical virtual-screen coordinates, where positions may
be negative. Framebuffer and display-mode extents use physical pixels. Each
Window chooses whether a DPI transition preserves its logical client size or
its physical client pixels.

## Public header boundary

The normal portable surface is assembled by `window/window.h` from focused `window/types.h`, `window/description.h`, `window/events.h`, and
`window/display.h`. Rich monitor/color inspection is opt-in through `window/display_info.h`. Renderer integration is opt-in through
`window/renderer_bridge.h`, and Win32 interoperability is opt-in through `window/native/win32.h`.

Installed consumers link `GameWIP::Window`. Window is intentionally built as a shared library: process-local Window and monitor identities, native
class ownership, dispatchers, and registries must remain coherent through one runtime instance rather than being duplicated across statically linked
modules.

## Dependency boundary

Window is installed as the shared target `GameWIP::Window`. IO and FileSystem
are public package dependencies because Window headers expose their contracts;
Unicode is a private native-text conversion dependency.

Window owns top-level native state, cached geometry, event translation, queried
display/color facts, and the persistent packed pointer mask published through
`window/renderer_bridge.h`.

It does not own input state, action mapping, renderer surfaces or swapchains,
renderer HDR metadata, tone mapping, GPU readback, UI layout, application loops,
or coordination policy across several Windows.
