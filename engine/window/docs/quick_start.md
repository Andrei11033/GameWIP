@page window_quick_start Quick start

This path opens one native top-level Window, processes its events on the owner
thread, and closes it explicitly. It establishes the lifecycle and threading
model used by every more advanced Window feature.

## Include

The normal Window surface is:

```cpp
#include "window/window.h"
```

Include `window/display_info.h`, `window/cursor.h`, `window/child_surface.h`,
`window/clipboard.h`, `window/renderer_bridge.h`, or `window/native/win32.h`
only when using rich display inspection, custom native cursors, native child
hosts, Clipboard/data transfer, renderer feedback, or Win32 interop.

## Installed CMake

Set `GAMEWIP_REQUIRED_VERSION` from the consuming project's dependency lock;
see @ref project_library_compatibility.

```cmake
find_package(Window ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Window)
```

## Source-tree CMake

```cmake
target_link_libraries(MyTarget PRIVATE Window)
```

## Minimal usage

This complete owner-thread example opens a window, pumps events until a sticky
close request is observed, and closes the native resource:

```cpp
#include "window/window.h"

#include <chrono>

int main()
{
    GameWIP::Window::Types::Description description;
    description.title = "GameWIP";
    description.visible = true;

    GameWIP::Window::Window window;
    if (!window.open(description).ok())
    {
        return 1;
    }

    while (!window.hasCloseRequest())
    {
        const auto pump =
            GameWIP::Window::Events::wait(std::chrono::milliseconds{16});
        if (!pump.status.ok())
        {
            static_cast<void>(window.close());
            return 2;
        }

        GameWIP::Window::Types::Event event;
        while (window.popEvent(event))
        {
            // React to the event payload needed by the application.
        }
    }

    return window.close().ok() ? 0 : 3;
}
```

`Window` is non-copyable and non-movable. Keep it in stable storage for its
entire open lifetime, and perform lifecycle/event operations on its owner
thread. Public text is UTF-8. File-drop paths use `FileSystem::Types::Path` and
are not flattened into text.

## Failure handling

Window operations return direct `IO::Types::Status` values or result structures
containing a `status`. Inspect the status before consuming the associated
payload. A failed event wait or pump does not close an open window
automatically. Call `close()` and handle its status; destruction performs only
best-effort cleanup.

The close request is sticky even if the corresponding queue event is coalesced
or dropped. Use `hasCloseRequest()` for lifecycle policy and `popEvent()` for
individual event payloads.

## Where to go next

- @ref window_public_api inventories headers, namespaces, types, and operations.
- @ref window_lifecycle_events defines ownership, queues, waiting, and close behavior.
- @ref window_coordinates_and_dpi explains logical and pixel coordinate contracts.
- @ref window_custom_cursors explains custom images, DPI variants, and shared lifetime.
- @ref window_child_surfaces explains native child hosting and external descendant ownership.
- @ref window_clipboard explains service calls that work with no Window open.
- @ref window_examples provides focused display, renderer, and native examples.
- @ref window_troubleshooting maps common failures to their owning contract.
