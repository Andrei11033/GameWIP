/// @file input.cpp
/// @brief Win32 Input mapping and Desktop attachment validation.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "validation/tests/input/platform.h"

#include "desktop/events.h"
#include "desktop/native/win32.h"
#include "desktop/window.h"
#include "input/input.h"
#include "input/native/desktop_win32.h"
#include "input/platform/win32/internal/win32_keyboard_mapping.h"
#include "test_support/test_support.h"

#include <windows.h>

#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

namespace
{
    namespace Desktop = GameWIP::Desktop;
    namespace Input = GameWIP::Input;
    namespace TestSupport = GameWIP::TestSupport;

    /// ------------------------------------------------------------
    /// Keyboard mapping
    /// ------------------------------------------------------------

    /// @brief Verifies standard and extended Win32 keyboard scan-code mappings.
    void testKeyboardMapping(TestSupport::Context &context)
    {
        using Input::Platform::Win32::Detail::keyboardUsageForScanCode;

        // Modifiers
        static_cast<void>(context.expectEq("left Shift maps to HID Left Shift", std::uint32_t{0xE1}, keyboardUsageForScanCode(0x2A, false, false)));
        static_cast<void>(context.expectEq("right Shift maps to HID Right Shift", std::uint32_t{0xE5}, keyboardUsageForScanCode(0x36, false, false)));
        static_cast<void>(context.expectEq("left Alt maps to HID Left Alt", std::uint32_t{0xE2}, keyboardUsageForScanCode(0x38, false, false)));
        static_cast<void>(
            context.expectEq("right Alt uses its E0-prefixed scan code", std::uint32_t{0xE6}, keyboardUsageForScanCode(0x38, true, false)));

        // Main and numpad keys
        static_cast<void>(context.expectEq("numpad asterisk keeps its scan code", std::uint32_t{0x55}, keyboardUsageForScanCode(0x37, false, false)));
        static_cast<void>(context.expectEq("Space maps to its key usage", std::uint32_t{0x2C}, keyboardUsageForScanCode(0x39, false, false)));
        static_cast<void>(
            context.expectEq("main Enter remains distinct from numpad Enter", std::uint32_t{0x28}, keyboardUsageForScanCode(0x1C, false, false)));
        static_cast<void>(
            context.expectEq("numpad Enter uses its E0-prefixed scan code", std::uint32_t{0x58}, keyboardUsageForScanCode(0x1C, true, false)));
        static_cast<void>(
            context.expectEq("main slash remains distinct from numpad slash", std::uint32_t{0x38}, keyboardUsageForScanCode(0x35, false, false)));
        static_cast<void>(
            context.expectEq("numpad slash uses its E0-prefixed scan code", std::uint32_t{0x54}, keyboardUsageForScanCode(0x35, true, false)));

        // Extended, lock, and function keys
        static_cast<void>(
            context.expectEq("SysRq maps to the Print Screen usage", std::uint32_t{0x46}, keyboardUsageForScanCode(0x54, false, false)));
        static_cast<void>(
            context.expectEq("Non-US backslash keeps its scan code", std::uint32_t{0x64}, keyboardUsageForScanCode(0x56, false, false)));
        static_cast<void>(context.expectEq("Num Lock is not F11", std::uint32_t{0x53}, keyboardUsageForScanCode(0x45, false, false)));
        static_cast<void>(context.expectEq("Scroll Lock is not F12", std::uint32_t{0x47}, keyboardUsageForScanCode(0x46, false, false)));
        static_cast<void>(context.expectEq("F11 maps from scan code 0x57", std::uint32_t{0x44}, keyboardUsageForScanCode(0x57, false, false)));
        static_cast<void>(context.expectEq("F12 maps from scan code 0x58", std::uint32_t{0x45}, keyboardUsageForScanCode(0x58, false, false)));
        static_cast<void>(context.expectEq("F13 maps from scan code 0x64", std::uint32_t{0x68}, keyboardUsageForScanCode(0x64, false, false)));
        static_cast<void>(context.expectEq("F18 maps from scan code 0x69", std::uint32_t{0x6D}, keyboardUsageForScanCode(0x69, false, false)));
        static_cast<void>(context.expectEq("F19 maps from scan code 0x6A", std::uint32_t{0x6E}, keyboardUsageForScanCode(0x6A, false, false)));
        static_cast<void>(context.expectEq("F20 maps from scan code 0x6B", std::uint32_t{0x6F}, keyboardUsageForScanCode(0x6B, false, false)));
        static_cast<void>(context.expectEq("F21 maps from scan code 0x6C", std::uint32_t{0x70}, keyboardUsageForScanCode(0x6C, false, false)));
        static_cast<void>(context.expectEq("F22 maps from scan code 0x6D", std::uint32_t{0x71}, keyboardUsageForScanCode(0x6D, false, false)));
        static_cast<void>(context.expectEq("F23 maps from scan code 0x6E", std::uint32_t{0x72}, keyboardUsageForScanCode(0x6E, false, false)));
        static_cast<void>(context.expectEq("F24 maps from scan code 0x76", std::uint32_t{0x73}, keyboardUsageForScanCode(0x76, false, false)));

        // Pause and Break use distinct native sequences for the same key.
        static_cast<void>(context.expectEq("E1-prefixed Pause maps to Pause", std::uint32_t{0x48}, keyboardUsageForScanCode(0x1D, false, true)));
        static_cast<void>(context.expectEq("E0-prefixed Break maps to Pause", std::uint32_t{0x48}, keyboardUsageForScanCode(0x46, true, false)));
    }

