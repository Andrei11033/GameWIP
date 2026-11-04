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

Link the consumer to `GameWIP::Input`. On Windows, the Win32 attachment uses the
caller's existing window and does not create or subclass it. On other supported
platforms, the optional Win32 API remains linkable but native attachment is
unsupported.

## Open and attach

Keep the `System`, `Attachment`, and `Frame` in stable storage. Attach after
opening the system. Open, attach, message handling, and capture must run on the
same owner thread:

```cpp
GameWIP::Input::System input;
if (!input.open().ok())
{
    return 1;
}

GameWIP::Input::Platform::Win32::Attachment attachment;
if (!attachment.attach(input, hwnd).ok())
{
    static_cast<void>(input.close());
    return 2;
}
```

In the window procedure, pass `message`, `wParam`, and `lParam` to
`Attachment::handleMessage`. Use the caller's normal default-message handling
when the attachment does not handle a message. Always pass `WM_INPUT` to
`DefWindowProcW` after processing so Windows can clean up the raw-input handle.
Capture once per iteration of the application's event or frame loop.

## Resolve and read controls

Resolve controls from connected device descriptors and retain their element
IDs for the current open session:

```cpp
std::span<const GameWIP::Input::Types::DeviceInfo> devices;
if (!input.devices(devices).ok())
{
    return 4;
}

auto key = GameWIP::Input::Query::findStandard(
    devices,
    GameWIP::Input::Standard::Key::W);
```

Capture into a reusable frame in the application loop, then query its state:

```cpp
GameWIP::Input::Types::Frame frame;
while (appIsRunning)
{
    // Pump the application's window messages and forward them to the attachment.
    const auto capture = input.capture(frame);
    if (!capture.status.ok())
    {
        break;
    }

    bool refreshBindings = false;
    for (const GameWIP::Input::Types::Event &event : frame.events())
    {
        refreshBindings |= event.kind == GameWIP::Input::Types::EventKind::DeviceConnected ||
                           event.kind == GameWIP::Input::Types::EventKind::DeviceDisconnected ||
                           event.kind == GameWIP::Input::Types::EventKind::DeviceChanged;
    }
    if (refreshBindings)
    {
        if (!input.devices(devices).ok())
        {
            break;
        }
        key = GameWIP::Input::Query::findStandard(
            devices,
            GameWIP::Input::Standard::Key::W);
    }

    if (key && frame.isDown(*key))
    {
        // Move forward.
    }
}
```

`appIsRunning` represents the application's existing loop condition. An
unfiltered query selects the first matching connected control. Pass a device
ID to target a specific device. Use
`Query::findElementByPersistentKey` for producer-defined binding keys, or
`Query::findHid` for HID controls outside the standard vocabulary. Runtime IDs
are valid only for the current open session.

## Mice and pointer position

Raw Input preserves physical-device identity: each mouse has its own buttons,
wheel, and relative movement elements. Those reports are delivered through
`WM_INPUT`. In contrast, `WM_MOUSEMOVE` reports the shared client-relative
cursor position and does not identify which physical mouse caused it. Input
therefore exposes cursor position as `Standard::Pointer::Position` on a
window-level pointer device, separate from the per-device mouse controls.

## Synthetic observations

Include `input/synthetic.h` only for deterministic tests or explicit
software-produced devices. `Source::setAbsoluteValue` replaces an absolute
value, while `Source::addRelativeDelta` adds a delta to a relative element for
the current frame. `Source::setValue` remains available when the reporting
mode is determined by the descriptor. Synthetic sources should be detached
before their `System` closes, although the API safely detects a closed or
destroyed session.

Include `input/synthetic.h` and attach a `Synthetic::Source` before publishing
software-generated text. Use `attachment.surfaceId()` to target the attached
Win32 window; other synthetic surfaces may use any nonzero application-defined
`Types::SurfaceId`.

```cpp
const auto surface = attachment.surfaceId();
if (surface.isValid())
{
    static_cast<void>(source.commitText(surface, "Hello"));
}
```
