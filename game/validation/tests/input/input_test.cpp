/// @file input_test.cpp
/// @brief Deterministic portable Input behavior and Win32 Desktop attachment tests.

#include "validation/tests/input/input_test.h"

#include "desktop/events.h"
#include "desktop/window.h"
#include "input/input.h"
#include "input/synthetic.h"
#include "test_support/test_support.h"
#include "validation/tests/runner_helpers.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "desktop/native/win32.h"
#include "input/native/desktop_win32.h"
#include "input/platform/win32/internal/win32_keyboard_mapping.h"

#include <windows.h>
#endif

#include <cstdint>
#include <string_view>
#include <variant>

namespace
{
    namespace Input = GameWIP::Input;
    namespace Desktop = GameWIP::Desktop;
    namespace TestSupport = GameWIP::TestSupport;

    /// @brief Verifies the owner-thread-affine System lifecycle and closed-state contract.
    void testSystemLifecycle(TestSupport::Context &context)
    {
        Input::System input;
        Input::Types::Frame frame;

        static_cast<void>(context.expectFalse("new Input system is closed", input.isOpen()));
        static_cast<void>(
            context.expectEq("capture on closed system reports NotOpen", GameWIP::IO::Types::ErrorCode::NotOpen, input.capture(frame).status.code));

        static_cast<void>(context.expectTrue("Input system opens", input.open().ok()));
        static_cast<void>(context.expectTrue("opened Input system reports open", input.isOpen()));
        static_cast<void>(context.expectEq("second open reports AlreadyOpen", GameWIP::IO::Types::ErrorCode::AlreadyOpen, input.open().code));
        static_cast<void>(context.expectTrue("Input system closes", input.close().ok()));
    }

