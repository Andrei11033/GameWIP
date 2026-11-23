/// @file input_test.cpp
/// @brief Deterministic portable Input behavior tests.

#include "validation/tests/input/input_test.h"
#include "validation/tests/input/platform.h"

#include "input/input.h"
#include "input/synthetic.h"
#include "test_support/test_support.h"
#include "validation/tests/internal/helpers/runner_helpers.h"

#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

namespace
{
    namespace Input = GameWIP::Input;
    namespace TestSupport = GameWIP::TestSupport;

    /// ------------------------------------------------------------
    /// Portable session and synthetic input
    /// ------------------------------------------------------------

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
        const Input::Types::SyntheticDeviceDescription description{
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

        // Add the device and resolve its standard and persistent controls.
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

        // Publish button transitions and accumulate relative movement.
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

        // Reject value types and update modes that do not match each element.
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

        // Capture committed text and repeat events without a new press.
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

        // Invalidate the button, then remove its device and source.
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

} // namespace

namespace GameWIP::Test
{
    int runInputTests(const InputTestOptions &options)
    {
        const TestSupport::Types::Reporting::Options reportOptions = RunnerHelpers::makeReportingOptions(options);

        TestSupport::Runner runner(reportOptions);
        runner.runSuite("Input system lifecycle", testSystemLifecycle);
        runner.runSuite("Synthetic input source", testSyntheticSource);
        InputPlatform::registerSuites(runner);

        return RunnerHelpers::finalizeRunner(runner);
    }
} // namespace GameWIP::Test
