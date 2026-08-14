@page window_quick_start Quick start

Include the normal Window surface and create a closed object:

```cpp
#include "window/window.h"

GameWIP::Window::Types::Description description;
description.title = "GameWIP";
description.visible = true;

auto window = std::make_unique<GameWIP::Window::Window>();
const auto openStatus = window->open(description);
```

`Window` is non-copyable and non-movable, so use stable storage when indirect ownership is required.

A simple owner-thread loop uses the event service namespace and sticky close request:

```cpp
while (!window->hasCloseRequest())
{
    const auto pump = GameWIP::Window::Events::wait(std::chrono::milliseconds{16});
    if (!pump.status.ok())
        break;

    GameWIP::Window::Types::Event event;
    while (window->popEvent(event))
    {
        if (const auto *resized = event.getIf<GameWIP::Window::Types::Events::ClientSizeChanged>())
        {
            // resize renderer resources from resized->size
        }
    }
}

static_cast<void>(window->close());
```

For display enumeration/HDR inspection, explicitly include `window/display_info.h` and use `GameWIP::Window::Display`. For renderer feedback include `window/renderer_bridge.h`. For HWND access include `window/native/win32.h`.

Public text is UTF-8. File-drop paths use `FileSystem::Types::Path` and are not flattened into text.