    /// @brief Verifies synthetic discovery, values, transitions, text, and removal.
    void testSyntheticSource(TestSupport::Context &context)
    {
        Input::System input;
        if(!context.expectTrue("synthetic system opens", input.open().ok()))
        {
            return;
        }
        Input::Synthetic::Source source;
        if(!context.expectTrue("synthetic source attaches", source.attach(input).ok()))
        {
            return;
        }
        Input::Types::SyntheticDeviceDescription description{
            .name = "Synthetic Test Device",
            .persistentKey = "test:synthetic-device",
            .elements =
                {
                    {
                        .name = "Test Button",
                        .persistentKey = "button",
                        .identity = {.standard = Input::Standard::Key::A},
                        .kind = Input::Types::ValueKind::Boolean,
                        .reporting = Input::Types::ReportingMode::Absolute,
                    },
                    {
                        .name = "Test Axis",
                        .persistentKey = "axis",
                        .kind = Input::Types::ValueKind::Float32,
                        .reporting = Input::Types::ReportingMode::Relative,
                    },
                },
        };

        Input::Types::DeviceId device{};
        if(!context.expectTrue("synthetic device is added", source.addDevice(description, device).ok()))
        {
            return;
        }

        std::span<const Input::Types::DeviceInfo> devices;
        if(!context.expectTrue("synthetic devices are queryable", input.devices(devices).ok()))
        {
            return;
        }
        const Input::Types::DeviceInfo *deviceInfo = Input::Query::findDevice(devices, device);
        if(!context.expectTrue("synthetic device is discoverable", deviceInfo != nullptr))
        {
            return;
        }
        static_cast<void>(context.expectEq("synthetic device has virtual origin", Input::Types::DeviceOrigin::Virtual, deviceInfo->origin));
        static_cast<void>(context.expectEq("synthetic device exposes both elements", std::size_t{2}, deviceInfo->elements.size()));

        const auto button = Input::Query::findStandard(devices, Input::Standard::Key::A, device);
        if(!button.has_value())
        {
            static_cast<void>(context.expectTrue("standard button lookup resolves", false));
            return;
        }
        static_cast<void>(context.expectTrue("standard button lookup resolves", true));

        const Input::Types::ElementInfo *axisInfo = Input::Query::findElementByPersistentKey(devices, "axis", device);
        if(!context.expectTrue("persistent axis lookup resolves", axisInfo != nullptr))
        {
            return;
        }
        const Input::Types::ElementId axis = axisInfo->id;

        Input::Types::Frame frame;
        const auto connectedCapture = input.capture(frame);
        if(!context.expectTrue("initial synthetic capture succeeds", connectedCapture.status.ok()))
        {
            return;
        }

        bool connectedEvent = false;
        for(const Input::Types::Event &event : frame.events())
        {
            if(event.kind != Input::Types::EventKind::DeviceConnected)
            {
                continue;
            }

            const auto *deviceEvent = std::get_if<Input::Types::DeviceEvent>(&event.data);
            connectedEvent = deviceEvent != nullptr && deviceEvent->id == device;
        }

        static_cast<void>(context.expectTrue("device connection event is published", connectedEvent));

        const Input::Types::ElementId buttonElement = button.value();
        static_cast<void>(context.expectTrue("button value publishes", source.setAbsoluteValue(buttonElement, Input::Types::Value{true}).ok()));
        static_cast<void>(context.expectTrue("first relative delta publishes", source.addRelativeDelta(axis, Input::Types::Value{1.25F}).ok()));
        static_cast<void>(context.expectTrue("second relative delta publishes", source.addRelativeDelta(axis, Input::Types::Value{2.75F}).ok()));

        const auto pressedCapture = input.capture(frame);
        if(!context.expectTrue("pressed synthetic capture succeeds", pressedCapture.status.ok()))
        {
            return;
        }
        static_cast<void>(context.expectTrue("button is down", frame.isDown(buttonElement)));
        static_cast<void>(context.expectTrue("button was pressed", frame.wasPressed(buttonElement)));
        static_cast<void>(context.expectEq("button press count is one", std::uint32_t{1}, frame.pressCount(buttonElement)));

        const float *axisValue = std::get_if<float>(frame.value(axis));
        static_cast<void>(context.expectTrue("relative deltas accumulate", axisValue != nullptr && *axisValue == 4.0F));

        static_cast<void>(context.expectTrue("button release publishes", source.setAbsoluteValue(buttonElement, Input::Types::Value{false}).ok()));
        static_cast<void>(context.expectTrue("negative relative delta publishes", source.addRelativeDelta(axis, Input::Types::Value{-1.5F}).ok()));

        const auto releasedCapture = input.capture(frame);
        if(!context.expectTrue("released synthetic capture succeeds", releasedCapture.status.ok()))
        {
            return;
        }
        static_cast<void>(context.expectTrue("button is released", !frame.isDown(buttonElement)));
        static_cast<void>(context.expectTrue("button was released", frame.wasReleased(buttonElement)));

        axisValue = std::get_if<float>(frame.value(axis));
        static_cast<void>(context.expectTrue("relative value resets for the next frame", axisValue != nullptr && *axisValue == -1.5F));

        static_cast<void>(context.expectEq(
            "wrong absolute value type is rejected",
            GameWIP::IO::Types::ErrorCode::InvalidArgument,
            source.setAbsoluteValue(buttonElement, Input::Types::Value{1.0F}).code));

        static_cast<void>(context.expectEq(
            "relative update on absolute element is rejected",
            GameWIP::IO::Types::ErrorCode::InvalidArgument,
            source.addRelativeDelta(buttonElement, Input::Types::Value{1.0F}).code));

        static_cast<void>(context.expectEq(
            "wrong relative value type is rejected",
            GameWIP::IO::Types::ErrorCode::InvalidArgument,
            source.addRelativeDelta(axis, Input::Types::Value{true}).code));

        static_cast<void>(
            context.expectTrue("synthetic text commit succeeds", source.commitText(Input::Types::SurfaceId{77}, "Synthetic text").ok()));

        const auto textCapture = input.capture(frame);
        if(!context.expectTrue("synthetic text capture succeeds", textCapture.status.ok()))
        {
            return;
        }
        static_cast<void>(context.expectEq("synthetic text is retained", std::string_view{"Synthetic text"}, frame.textUtf8()));

        bool textEvent = false;
        for(const Input::Types::Event &event : frame.events())
        {
            if(event.kind != Input::Types::EventKind::TextCommitted)
            {
                continue;
            }

            const auto *text = std::get_if<Input::Types::TextEvent>(&event.data);
            textEvent = text != nullptr && text->surface == Input::Types::SurfaceId{77} && text->utf8 == "Synthetic text";
        }

        static_cast<void>(context.expectTrue("synthetic text event is published", textEvent));

        static_cast<void>(context.expectTrue("element repeat succeeds", source.repeat(buttonElement).ok()));

        const auto repeatCapture = input.capture(frame);
        if(!context.expectTrue("repeat capture succeeds", repeatCapture.status.ok()))
        {
            return;
        }
        static_cast<void>(context.expectEq("repeat does not create a press transition", std::uint32_t{0}, frame.pressCount(buttonElement)));
        static_cast<void>(context.expectEq("repeat does not create a release transition", std::uint32_t{0}, frame.releaseCount(buttonElement)));

        bool repeatedEvent = false;
        for(const Input::Types::Event &event : frame.events())
        {
            if(event.kind != Input::Types::EventKind::ElementRepeated)
            {
                continue;
            }

            const auto *element = std::get_if<Input::Types::ElementEvent>(&event.data);
            repeatedEvent = element != nullptr && element->id == buttonElement;
        }

        static_cast<void>(context.expectTrue("element repeat event is published", repeatedEvent));

        static_cast<void>(context.expectTrue("element invalidation succeeds", source.invalidate(buttonElement).ok()));

        const auto invalidationCapture = input.capture(frame);
        if(!context.expectTrue("invalidation capture succeeds", invalidationCapture.status.ok()))
        {
            return;
        }

        const Input::Types::ElementState *invalidated = frame.state(buttonElement);
        static_cast<void>(context.expectTrue(
            "invalidated element becomes unavailable",
            invalidated != nullptr && invalidated->validity == Input::Types::ValueValidity::Unavailable));

        bool invalidatedEvent = false;
        for(const Input::Types::Event &event : frame.events())
        {
            if(event.kind != Input::Types::EventKind::ElementInvalidated)
            {
                continue;
            }

            const auto *element = std::get_if<Input::Types::ElementEvent>(&event.data);
            invalidatedEvent = element != nullptr && element->id == buttonElement;
        }

        static_cast<void>(context.expectTrue("element invalidation event is published", invalidatedEvent));

        static_cast<void>(context.expectTrue("synthetic device removal succeeds", source.removeDevice(device).ok()));

        const auto disconnectedCapture = input.capture(frame);
        if(!context.expectTrue("disconnected synthetic capture succeeds", disconnectedCapture.status.ok()))
        {
            return;
        }

        bool disconnectedEvent = false;
        for(const Input::Types::Event &event : frame.events())
        {
            if(event.kind != Input::Types::EventKind::DeviceDisconnected)
            {
                continue;
            }

            const auto *deviceEvent = std::get_if<Input::Types::DeviceEvent>(&event.data);
            disconnectedEvent = deviceEvent != nullptr && deviceEvent->id == device;
        }

        static_cast<void>(context.expectTrue("device disconnection event is published", disconnectedEvent));
        static_cast<void>(context.expectTrue("synthetic source detaches", source.detach().ok()));
        static_cast<void>(context.expectTrue("synthetic system closes", input.close().ok()));
    }

#if defined(_WIN32)
    /// @brief Verifies standard and extended Win32 keyboard scan-code mappings.
    void testWin32KeyboardMapping(TestSupport::Context &context)
    {
        using Input::Platform::Win32::Detail::keyboardUsageForScanCode;

        static_cast<void>(context.expectEq("left Shift maps to HID Left Shift", std::uint32_t{0xE1}, keyboardUsageForScanCode(0x2A, false, false)));
        static_cast<void>(context.expectEq("right Shift maps to HID Right Shift", std::uint32_t{0xE5}, keyboardUsageForScanCode(0x36, false, false)));
        static_cast<void>(context.expectEq("numpad asterisk keeps its scan code", std::uint32_t{0x55}, keyboardUsageForScanCode(0x37, false, false)));
        static_cast<void>(context.expectEq("left Alt maps to HID Left Alt", std::uint32_t{0xE2}, keyboardUsageForScanCode(0x38, false, false)));
        static_cast<void>(
            context.expectEq("right Alt uses its E0-prefixed scan code", std::uint32_t{0xE6}, keyboardUsageForScanCode(0x38, true, false)));
        static_cast<void>(context.expectEq("Space maps to its key usage", std::uint32_t{0x2C}, keyboardUsageForScanCode(0x39, false, false)));
        static_cast<void>(
            context.expectEq("main Enter remains distinct from numpad Enter", std::uint32_t{0x28}, keyboardUsageForScanCode(0x1C, false, false)));
        static_cast<void>(
            context.expectEq("numpad Enter uses its E0-prefixed scan code", std::uint32_t{0x58}, keyboardUsageForScanCode(0x1C, true, false)));
        static_cast<void>(
            context.expectEq("main slash remains distinct from numpad slash", std::uint32_t{0x38}, keyboardUsageForScanCode(0x35, false, false)));
        static_cast<void>(
            context.expectEq("numpad slash uses its E0-prefixed scan code", std::uint32_t{0x54}, keyboardUsageForScanCode(0x35, true, false)));
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
        static_cast<void>(context.expectEq("E1-prefixed Pause maps to Pause", std::uint32_t{0x48}, keyboardUsageForScanCode(0x1D, false, true)));
        static_cast<void>(context.expectEq("E0-prefixed Break maps to Pause", std::uint32_t{0x48}, keyboardUsageForScanCode(0x46, true, false)));
    }

