@page desktop_library Desktop

`GameWIP::Desktop` provides standalone portable ownership of native top-level desktop windows, optional managed child hosts, and synchronous desktop
Clipboard data exchange. Its API provides checked lifecycle and mutation operations, fixed-capacity typed event queues, cached state, display
discovery and inspection, and an explicit native interoperability boundary.

Desktop is usable without Input, Action, WindowManager, Renderer, UI, or the game executable. It creates no event thread and invokes no user callbacks
from a native window procedure.

## How the library is organized

A `Window` owns one native top-level window, its cached portable state, and a
fixed-capacity event queue. The thread that opens it also mutates it, pumps its
events, and consumes that queue. Native callbacks first update cached state and
then publish typed events, allowing getters to remain current even if the queue
overflows. Display APIs describe monitors and modes independently of a Window;
opt-in headers expose renderer integration and deliberate native interoperation.

## Consumer manual

- @subpage desktop_quick_start — Include, link, open, pump, inspect events, and
  close a Window.
- @subpage desktop_public_api — Find headers, namespaces, owners, passive types,
  capability groups, and results.
- @subpage desktop_package_abi — Understand why Window is shared and how its
  package, exports, manifest, and runtime identity work.
- @subpage desktop_coordinates_and_dpi — Relate logical client units, physical
  pixels, desktop coordinates, framebuffers, scale, and DPI policy.
- @subpage desktop_custom_cursors — Create shared native cursor images, supply
  DPI variants, select them on Windows, and restore system shapes.
- @subpage desktop_child_surfaces — Host externally managed native descendants
  inside an optional managed child HWND.
- @subpage desktop_clipboard — Exchange UTF-8 text, paths, RGBA8 images, and
  arbitrary named opaque data without opening a Window.
- @subpage desktop_lifecycle_events — Understand thread ownership, dispatch,
  queue overflow, close requests, waits, and native destruction.
- @subpage desktop_chrome_and_pointer_input — Configure system and custom chrome,
  drag regions, caption controls, cursor modes, and pointer routing.
- @subpage desktop_fullscreen_monitors — Choose windowed, borderless, and
  exclusive modes and handle monitor or topology changes.
- @subpage desktop_native_interop — Access a native handle without taking
  ownership or bypassing portable lifetime rules.
- @subpage desktop_renderer_integration — Enable concurrent presentation reads,
  attach renderer feedback, and publish packed pointer data.
- @subpage desktop_examples — See lifecycle, events, displays, fullscreen,
  custom chrome, and renderer integration in context.
- @subpage desktop_troubleshooting — Diagnose ownership, capabilities, queue
  pressure, display transitions, native destruction, and renderer integration.
- @subpage desktop_future_extensions — Understand where proposed accessibility,
  drag/drop, dialogs, and related features belong.

## Maintainer validation

- @subpage desktop_testing — See automated, package, ABI, and platform coverage.
- @subpage desktop_test_hooks — Understand source-tree-only fault and state
  inspection seams.
- @subpage desktop_manual_validation — Run and interpret the visual behaviors
  that automation cannot prove.

## Generated API reference

Use @ref GameWIP::Desktop for library-wide capability operations and the non-copyable, non-movable @ref GameWIP::Desktop::Window owner. Passive values
live under @ref GameWIP::Desktop::Types, with child-host values under `Types::ChildSurface`, transfer values under `Types::DataTransfer`, Clipboard
results under `Types::Clipboard`, event payloads under `Types::Events`, display values under `Types::Display`, and renderer-bridge values under
`Types::Renderer`. Global event pumping lives under `Desktop::Events`, Clipboard operations under `Desktop::Clipboard`, display inspection under
`Desktop::Display`, and renderer integration under `Desktop::Renderer`. Win32 consumers use @ref GameWIP::Desktop::Native::Win32 deliberately.

## Key behavior

Every successful `open()` creates one process-local `Types::WindowId` and one fixed event queue. `Window` is non-copyable and non-movable, keeping its
address and thread affinity stable. Cached getters are allocation-free and never query the operating system. By default they remain owner-thread-only.
The optional renderer bridge can lazily enable atomic publication for the documented presentation subset.

Native callbacks update cached state before inserting events, so queue overflow loses notification history without making current state stale. Close
requests remain sticky even when their `Types::Events::CloseRequested` payload cannot be retained.

The opening thread owns native mutation, queue consumption, and event pumping. `wakeEventWait()` is always cross-thread-safe. Renderer-facing
presentation reads become a narrow additional exception only after explicit opt-in. A thread-local dispatcher pumps each owner thread's Windows.

Cross-thread presentation reads do not make concurrent destruction safe. Applications must ensure the `Window` object outlives every renderer read.

Destruction on another thread transfers complete state ownership to that dispatcher without allocation. Dispatcher or thread shutdown closes remaining
native windows and restores exclusive-mode, cursor, class, and identity resources. Unexpected native destruction retains portable state and a typed
`Types::Events::NativeDestroyed` payload until controlled owner-thread finalization. Explicit `close()` does not emit that payload.

Public text is valid UTF-8. Desktop uses the foundational Unicode library for
strict native-boundary conversion instead of maintaining another decoder.
Embedded U+0000 is invalid in a native Window title because the Win32 title APIs
are NUL-terminated.

Client-local positions and sizes use logical units. Desktop placement and
monitor rectangles use physical virtual-screen coordinates, where positions may
be negative. Framebuffer and display-mode extents use physical pixels. Each
Window chooses whether a DPI transition preserves its logical client size or
its physical client pixels.

## Public header boundary

The normal portable surface is assembled by `desktop/window.h` from focused `desktop/types.h`, `desktop/description.h`, `desktop/events.h`, and
`desktop/display.h`. Rich monitor/color inspection is opt-in through `desktop/display_info.h`. Renderer integration is opt-in through
`desktop/renderer_bridge.h`, custom native cursors are opt-in through `desktop/cursor.h`, native child hosts are opt-in through
`desktop/child_surface.h`, shared transfer values and Clipboard are opt-in through `desktop/data_transfer.h` and `desktop/clipboard.h`, and Win32
interoperability is opt-in through `desktop/native/win32.h`.

Installed consumers link `GameWIP::Desktop`. Window is intentionally built as a shared library: process-local Window and monitor identities, native
class ownership, dispatchers, and registries must remain coherent through one runtime instance rather than being duplicated across statically linked
modules.

## Dependency boundary

Window is installed as the shared target `GameWIP::Desktop`. IO and FileSystem
are public package dependencies because Window headers expose their contracts;
Unicode is a private native-text conversion dependency.

Window owns top-level native state, cached geometry, event translation, queried
display/color facts, and the persistent packed pointer mask published through
`desktop/renderer_bridge.h`.

It does not own input state, action mapping, renderer surfaces or swapchains,
renderer HDR metadata, tone mapping, GPU readback, UI layout, application loops,
or coordination policy across several Windows.
