@page desktop_library Desktop

`GameWIP::Desktop` owns native top-level windows and provides optional child
hosts, Clipboard exchange, drag and drop, native dialogs, accessibility
snapshots, and shell integration. Its API provides checked lifecycle
and mutation operations, fixed-capacity typed event queues, cached state,
display discovery and inspection, and an explicit native interoperability
boundary.

Desktop is usable without Input, Action, WindowManager, Renderer, UI, or the game executable. It creates no event thread and invokes no user callbacks
from a native window procedure. Worker or renderer code that needs an
owner-thread mutation must use an application-owned transport queue and have
the owner thread perform the Desktop call; Desktop provides no
hidden worker or generic `post()` facility.

## How the library is organized

A `Window` owns one native top-level window, its cached portable state, and a
fixed-capacity event queue. The thread that opens it also mutates it, pumps its
events, and consumes that queue. Native callbacks first update cached state and
then publish typed events, allowing getters to remain current even if the queue
overflows. Display APIs describe monitors and modes independently of a Window;
focused headers expose renderer integration and native interoperation.

## Consumer manual

- @subpage desktop_quick_start - Include, link, open, pump, inspect events, and
  close a Window.
- @subpage desktop_public_api - Find headers, namespaces, owners, passive types,
  capability groups, and results.
- @subpage desktop_package_abi - Understand why Desktop is shared and how its
  package, exports, manifest, and runtime identity work.
- @subpage desktop_coordinates_and_dpi - Relate logical client units, physical
  pixels, desktop coordinates, framebuffers, scale, and DPI policy.
- @subpage desktop_custom_cursors - Create shared native cursor images, supply
  DPI variants, select them on Windows, and restore system shapes.
- @subpage desktop_child_surfaces - Host externally managed native descendants
  inside an optional managed child HWND.
- @subpage desktop_clipboard - Exchange UTF-8 text, paths, RGBA8 images, and
  arbitrary named opaque data without opening a Window.
- @subpage desktop_drag_drop - Exchange portable data through native drag
  sources and declarative target regions.
- @subpage desktop_dialogs - Present synchronous native choices and modeless
  owner-thread operation progress.
- @subpage desktop_shell - Publish taskbar, tray, notification, jump-list,
  recent-item, and current-user shell registration state.
- @subpage desktop_accessibility - Publish immutable application-owned semantics
  through native providers, bounded actions, and notifications.
- @subpage desktop_lifecycle_events - Understand thread ownership, dispatch,
  queue overflow, close requests, waits, and native destruction.
- @subpage desktop_chrome_and_pointer_input - Configure system and custom chrome,
  drag regions, caption controls, cursor modes, and pointer routing.
- @subpage desktop_fullscreen_monitors - Choose windowed, borderless, and
  exclusive modes and handle monitor or topology changes.
- @subpage desktop_native_interop - Access a native handle without taking
  ownership or bypassing portable lifetime rules.
- @subpage desktop_renderer_integration - Enable concurrent presentation reads,
  attach renderer feedback, and publish packed pointer data.
- @subpage desktop_examples - See lifecycle, events, displays, fullscreen,
  shell integration, custom chrome, and renderer integration in context.
- @subpage desktop_troubleshooting - Diagnose ownership, capabilities, queue
  pressure, display transitions, native destruction, and renderer integration.
- @subpage desktop_future_extensions - Understand ownership boundaries for future
  UI and platform extensions.

## Maintainer validation

- @subpage desktop_testing - See automated, package, ABI, and platform coverage.
- @subpage desktop_test_hooks - Understand source-tree-only fault and state
  inspection seams.
- @subpage desktop_manual_validation - Run and interpret the visual behaviors
  that automation cannot prove.

## Generated API reference

See @ref desktop_public_api for header ownership, namespaces, and operation
names. The generated reference documents each declaration.

## Key behavior

- Threads, close, destruction, and queues: see @ref desktop_lifecycle_events.
- `Window` is non-copyable and non-movable. Cached getters never query the OS
  or allocate. The renderer bridge is the only opt-in for cross-thread reads
  (@ref desktop_renderer_integration).
- Shell resources are not Window members. They borrow an application-owned
  queue, and the owner thread mutates them.
- Public text is UTF-8; a Window title may not contain U+0000.
- Units: see @ref desktop_coordinates_and_dpi.

## Public header boundary

`desktop/window.h` assembles the common Window surface. Rich display inspection,
cursors, child hosts, accessibility, transfer, dialogs, shell, renderer
integration, and native interop stay opt-in. See @ref desktop_public_api for
the complete header map and operation ownership.

Installed consumers link `GameWIP::Desktop`. Desktop is built as a shared library: process-local Window and monitor identities, native
class ownership, dispatchers, and registries must remain coherent through one runtime instance rather than being duplicated across statically linked
modules.

## Dependency boundary

Desktop is installed as the shared target `GameWIP::Desktop`. Status and
FileSystem are public package dependencies; Unicode is a private native-text
conversion dependency. FileSystem supplies Desktop's transitive IO dependency.

Desktop owns top-level native state, cached geometry, event translation, queried
display/color facts, and the persistent packed pointer mask published through
`desktop/renderer_bridge.h`.

It does not own input state, action mapping, renderer surfaces or swapchains,
renderer HDR metadata, tone mapping, GPU readback, UI layout, application loops,
or coordination policy across several Windows.