    /// @brief Verifies Desktop message forwarding into the Win32 Input attachment.
    void testWin32DesktopAttachment(TestSupport::Context &context)
    {
        Input::System input;
        Desktop::Window window;
        Input::Platform::Win32::Attachment attachment;

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

        static_cast<void>(context.expectTrue("Desktop message sink detaches", Desktop::Native::Win32::detachMessageSink(window).ok()));
        static_cast<void>(context.expectTrue("Win32 Input attachment detaches", attachment.detach().ok()));
        static_cast<void>(context.expectTrue("Win32 Input system closes", input.close().ok()));
        static_cast<void>(context.expectTrue("Win32 test window closes", window.close().ok()));
    }
#endif
} // namespace

namespace GameWIP::Test
{
    int runInputTests(const InputTestOptions &options)
    {
        TestSupport::Types::Reporting::Options reportOptions = RunnerHelpers::makeReportingOptions(options);

        TestSupport::Runner runner(reportOptions);
        runner.runSuite("Input system lifecycle", testSystemLifecycle);
        runner.runSuite("Synthetic input source", testSyntheticSource);
#if defined(_WIN32)
        runner.runSuite("Win32 keyboard scan-code mapping", testWin32KeyboardMapping);
        runner.runSuite("Win32 Desktop attachment", testWin32DesktopAttachment);
#endif

        return RunnerHelpers::finalizeRunner(runner);
    }
} // namespace GameWIP::Test
