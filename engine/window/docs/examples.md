@page window_examples Examples

These focused examples build on the owner-thread lifecycle from
@ref window_quick_start and demonstrate displays, custom cursors, renderer
feedback, and native interop without hiding status handling.

## Open a normal Window

```cpp
#include "window/window.h"

GameWIP::Window::Types::Description description;
description.title = "Example";
description.clientSize = {1280, 720};
description.visible = true;
description.fileDropEnabled = true;

GameWIP::Window::Window window;
if (auto status = window.open(description); !status.ok())
    return;
```

## Pump and consume typed events

```cpp
while (!window.hasCloseRequest())
{
    const auto pump = GameWIP::Window::Events::wait(std::chrono::milliseconds{16});
    if (!pump.status.ok())
        break;

    GameWIP::Window::Types::Event event;
    while (window.popEvent(event))
    {
        if (const auto *drop = event.getIf<GameWIP::Window::Types::Events::FilesDropped>())
        {
            for (const auto &path : drop->paths)
            {
                // consume FileSystem::Types::Path
            }
        }
    }
}
```

## Enumerate displays

```cpp
#include "window/display_info.h"

const auto monitors = GameWIP::Window::Display::getMonitors();
if (monitors.status.ok())
{
    for (const auto &monitor : monitors.monitors)
    {
        const auto current = GameWIP::Window::Display::getCurrentMode(monitor.id);
        const auto color = GameWIP::Window::Display::getColorInfo(monitor.id);
    }
}
```

## Borderless fullscreen

```cpp
GameWIP::Window::Types::Description description;
description.mode.mode = GameWIP::Window::Types::Mode::BorderlessFullscreen;
description.visible = true;
```

For exclusive fullscreen, set `description.mode.mode` to `Types::Mode::ExclusiveFullscreen`, choose a `Types::Display::MonitorId`, and optionally
provide an exact `Types::Display::Mode`.

## Custom cursor

```cpp
#include "window/cursor.h"

if (window.supports(GameWIP::Window::Types::Capability::CustomCursor))
{
    GameWIP::Window::Types::Cursor::ImageView cursorImage{{32, 32}, {4, 3}, 96, 0, cursorRgba8};
    auto cursor = GameWIP::Window::createCursor(cursorImage);
    if (cursor.status.ok())
        static_cast<void>(GameWIP::Window::setCursor(window, cursor.cursor));
}
```

Use @ref window_custom_cursors for multiple-DPI variants, sharing, lifetime, cursor-mode interaction, and restoring a system shape.

## Renderer feedback

```cpp
#include "window/renderer_bridge.h"

if (window.supports(GameWIP::Window::Types::Capability::OcclusionReporting))
{
    static_cast<void>(GameWIP::Window::Renderer::attachOcclusionProvider(window));
    if (GameWIP::Window::Renderer::hasOcclusionProvider(window))
        static_cast<void>(GameWIP::Window::Renderer::reportOcclusion(window, true));
}
```

## Native Win32 view

```cpp
#include "window/native/win32.h"

const auto native = GameWIP::Window::Native::Win32::getHandle(window);
if (native.status.ok())
{
    HWND hwnd = native.handle.window;
}
```
