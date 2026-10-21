@page desktop_examples Examples

Examples build on the lifecycle in @ref desktop_quick_start. Snippets use an already
open owner-thread `window` unless they create one.

## Open a normal Window

```cpp
#include "desktop/window.h"

#include <chrono>

GameWIP::Desktop::Types::Description description;
description.title = "Example";
description.windowed.clientSize = {1280, 720};
description.activation.visible = true;
description.fileDropEnabled = true;

GameWIP::Desktop::Window window;
if (auto status = window.open(description); !status.ok())
    return;
```

## Pump and consume typed events

```cpp
while (window.isOpen() && !window.hasCloseRequest())
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

After the loop, call `window.close()` and check its status. `isOpen()` also ends the loop after
unexpected native destruction; retained events can still be consumed before finalization.

## Enumerate displays

```cpp
#include "desktop/display_info.h"

const auto monitors = GameWIP::Desktop::Display::getMonitors();
if (monitors.ok())
{
    for (const auto &monitor : *monitors)
    {
        const auto current = GameWIP::Desktop::Display::getCurrentMode(monitor.id);
        const auto color = GameWIP::Desktop::Display::getColorInfo(monitor.id);
    }
}
```

## Borderless fullscreen

```cpp
GameWIP::Desktop::Types::Description description;
description.initialWindowMode = GameWIP::Desktop::Types::WindowModes::borderlessFullscreen();
description.activation.visible = true;
```

For exclusive fullscreen use `Types::WindowModes::exclusiveFullscreen()` (current monitor mode) or
pass a `Types::Display::Mode` (and optionally a `MonitorId`). The restored logical size stays in
`description.windowed.clientSize`.

## Custom cursor

```cpp
#include "desktop/cursor.h"

if (window.supports(GameWIP::Desktop::Types::Capability::CustomCursor))
{
    auto cursorImage = GameWIP::Desktop::Types::Cursor::ImageView::packed({32, 32}, {4, 3}, cursorRgba8);
    auto cursor = GameWIP::Desktop::createCursor(cursorImage);
    if (cursor.ok())
        static_cast<void>(GameWIP::Desktop::setCursor(window, *cursor));
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
    if (native.ok())
    {
        // Create externally managed native descendants below native->window.
    }
}
```

Shut external technology down before `host.close()` if its SDK needs explicit teardown. See @ref desktop_child_surfaces.

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
if (pasted.ok())
{
    // Insert *pasted into the application-owned selection.
}
```

For multi-format publication, build ordered `Types::DataTransfer::ItemView` values and call
`Clipboard::write()`; on failure inspect `commitState` and `formatsPublished`. See @ref desktop_clipboard.

## Accept native text and file drops

```cpp
#include "desktop/drag_drop.h"

#include <array>
#include <optional>

namespace Desktop = GameWIP::Desktop;
namespace DD = GameWIP::Desktop::Types::DragDrop;
namespace Transfer = GameWIP::Desktop::Types::DataTransfer;

std::array formats{
    Transfer::FormatView::text(),
    Transfer::FormatView::files()};
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

Close the target before the window (@ref desktop_lifecycle_events). File drop and `DragDropTarget`
are exclusive: see @ref desktop_drag_drop.

## Select a file

```cpp
#include "desktop/dialogs.h"

#include <array>
#include <string_view>

using namespace GameWIP::Desktop;

const std::array imageExtensions{std::string_view{"png"}, std::string_view{"jpg"}};
const std::array filters{
    Types::Dialogs::File::Filter{"Images", imageExtensions},
    Types::Dialogs::File::Filter{"All files", {}}};

Types::Dialogs::File::OpenDescription request;
request.owner = &window;
request.title = "Open image";
request.filters = filters;

const auto selected = Dialogs::openFile(request);
if (selected.status.ok() &&
    selected.outcome == Types::Dialogs::Outcome::Accepted)
{
    // Consume selected.path.
}
```

Runs synchronously on the owner Window's thread. Cancellation is a successful outcome. See @ref desktop_dialogs.

## Publish operation progress

```cpp
#include "desktop/dialogs.h"

GameWIP::Desktop::ProgressDialog progress;
GameWIP::Desktop::Types::Dialogs::Progress::Description request;
request.owner = &window;
request.title = "Importing";
request.heading = "Preparing assets";
request.presentationMode = GameWIP::Desktop::Types::Dialogs::Progress::Mode::Determinate;
request.cancelable = true;

