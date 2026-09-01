@page desktop_examples Examples

These focused examples build on the owner-thread lifecycle from
@ref desktop_quick_start and demonstrate displays, custom cursors, native child
hosts, Clipboard data exchange, native drag and drop, renderer integration, and native interop without
hiding status handling.

## Open a normal Window

```cpp
#include "desktop/window.h"

GameWIP::Desktop::Types::Description description;
description.title = "Example";
description.clientSize = {1280, 720};
description.visible = true;
description.fileDropEnabled = true;

GameWIP::Desktop::Window window;
if (auto status = window.open(description); !status.ok())
    return;
```

## Pump and consume typed events

```cpp
while (!window.hasCloseRequest())
{
    const auto pump = GameWIP::Desktop::Events::wait(std::chrono::milliseconds{16});
    if (!pump.status.ok())
        break;

    GameWIP::Desktop::Types::Event event;
    while (window.popEvent(event))
    {
        if (const auto *drop = event.getIf<GameWIP::Desktop::Types::Events::FilesDropped>())
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
#include "desktop/display_info.h"

const auto monitors = GameWIP::Desktop::Display::getMonitors();
if (monitors.status.ok())
{
    for (const auto &monitor : monitors.monitors)
    {
        const auto current = GameWIP::Desktop::Display::getCurrentMode(monitor.id);
        const auto color = GameWIP::Desktop::Display::getColorInfo(monitor.id);
    }
}
```

## Copy and paste UTF-8 text

```cpp
#include "desktop/clipboard.h"

const auto copied = GameWIP::Desktop::Clipboard::writeText("selected UTF-8 text");
if (!copied.status.ok())
{
    // copied.commitState says whether the external Clipboard changed.
}

const auto pasted = GameWIP::Desktop::Clipboard::readText(
    std::chrono::milliseconds{50});
if (pasted.status.ok())
{
    // Insert pasted.text into the application-owned selection.
}
```

The UI decides when a shortcut or menu command calls these operations. For
multi-format publication, construct ordered `Types::DataTransfer::ItemView`
values and call `Clipboard::write()`; inspect both `commitState` and
`formatsPublished` on failure. See @ref desktop_clipboard.

## Accept native text and file drops

```cpp
#include "desktop/drag_drop.h"

#include <array>
#include <optional>

namespace Desktop = GameWIP::Desktop;
namespace DD = GameWIP::Desktop::Types::DragDrop;
namespace Transfer = GameWIP::Desktop::Types::DataTransfer;

std::array formats{
    Transfer::FormatView{Transfer::FormatKind::Text, {}},
    Transfer::FormatView{Transfer::FormatKind::FileList, {}}};
std::array regions{DD::RegionDescription{
    DD::RegionId{1}, std::nullopt, formats,
    DD::Effect::Copy, DD::Effect::Copy}};

Desktop::DragDropTarget target;
if (const auto status = target.open(window, DD::TargetDescription{regions});
    !status.ok())
{
    return;
}

DD::Event event;
while (target.popEvent(event))
{
    if (const auto *dropped = event.getIf<DD::Events::Dropped>())
    {
        // Visit dropped->payload; every selected item is fully owned.
    }
}
```

The Window and target share an owner thread but retain separate queues. Continue
pumping `Desktop::Events`; call `target.close()` before `window.close()` during
ordinary controlled shutdown. See @ref desktop_drag_drop for regions, source
dragging, effects, and failure handling.

## Borderless fullscreen

```cpp
GameWIP::Desktop::Types::Description description;
description.mode.mode = GameWIP::Desktop::Types::Mode::BorderlessFullscreen;
description.visible = true;
```

For exclusive fullscreen, set `description.mode.mode` to `Types::Mode::ExclusiveFullscreen`, choose a `Types::Display::MonitorId`, and optionally
provide an exact `Types::Display::Mode`.

## Custom cursor

```cpp
#include "desktop/cursor.h"

if (window.supports(GameWIP::Desktop::Types::Capability::CustomCursor))
{
    GameWIP::Desktop::Types::Cursor::ImageView cursorImage{{32, 32}, {4, 3}, 96, 0, cursorRgba8};
    auto cursor = GameWIP::Desktop::createCursor(cursorImage);
    if (cursor.status.ok())
        static_cast<void>(GameWIP::Desktop::setCursor(window, cursor.cursor));
}
```

Use @ref desktop_custom_cursors for multiple-DPI variants, sharing, lifetime, cursor-mode interaction, and restoring a system shape.

## Native child host

```cpp
#include "desktop/child_surface.h"
#include "desktop/native/win32.h"

GameWIP::Desktop::ChildSurface host;
GameWIP::Desktop::Types::ChildSurface::Description hostDescription;
hostDescription.rect = {{40, 40}, {800, 600}};
hostDescription.visible = true;

if (host.open(window, hostDescription).ok())
{
    const auto native = GameWIP::Desktop::Native::Win32::getHandle(host);
    if (native.status.ok())
    {
        // Create externally managed native descendants below native.handle.window.
    }
}
```

Shut external technology down before `host.close()` when its SDK requires explicit teardown. See @ref desktop_child_surfaces for ownership, parent
loss, event queues, geometry, DPI, and sibling ordering.

## Renderer integration

```cpp
#include "desktop/renderer_bridge.h"

const auto concurrentReads = GameWIP::Desktop::Renderer::enableConcurrentPresentationReads(window);
if (concurrentReads.ok())
{
    // Start the renderer thread only after enablement completes. It may now use the documented
    // presentation getters directly, including framebufferSize(), currentMonitor(), and occluded().
}

if (window.supports(GameWIP::Desktop::Types::Capability::OcclusionReporting))
{
    static_cast<void>(GameWIP::Desktop::Renderer::attachOcclusionProvider(window));
    if (GameWIP::Desktop::Renderer::hasOcclusionProvider(window))
        static_cast<void>(GameWIP::Desktop::Renderer::reportOcclusion(window, true));
}
```

Concurrent presentation reads and occlusion feedback are independent opt-ins. Stop or join every renderer reader before destroying the C++
`Window` object. See @ref desktop_renderer_integration for the exact getter set and close/reopen behavior.

## Native Win32 view

```cpp
#include "desktop/native/win32.h"

const auto native = GameWIP::Desktop::Native::Win32::getHandle(window);
if (native.status.ok())
{
    HWND hwnd = native.handle.window;
}
```