    /// ------------------------------------------------------------
    /// Desktop attachment
    /// ------------------------------------------------------------

    /// @brief Verifies Desktop message forwarding into the Win32 Input attachment.
    void testDesktopAttachment(TestSupport::Context &context)
    {
        Input::System input;
        Desktop::Window window;
        Input::Platform::Win32::Attachment attachment;

        // Open a hidden Desktop window and attach Input to its event stream.
        Desktop::Types::Description description;
        description.title = "Input validation window";
        description.windowed.clientSize = {320, 200};
        description.activation.visible = false;
        description.activation.requestFocus = false;

        if(!context.expectTrue("Win32 test window opens", window.open(description, Desktop::Events::owned(8)).ok()))
        {
            return;
        }

        if(!context.expectTrue("Win32 Input system opens", input.open().ok()))
        {
            return;
        }

        if(!context.expectTrue("Input attaches to Desktop window", Input::Platform::Win32::attachToWindow(input, attachment, window).ok()))
        {
            return;
        }
        static_cast<void>(context.expectTrue("Win32 attachment reports attached", attachment.isAttached()));
        static_cast<void>(context.expectTrue("Win32 attachment exposes a surface", attachment.surfaceId().isValid()));

        // Resolve the native handle and capture the initial device frame.
        const auto nativeHandle = Desktop::Native::Win32::getHandle(window);
        if(!context.expectTrue("Win32 test window exposes a native handle", nativeHandle.ok() && nativeHandle->window != nullptr))
        {
            return;
        }

        Input::Types::Frame frame;
        if(!context.expectTrue("initial Win32 Input capture succeeds", input.capture(frame).status.ok()))
        {
            return;
        }

        // Forward native window messages and capture their normalized values.
        HWND windowHandle = nativeHandle->window;
        static_cast<void>(SendMessageW(windowHandle, WM_MOUSEMOVE, 0, MAKELPARAM(123, 45)));
        static_cast<void>(SendMessageW(windowHandle, WM_CHAR, static_cast<WPARAM>(L'A'), 0));

        const auto messageCapture = input.capture(frame);
        if(!context.expectTrue("Win32 message capture succeeds", messageCapture.status.ok()))
        {
            return;
        }
        static_cast<void>(context.expectEq("Win32 text is forwarded", std::string_view{"A"}, frame.textUtf8()));

        std::span<const Input::Types::DeviceInfo> devices;
        if(!context.expectTrue("Win32 devices are queryable", input.devices(devices).ok()))
        {
            return;
        }
        const auto pointer = Input::Query::findStandard(devices, Input::Standard::Pointer::Position);
        if(!pointer.has_value())
        {
            static_cast<void>(context.expectTrue("window pointer is discoverable", false));
            return;
        }
        static_cast<void>(context.expectTrue("window pointer is discoverable", true));

        const auto *position = std::get_if<Input::Types::Vector2I64>(frame.value(pointer.value()));
        static_cast<void>(context.expectTrue("window pointer position is forwarded", position != nullptr && position->x == 123 && position->y == 45));

        // Detach the event sink before closing its owners.
        static_cast<void>(context.expectTrue("Desktop message sink detaches", Desktop::Native::Win32::detachMessageSink(window).ok()));
        static_cast<void>(context.expectTrue("Win32 Input attachment detaches", attachment.detach().ok()));
        static_cast<void>(context.expectTrue("Win32 Input system closes", input.close().ok()));
        static_cast<void>(context.expectTrue("Win32 test window closes", window.close().ok()));
    }
} // namespace

namespace GameWIP::Test::InputPlatform
{
    /// ------------------------------------------------------------
    /// Input validation suites
    /// ------------------------------------------------------------

    void registerSuites(TestSupport::Runner &runner)
    {
        runner.runSuite("Win32 keyboard scan-code mapping", testKeyboardMapping);
        runner.runSuite("Win32 Desktop attachment", testDesktopAttachment);
    }
} // namespace GameWIP::Test::InputPlatform