if (progress.open(request).ok())
{
    static_cast<void>(progress.setProgress(0.5));
    if (progress.hasCancelRequest())
    {
        // Ask the application-owned operation to stop.
    }
    static_cast<void>(progress.close());
}
```

Progress is modeless, uses the ordinary event pump, and creates no worker thread; workers must report back to the owner thread.

## Publish shell state

```cpp
#include "desktop/shell.h"
#include "desktop/shell_taskbar.h"

if (GameWIP::Desktop::Shell::supports(
        GameWIP::Desktop::Types::Shell::Capability::Taskbar) &&
    GameWIP::Desktop::Shell::supports(
        GameWIP::Desktop::Types::Shell::Capability::TaskbarProgress))
{
    GameWIP::Desktop::ShellEventQueue shellEvents;
    if (shellEvents.open().ok())
    {
        GameWIP::Desktop::TaskbarItem taskbar;
        GameWIP::Desktop::Types::Taskbar::Description taskbarDescription;
        taskbarDescription.progress = GameWIP::Desktop::Types::Shell::Progress{
            GameWIP::Desktop::Types::Shell::ProgressState::Normal, 0.5};

        if (taskbar.open(window, taskbarDescription, shellEvents).ok())
        {
            // Consume shellEvents while the Window and taskbar binding remain open.
            GameWIP::Desktop::Types::Shell::Event event;
            while (shellEvents.popEvent(event))
            {
                // Handle typed taskbar, tray, or notification payloads here.
            }
            static_cast<void>(taskbar.close());
        }
        static_cast<void>(shellEvents.close());
    }
}
```

Close each resource, then its `ShellEventQueue`, then the `Window`. Keep pumping `Desktop::Events`;
draining the queue alone dispatches nothing. See @ref desktop_shell.

## Publish an accessible command

Enable the bridge on the open Window's owner thread before calling this helper.
The helper only publishes, so later calls may run on another thread while the
Window remains alive. Supply a strictly newer nonzero generation each time.

```cpp
#include "desktop/accessibility.h"
#include <array>

GameWIP::IO::Types::Status publishAccessibleCommand(
    GameWIP::Desktop::Window &window,
    GameWIP::Desktop::Types::Accessibility::Generation generation)
{
    namespace D = GameWIP::Desktop;
    namespace A = D::Types::Accessibility;
    auto bridge = window.accessibility();
    const std::array<A::NodeId, 1> children{2};
    std::array<A::Node, 2> nodes{};
    nodes[0].id = 1;
    nodes[0].role = A::Role::Window;
    nodes[0].name = "Editor";
    nodes[0].children = children;
    nodes[1].id = 2;
    nodes[1].parent = 1;
    nodes[1].role = A::Role::Button;
    nodes[1].name = "Save document";
    nodes[1].states.flags = static_cast<std::uint64_t>(A::State::Focusable);
    nodes[1].actions.flags = (std::uint64_t{1} << static_cast<unsigned>(A::ActionKind::Invoke));
    nodes[1].geometry = A::Geometry{{20, 20, 160, 40}};
    return bridge.publish({generation, 1, nodes});
}
```

For initial activation on the owner thread:

```cpp
const auto enabled = window.accessibility().enable();
if (enabled.ok())
{
    const auto published = publishAccessibleCommand(window, 1);
    // Inspect published before starting native or worker-thread use.
}
```

The call copies every view. Keep pumping `Desktop::Events`, drain `window.accessibility().popAction(request)` on the owner
thread, and run Save in application code after checking identity and observed generation (native Invoke only queues the request).
Publish resulting state with a newer generation. See @ref desktop_accessibility.

## Renderer integration

```cpp
#include "desktop/renderer_bridge.h"

const auto concurrentReads = GameWIP::Desktop::Renderer::enableConcurrentPresentationReads(window);
if (concurrentReads.ok())
{
    // Start the renderer thread only after enablement completes. It may now use
    // the documented presentation getters, including framebufferSize(),
    // currentMonitor(), and occluded().
}

if (window.supports(GameWIP::Desktop::Types::Capability::OcclusionReporting))
{
    static_cast<void>(GameWIP::Desktop::Renderer::attachOcclusionProvider(window));
    if (GameWIP::Desktop::Renderer::hasOcclusionProvider(window))
        static_cast<void>(GameWIP::Desktop::Renderer::reportOcclusion(window, true));
}
```

Concurrent reads and occlusion feedback are independent opt-ins. See @ref desktop_renderer_integration.

## Native Win32 view

```cpp
#include "desktop/native/win32.h"

const auto native = GameWIP::Desktop::Native::Win32::getHandle(window);
if (native.ok())
{
    HWND hwnd = native->window;
}
```
