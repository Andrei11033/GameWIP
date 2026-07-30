@page window_quick_start Quick start

## Include

```cpp
#include "window/window.h"
```

The portable header does not require `windows.h`.

## Installed CMake

Set `GAMEWIP_REQUIRED_VERSION` from the consumer dependency lock; see @ref project_library_compatibility.

```cmake
find_package(Window ${GAMEWIP_REQUIRED_VERSION} EXACT CONFIG REQUIRED)
target_link_libraries(MyTarget PRIVATE GameWIP::Window)
```

The package resolves exact-version IO and FileSystem dependencies.

## Source-tree CMake

```cmake
target_link_libraries(MyTarget PRIVATE Window)
```

## Minimal event loop

```cpp
#include "window/window.h"

int main()
{
    namespace Window = GameWIP::Window;

    Window::Types::Description description;
    description.title = "GameWIP example";
    description.clientSize = {1280, 720};
    description.visible = true;
    description.requestFocus = true;

    Window::Window window;
    if (const auto status = window.open(description); !status.ok())
        return 1;

    while (!window.closeRequested())
    {
        const auto pump = Window::waitEvents();
        if (!pump.status.ok())
        {
            static_cast<void>(window.close());
            return 2;
        }

        Window::Types::Event event;
        while (window.popEvent(event))
        {
            if (const auto *size = event.getIf<Window::Types::FramebufferSizeChangedEvent>())
            {
                // Resize renderer-owned attachments to size->size.
            }
        }
    }

    return window.close().ok() ? 0 : 3;
}
```

## Failure handling

Expected validation, unsupported-operation, ownership, creation, conversion, and native failures use IO status/result values. Failed creation leaves the object closed. Recoverable `close()` failure leaves it open so the owning thread may retry; a late cleanup diagnostic may accompany an already completed close, in which case `isOpen()` is false.

Use explicit `close()` when cleanup errors matter. The destructor is `noexcept`; normal cleanup belongs on the owner thread. Wrong-thread destruction transfers the state to that dispatcher for complete owner-thread cleanup, including during dispatcher/thread shutdown.

## Where to go next

- @ref window_public_api maps the complete surface.
- @ref window_package_abi documents installed headers and the required Windows manifest.
- @ref window_coordinates_and_dpi defines logical, screen, and pixel units.
- @ref window_lifecycle_and_events explains thread affinity, fixed storage, coalescing, and close intent.
- @ref window_chrome_and_pointer_input covers declarative native hit testing.
- @ref window_fullscreen_and_monitors covers monitor identities and mode restoration.
- @ref window_native_interop and @ref window_renderer_integration define the two narrow Renderer integration seams.
- @ref window_examples contains focused integration examples.
