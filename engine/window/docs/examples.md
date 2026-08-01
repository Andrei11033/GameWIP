@page window_examples Examples

The @ref window_quick_start page contains a complete single-window event loop. The focused examples below omit repetitive status reporting but always check operations whose failure affects the workflow.

## Multiple independent windows

```cpp
GameWIP::Window::Window mainWindow;
GameWIP::Window::Window toolWindow;

GameWIP::Window::Types::Description mainDescription;
mainDescription.title = "Main";
mainDescription.visible = true;

if (!mainWindow.open(mainDescription).ok()) return 1;

auto toolDescription = mainDescription;
toolDescription.title = "Tool";
toolDescription.owner = mainWindow.id();
if (!toolWindow.open(toolDescription).ok()) return 2;

while (!mainWindow.closeRequested())
{
    const auto pump = GameWIP::Window::waitEvents();
    if (!pump.status.ok()) break;

    GameWIP::Window::Types::Event event;
    while (mainWindow.popEvent(event)) {}
    while (toolWindow.popEvent(event)) {}
}

const auto toolClose = toolWindow.close();
const auto mainClose = mainWindow.close();
return toolClose.ok() && mainClose.ok() ? 0 : 3;
```

## Caller-provided event storage

```cpp
std::array<GameWIP::Window::Types::Event, 64> events;
GameWIP::Window::Window window;

if (!window.open({}, events).ok()) return 1;
const auto info = window.eventQueueInfo();
// info.storage == EventStorageKind::External; events must not move before close.
return window.close().ok() ? 0 : 2;
```

## Custom chrome

```cpp
using namespace GameWIP::Window;

Window window;
Types::Description description;
description.decoration = Types::DecorationMode::Custom;
if (!window.open(description).ok()) return 1;

const std::array draggable{Types::LogicalRect{{0, 0}, {800, 40}}};
Types::CustomChromeLayout chrome;
chrome.draggableRegions = draggable;
chrome.minimizeButtonRegion = Types::LogicalRect{{680, 0}, {40, 40}};
chrome.maximizeButtonRegion = Types::LogicalRect{{720, 0}, {40, 40}};
chrome.closeButtonRegion = Types::LogicalRect{{760, 0}, {40, 40}};
return window.setCustomChromeLayout(chrome).ok() ? 0 : 2;
```

Window copies the array during the call, so the caller does not need to retain it afterward.

## Borderless fullscreen

```cpp
GameWIP::Window::Types::ModeRequest mode;
mode.mode = GameWIP::Window::Types::WindowMode::BorderlessFullscreen;
mode.monitor = window.currentMonitor();
if (!window.setMode(mode).ok()) return 1;

// Later restore saved windowed placement.
mode = {};
return window.setMode(mode).ok() ? 0 : 2;
```

Borderless fullscreen changes only the native Window style and monitor-sized placement; it does not change the monitor's display mode.

## Exclusive fullscreen

```cpp
const auto monitor = window.currentMonitor();
const auto modes = GameWIP::Window::getDisplayModes(monitor);
if (!modes.status.ok() || modes.displayModes.empty()) return 1;

GameWIP::Window::Types::ModeRequest mode;
mode.mode = GameWIP::Window::Types::WindowMode::ExclusiveFullscreen;
mode.monitor = monitor;
mode.displayMode = modes.displayModes.front(); // Choose an appropriate enumerated mode.
if (!window.setMode(mode).ok()) return 2;

// Later restore the saved desktop mode, style, and windowed placement.
return window.setMode({}).ok() ? 0 : 3;
```

An exact exclusive request must use a mode returned for the target monitor. Exclusive mode suspends when the Window loses focus and resumes when it regains focus.

## Whole-window click-through overlay

```cpp
using namespace GameWIP::Window;
if (!window.supports(Types::Capability::PointerClickThrough)) return 0;
if (!window.setPointerInputLayout(
        {.mode = Types::PointerInputMode::ClickThrough}).ok()) return 1;

// Later restore native client and frame input.
return window.setPointerInputLayout({}).ok() ? 0 : 2;
```

## Capability-guarded pointer regions

```cpp
using namespace GameWIP::Window;
if (!window.supports(Types::Capability::PointerRegions)) return 0;
const std::array controls{Types::LogicalRect{{16, 16}, {240, 80}}};
Types::PointerInputLayout input;
input.mode = Types::PointerInputMode::AcceptRegions;
input.regions = controls;
if (!window.setPointerInputLayout(input).ok()) return 1;
return window.setAlwaysOnTop(true).ok() ? 0 : 2;
```

The current Win32 backend takes the early return because it does not advertise region-based or per-pixel desktop routing.

## Native Win32 handle

```cpp
#include "window/native/win32.h"

const auto result = GameWIP::Window::Native::Win32::getHandle(window);
if (result.status.ok())
{
    HWND nonOwning = result.handle.window;
    // Pass nonOwning to the renderer's Win32 surface attachment.
}
```

## Renderer occlusion feedback

```cpp
#include <window/renderer.h>

namespace Feedback = GameWIP::Window::Renderer;

if (!Feedback::attachOcclusionProvider(window).ok()) return 1;

// Run on the Window owner thread after consuming the renderer's latest result.
if (!Feedback::reportOcclusion(window, presentWasOccluded).ok()) return 2;

GameWIP::Window::Types::Event event;
while (window.popEvent(event))
{
    if (const auto *changed = event.getIf<GameWIP::Window::Types::OcclusionChangedEvent>())
    {
        // Pause optional rendering work when changed->occluded is true.
    }
}

return Feedback::detachOcclusionProvider(window).ok() ? 0 : 3;
```

The Renderer bridge defines how `presentWasOccluded` reaches the owner thread. Repeated values do not add events, and the cached `window.isOccluded()` value remains authoritative if the queue overflows.

## Packed pointer mask publication

```cpp
#include <window/renderer.h>

const auto target = GameWIP::Window::Renderer::beginPointerHitMaskUpdate(window);
if (!target.status.ok()) return 1;
std::vector<std::uint64_t> words(
    target.target.requiredWordCount, 0);

// Example: accept the first physical framebuffer pixel.
if (!words.empty()) words[0] |= 1ULL;

const auto status =
    GameWIP::Window::Renderer::publishPointerHitMask(window, target.target.generation, words);
if (!status.ok()) return 1;
```

Run begin and publication on the Window owner thread. Renderer owns thresholding and asynchronous readback; Window owns generations, and an older completion returns `Interrupted`.

## Monitor and display-mode enumeration

```cpp
const auto monitors = GameWIP::Window::getMonitors();
if (!monitors.status.ok()) return 1;

for (const auto &monitor : monitors.monitors)
{
    const auto modes = GameWIP::Window::getDisplayModes(monitor.id);
    if (!modes.status.ok()) return 2;
    for (const auto &mode : modes.displayModes)
    {
        // mode.resolution is physical pixels; refresh is millihertz.
    }
}
```

## Display color information

```cpp
#include <window/renderer.h>

const auto color =
    GameWIP::Window::Renderer::getWindowDisplayColorInfo(window);
if (!color.status.ok()) return 1;

if (color.info.hdrSupported && color.info.hdrEnabled)
{
    // Renderer chooses its swapchain, presentation color space,
    // HDR metadata, and tone-mapping policy.
}
```

Run the Window form on its owner thread. Re-query after `MonitorChangedEvent` or `DisplayConfigurationChangedEvent`; zero luminance or channel-precision fields mean the operating system did not provide reliable optional metadata.

## Related pages

- @ref window_coordinates_and_dpi
- @ref window_renderer_integration
- @ref window_manual_validation
