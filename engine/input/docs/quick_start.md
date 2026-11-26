@page input_quick_start Quick start

Input separates the portable, frame-based API from the platform attachment
that supplies hardware observations. A `System` owns one capture session;
create the platform attachment after opening the system and destroy it before
the system.

## Include and link

```cpp
#include "input/input.h"
#include "input/win32.h"
```

Link the consumer to `GameWIP::Input`. The Win32 attachment is implemented by the
Win32 backend and observes messages from an existing owner-thread window.

## Windows integration

For Desktop integration, link `GameWIP::Input` and `GameWIP::Desktop`. Include
`desktop/window.h` and use `GameWIP::Input::Platform::Win32::attachToWindow()`
to connect Input and message routing to an existing `Desktop::Window`.

```cpp
#if defined(_WIN32)
#include "desktop/window.h"
#include "input/native/desktop_win32.h"

GameWIP::Desktop::Window window;
GameWIP::Input::System input;
GameWIP::Input::Platform::Win32::Attachment attachment;

if(window.open().ok() && input.open().ok())
{
    const auto status = GameWIP::Input::Platform::Win32::attachToWindow(input, attachment, window);
    if(!status.ok())
    {
        static_cast<void>(input.close());
        return 1;
    }
}
#endif
```

Manual attachment to non-Desktop windows uses `Attachment::attach(input, hwnd)`.

By default, Windows tries each available input backend. Set
`Configuration::enabledBackends` before `System::open()` to select
`Win32RawInput`, `Win32Hid`, `XInput`, `GameInput`, `DirectInput`, or
`Win32WindowMessage`; an empty list disables them all. `Attachment::backends()`
reports which providers started, were disabled, or were unavailable. A disabled
provider is not initialized or polled.

The window-message provider supplies a keyboard fallback without physical-device
identity. Raw Input preserves physical-device identity for keyboard and mouse
reports.

## Open and attach

Keep the `System`, `Attachment`, and `Frame` in stable storage. Attach after opening
the system; detach before closing. Open, attach, message handling, and capture must
run on the same owner thread:

```cpp
GameWIP::Input::System input;
if(!input.open().ok())
{
    return 1;
}

GameWIP::Input::Platform::Win32::Attachment attachment;
if(!attachment.attach(input, hwnd).ok())
{
    static_cast<void>(input.close());
    return 2;
}

// ... capture loop ...

if(!attachment.detach().ok())
{
    // Log or handle detach failure
}
static_cast<void>(input.close());
```

In the window procedure, pass `message`, `wParam`, and `lParam` to
`Attachment::handleMessage`. Check `ok()` before reading `value()`; use the
caller's normal default-message handling when the value is `false`. Always pass
`WM_INPUT` to `DefWindowProcW` after processing so Windows can clean up the
raw-input handle. Capture once per iteration of the application's event or
frame loop.

## Resolve and read controls

Resolve controls from connected device descriptors and retain their element
IDs for the current open session:

```cpp
const auto devicesResult = input.devices();
if(!devicesResult.ok())
{
    return 4;
}
const std::span<const GameWIP::Input::Types::DeviceInfo> devices = devicesResult.value();

auto key = GameWIP::Input::Query::findStandard(devices, GameWIP::Input::Standard::Key::W);
```

Capture into a reusable frame in the application loop, then query its state:

```cpp
GameWIP::Input::Types::Frame frame;
while(appIsRunning)
{
    // Pump the application's window messages and forward them to the attachment.
    const auto capture = input.capture(frame);
    if(!capture.ok())
    {
        break;
    }

    bool refreshBindings = false;
    for(const GameWIP::Input::Types::Event &event : frame.events())
    {
        refreshBindings |= event.kind == GameWIP::Input::Types::EventKind::DeviceConnected ||
                           event.kind == GameWIP::Input::Types::EventKind::DeviceDisconnected ||
                           event.kind == GameWIP::Input::Types::EventKind::DeviceChanged;
    }
    if(refreshBindings)
    {
        const auto updatedDevices = input.devices();
        if(!updatedDevices.ok())
        {
            break;
        }
        key = GameWIP::Input::Query::findStandard(updatedDevices.value(), GameWIP::Input::Standard::Key::W);
    }

    if(key && frame.isDown(*key))
    {
        // Move forward.
    }
}
```

`appIsRunning` represents the application's existing loop condition. Query
helpers search connected devices by default and return the first match; pass a
device ID to restrict the search. Selectors filter lookup results by the
criteria supplied, not the live controls captured by a backend. Controls
outside the standard vocabulary remain available through HID identities or
producer-defined persistent keys. Runtime IDs are valid only for the current
open session.

## Mice and pointer position

Raw Input preserves physical-device identity: each mouse has its own buttons,
wheel, and relative movement elements. Those reports are delivered through
`WM_INPUT`. In contrast, `WM_MOUSEMOVE` reports the shared client-relative
cursor position and does not identify which physical mouse caused it. Input
therefore exposes cursor position as `Standard::Pointer::Position` on a
window-level pointer device, separate from the per-device mouse controls.

## Synthetic observations

Include `input/synthetic.h` for deterministic tests or explicit software input.
Attach a `Synthetic::Source` before publishing values or text, and detach it
before its `System` closes. `Source::setAbsoluteValue` replaces an absolute
value, while `Source::addRelativeDelta` adds a delta for the current frame.
`Source::setValue` uses the reporting mode in the element descriptor.

For text, use `attachment.surfaceId()` to target the attached Win32 window.
Other synthetic surfaces may use any nonzero application-defined
`Types::SurfaceId`.

```cpp
const auto surface = attachment.surfaceId();
if(surface.isValid())
{
    static_cast<void>(source.commitText(surface, "Hello"));
}
```
