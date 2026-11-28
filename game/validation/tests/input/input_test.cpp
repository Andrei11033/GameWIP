/// @file input_test.cpp
/// @brief Deterministic portable Input behavior tests.

#include "validation/tests/input/input_test.h"
#include "validation/tests/input/platform.h"

#include "input/input.h"
#include "input/synthetic.h"
#include "test_support/test_support.h"
#include "validation/tests/internal/helpers/runner_helpers.h"

#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

namespace
{
    namespace Input = GameWIP::Input;
    namespace Status = GameWIP::Status::Types;
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
        static_cast<void>(context.expectEq("capture on closed system reports NotOpen", Status::Code::NotOpen, input.capture(frame).status().code()));

        static_cast<void>(context.expectTrue("Input system opens", input.open().ok()));
        static_cast<void>(context.expectTrue("opened Input system reports open", input.isOpen()));
        static_cast<void>(context.expectEq("second open reports AlreadyOpen", Status::Code::AlreadyOpen, input.open().status().code()));
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
        const auto addDeviceResult = source.addDevice(description);
        if(!context.expectTrue("synthetic device is added", addDeviceResult.ok()))
        {
            return;
        }
        const Input::Types::DeviceId device = addDeviceResult.value();

        const auto devicesResult = input.devices();
        if(!context.expectTrue("synthetic devices are queryable", devicesResult.ok()))
        {
            return;
        }
        const std::span<const Input::Types::DeviceInfo> devices = devicesResult.value();
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
        if(!context.expectTrue("initial synthetic capture succeeds", connectedCapture.ok()))
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
        if(!context.expectTrue("pressed synthetic capture succeeds", pressedCapture.ok()))
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
        if(!context.expectTrue("released synthetic capture succeeds", releasedCapture.ok()))
        {
            return;
        }
        static_cast<void>(context.expectTrue("button is released", !frame.isDown(buttonElement)));
        static_cast<void>(context.expectTrue("button was released", frame.wasReleased(buttonElement)));
        static_cast<void>(context.expectTrue("button release is a generic value change", frame.wasChanged(buttonElement)));
        static_cast<void>(context.expectEq("button records one generic value change", std::uint32_t{1}, frame.changeCount(buttonElement)));
        static_cast<void>(context.expectFalse("boolean button has no numeric component change", frame.wasChanged({buttonElement, 0})));
        static_cast<void>(context.expectFalse("boolean button has no numeric component delta", frame.componentDelta({buttonElement, 0}).has_value()));

        axisValue = std::get_if<float>(frame.value(axis));
        static_cast<void>(context.expectTrue("relative value resets for the next frame", axisValue != nullptr && *axisValue == -1.5F));

        // Reject value types and update modes that do not match each element.
        static_cast<void>(context.expectEq(
            "wrong absolute value type is rejected",
            Status::Code::InvalidArgument,
            source.setAbsoluteValue(buttonElement, Input::Types::Value{1.0F}).status().code()));

        static_cast<void>(context.expectEq(
            "relative update on absolute element is rejected",
            Status::Code::InvalidArgument,
            source.addRelativeDelta(buttonElement, Input::Types::Value{1.0F}).status().code()));

        static_cast<void>(context.expectEq(
            "wrong relative value type is rejected",
            Status::Code::InvalidArgument,
            source.addRelativeDelta(axis, Input::Types::Value{true}).status().code()));

        // Capture committed text and repeat events without a new press.
        static_cast<void>(
            context.expectTrue("synthetic text commit succeeds", source.commitText(Input::Types::SurfaceId{77}, "Synthetic text").ok()));

        const auto textCapture = input.capture(frame);
        if(!context.expectTrue("synthetic text capture succeeds", textCapture.ok()))
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
        if(!context.expectTrue("repeat capture succeeds", repeatCapture.ok()))
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
        static_cast<void>(context.expectFalse("element repeat is not a value change", frame.wasChanged(buttonElement)));
        static_cast<void>(context.expectEq("element repeat does not increment change count", std::uint32_t{0}, frame.changeCount(buttonElement)));

        // Invalidate the button, then remove its device and source.
        static_cast<void>(context.expectTrue("element invalidation succeeds", source.invalidate(buttonElement).ok()));

        const auto invalidationCapture = input.capture(frame);
        if(!context.expectTrue("invalidation capture succeeds", invalidationCapture.ok()))
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
        if(!context.expectTrue("disconnected synthetic capture succeeds", disconnectedCapture.ok()))
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

    /// @brief Verifies frame change summaries for absolute and relative numeric controls.
    void testFrameChangeSummaries(TestSupport::Context &context)
    {
        Input::System input;
        if(!context.expectTrue("summary system opens", input.open().ok()))
        {
            return;
        }

        Input::Synthetic::Source source;
        if(!context.expectTrue("summary source attaches", source.attach(input).ok()))
        {
            return;
        }

        const Input::Types::SyntheticDeviceDescription description{
            .name = "Frame Summary Device",
            .persistentKey = "test:frame-summary-device",
            .elements =
                {
                    {
                        .name = "Absolute Axis",
                        .persistentKey = "absolute-axis",
                        .kind = Input::Types::ValueKind::Float32,
                        .reporting = Input::Types::ReportingMode::Absolute,
                    },
                    {
                        .name = "Absolute Stick",
                        .persistentKey = "absolute-stick",
                        .kind = Input::Types::ValueKind::Vector2F32,
                        .reporting = Input::Types::ReportingMode::Absolute,
                    },
                    {
                        .name = "Relative Motion",
                        .persistentKey = "relative-motion",
                        .kind = Input::Types::ValueKind::Vector2F32,
                        .reporting = Input::Types::ReportingMode::Relative,
                    },
                    {
                        .name = "Spatial Pose",
                        .persistentKey = "spatial-pose",
                        .kind = Input::Types::ValueKind::Spatial,
                    },
                },
        };

        const auto deviceResult = source.addDevice(description);
        if(!context.expectTrue("summary device adds", deviceResult.ok()))
        {
            return;
        }
        const Input::Types::DeviceId device = deviceResult.value();

        const auto devicesResult = input.devices();
        if(!context.expectTrue("summary descriptors query", devicesResult.ok()))
        {
            return;
        }

        const std::span<const Input::Types::DeviceInfo> devices = devicesResult.value();
        const Input::Types::ElementInfo *absoluteInfo = Input::Query::findElementByPersistentKey(devices, "absolute-axis", device);
        const Input::Types::ElementInfo *stickInfo = Input::Query::findElementByPersistentKey(devices, "absolute-stick", device);
        const Input::Types::ElementInfo *relativeInfo = Input::Query::findElementByPersistentKey(devices, "relative-motion", device);
        const Input::Types::ElementInfo *spatialInfo = Input::Query::findElementByPersistentKey(devices, "spatial-pose", device);
        if(absoluteInfo == nullptr || stickInfo == nullptr || relativeInfo == nullptr || spatialInfo == nullptr)
        {
            static_cast<void>(context.expectTrue("summary controls resolve", false));
            return;
        }
        static_cast<void>(context.expectTrue("summary controls resolve", true));

        const Input::Types::ElementId absolute = absoluteInfo->id;
        const Input::Types::ElementId stick = stickInfo->id;
        const Input::Types::ElementId relative = relativeInfo->id;
        const Input::Types::ElementId spatial = spatialInfo->id;
        const Input::Types::ComponentId stickX{stick, 0};
        const Input::Types::ComponentId stickY{stick, 1};
        const Input::Types::ComponentId relativeX{relative, 0};
        Input::Types::Frame frame;

        if(!context.expectTrue("empty summary frame captures", input.capture(frame).ok()))
        {
            return;
        }
        static_cast<void>(context.expectTrue("absolute baseline is initially unknown", !frame.componentDelta({absolute, 0}).has_value()));
        static_cast<void>(context.expectTrue("unknown initial sample is not a change", !frame.wasChanged(absolute)));
        static_cast<void>(context.expectEq("unknown initial sample count is zero", std::uint32_t{0}, frame.changeCount(absolute)));

        // Establish known absolute baselines; the first observations are not changes.
        static_cast<void>(context.expectTrue("absolute axis baseline publishes", source.setAbsoluteValue(absolute, 0.25F).ok()));
        static_cast<void>(context.expectTrue(
            "absolute stick baseline publishes",
            source.setAbsoluteValue(stick, Input::Types::Value{Input::Types::Vector2F32{0.0F, 0.0F}}).ok()));
        static_cast<void>(context.expectTrue(
            "spatial baseline publishes",
            source.setAbsoluteValue(spatial, Input::Types::Value{Input::Types::Spatial{.position = {1.0, 2.0, 3.0}, .positionValid = true}}).ok()));
        if(!context.expectTrue("baseline frame captures", input.capture(frame).ok()))
        {
            return;
        }
        static_cast<void>(context.expectTrue("first absolute sample has no delta", !frame.componentDelta({absolute, 0}).has_value()));
        static_cast<void>(context.expectEq("first absolute sample is not a change", std::uint32_t{0}, frame.changeCount(absolute)));

        // Identical reports keep a known zero delta without incrementing counts.
        static_cast<void>(context.expectTrue("same absolute axis report publishes", source.setAbsoluteValue(absolute, 0.25F).ok()));
        static_cast<void>(context.expectTrue(
            "same absolute stick report publishes",
            source.setAbsoluteValue(stick, Input::Types::Value{Input::Types::Vector2F32{0.0F, 0.0F}}).ok()));
        if(!context.expectTrue("unchanged frame captures", input.capture(frame).ok()))
        {
            return;
        }
        const auto unchangedDelta = frame.componentDelta({absolute, 0});
        const float *unchangedValue = unchangedDelta.has_value() ? std::get_if<float>(&*unchangedDelta) : nullptr;
        static_cast<void>(context.expectTrue("unchanged absolute delta is zero", unchangedValue != nullptr && *unchangedValue == 0.0F));
        static_cast<void>(context.expectFalse("identical reports are not changes", frame.wasChanged(absolute)));
        static_cast<void>(context.expectEq("identical report count is zero", std::uint32_t{0}, frame.changeCount(absolute)));
        const auto spatialDelta = frame.componentDelta({spatial, 0});
        const double *spatialValue = spatialDelta.has_value() ? std::get_if<double>(&*spatialDelta) : nullptr;
        static_cast<void>(context.expectTrue("unchanged spatial component has zero delta", spatialValue != nullptr && *spatialValue == 0.0));

        // Count both observed transitions even when the axis returns to its starting value.
        static_cast<void>(context.expectTrue("absolute axis moves forward", source.setAbsoluteValue(absolute, 0.5F).ok()));
        static_cast<void>(context.expectTrue("absolute axis returns", source.setAbsoluteValue(absolute, 0.25F).ok()));
        if(!context.expectTrue("net-zero absolute frame captures", input.capture(frame).ok()))
        {
            return;
        }
        const auto netZeroDelta = frame.componentDelta({absolute, 0});
        const float *netZeroValue = netZeroDelta.has_value() ? std::get_if<float>(&*netZeroDelta) : nullptr;
        static_cast<void>(context.expectTrue("absolute net delta is zero", netZeroValue != nullptr && *netZeroValue == 0.0F));
        static_cast<void>(context.expectTrue("absolute axis changed despite net zero", frame.wasChanged(absolute)));
        static_cast<void>(context.expectEq("absolute axis records both changes", std::uint32_t{2}, frame.changeCount(absolute)));
        static_cast<void>(context.expectEq("absolute component records both changes", std::uint32_t{2}, frame.changeCount({absolute, 0})));

        // Track vector components independently while counting element-level reports.
        static_cast<void>(
            context.expectTrue("stick X changes", source.setAbsoluteValue(stick, Input::Types::Value{Input::Types::Vector2F32{1.0F, 0.0F}}).ok()));
        static_cast<void>(
            context.expectTrue("stick Y changes", source.setAbsoluteValue(stick, Input::Types::Value{Input::Types::Vector2F32{1.0F, 2.0F}}).ok()));
        if(!context.expectTrue("vector change frame captures", input.capture(frame).ok()))
        {
            return;
        }
        const auto stickXDelta = frame.componentDelta(stickX);
        const auto stickYDelta = frame.componentDelta(stickY);
        const float *stickXValue = stickXDelta.has_value() ? std::get_if<float>(&*stickXDelta) : nullptr;
        const float *stickYValue = stickYDelta.has_value() ? std::get_if<float>(&*stickYDelta) : nullptr;
        static_cast<void>(context.expectTrue("stick X delta is one", stickXValue != nullptr && *stickXValue == 1.0F));
        static_cast<void>(context.expectTrue("stick Y delta is two", stickYValue != nullptr && *stickYValue == 2.0F));
        static_cast<void>(context.expectEq("stick element records both updates", std::uint32_t{2}, frame.changeCount(stick)));
        static_cast<void>(context.expectEq("stick X records one change", std::uint32_t{1}, frame.changeCount(stickX)));
        static_cast<void>(context.expectEq("stick Y records one change", std::uint32_t{1}, frame.changeCount(stickY)));

        // Relative reports remain deltas and count nonzero movement reports.
        static_cast<void>(context.expectTrue(
            "relative motion advances",
            source.addRelativeDelta(relative, Input::Types::Value{Input::Types::Vector2F32{4.0F, 0.0F}}).ok()));
        static_cast<void>(context.expectTrue(
            "relative motion returns",
            source.addRelativeDelta(relative, Input::Types::Value{Input::Types::Vector2F32{-4.0F, 0.0F}}).ok()));
        if(!context.expectTrue("relative change frame captures", input.capture(frame).ok()))
        {
            return;
        }
        const Input::Types::ElementState *relativeState = frame.state(relative);
        static_cast<void>(context.expectTrue(
            "frame retains relative reporting mode",
            relativeState != nullptr && relativeState->reporting == Input::Types::ReportingMode::Relative));
        const auto relativeDelta = frame.componentDelta(relativeX);
        const float *relativeValue = relativeDelta.has_value() ? std::get_if<float>(&*relativeDelta) : nullptr;
        static_cast<void>(context.expectTrue("relative net delta is zero", relativeValue != nullptr && *relativeValue == 0.0F));
        static_cast<void>(context.expectTrue("relative control changed despite net zero", frame.wasChanged(relativeX)));
        static_cast<void>(context.expectEq("relative control records both reports", std::uint32_t{2}, frame.changeCount(relativeX)));
        static_cast<void>(context.expectEq("relative element records both reports", std::uint32_t{2}, frame.changeCount(relative)));

        static_cast<void>(context.expectTrue(
            "relative motion publishes in the next interval",
            source.addRelativeDelta(relative, Input::Types::Value{Input::Types::Vector2F32{2.0F, 0.0F}}).ok()));
        if(!context.expectTrue("next relative frame captures", input.capture(frame).ok()))
        {
            return;
        }
        const auto nextRelativeDelta = frame.componentDelta(relativeX);
        const float *nextRelativeValue = nextRelativeDelta.has_value() ? std::get_if<float>(&*nextRelativeDelta) : nullptr;
        static_cast<void>(context.expectTrue("relative delta is frame-local", nextRelativeValue != nullptr && *nextRelativeValue == 2.0F));
        static_cast<void>(context.expectTrue("relative control changes in the next frame", frame.wasChanged(relativeX)));
        static_cast<void>(context.expectEq("relative next-frame count is one", std::uint32_t{1}, frame.changeCount(relativeX)));

        static_cast<void>(context.expectTrue("empty relative frame captures", input.capture(frame).ok()));
        const auto emptyRelativeDelta = frame.componentDelta(relativeX);
        const float *emptyRelativeValue = emptyRelativeDelta.has_value() ? std::get_if<float>(&*emptyRelativeDelta) : nullptr;
        static_cast<void>(context.expectTrue("relative delta resets to zero", emptyRelativeValue != nullptr && *emptyRelativeValue == 0.0F));
        static_cast<void>(context.expectFalse("relative control is unchanged in an empty frame", frame.wasChanged(relativeX)));
        static_cast<void>(context.expectEq("empty relative-frame count is zero", std::uint32_t{0}, frame.changeCount(relativeX)));

        static_cast<void>(context.expectTrue("summary source detaches", source.detach().ok()));
        static_cast<void>(context.expectTrue("summary system closes", input.close().ok()));
    }

    /// @brief Verifies observed summaries remain available when an interval is discontinuous.
    void testDiscontinuousFrameChangeSummaries(TestSupport::Context &context)
    {
        Input::System input;
        const Input::Types::Configuration configuration{.eventCapacity = 1};
        if(!context.expectTrue("discontinuous summary system opens", input.open(configuration).ok()))
        {
            return;
        }

        Input::Synthetic::Source source;
        if(!context.expectTrue("discontinuous summary source attaches", source.attach(input).ok()))
        {
            return;
        }

        const Input::Types::SyntheticDeviceDescription description{
            .name = "Discontinuous Summary Device",
            .persistentKey = "test:discontinuous-summary-device",
            .elements = {{
                .name = "Absolute Axis",
                .persistentKey = "absolute-axis",
                .kind = Input::Types::ValueKind::Float32,
                .reporting = Input::Types::ReportingMode::Absolute,
            }},
        };
        const auto deviceResult = source.addDevice(description);
        if(!context.expectTrue("discontinuous summary device adds", deviceResult.ok()))
        {
            return;
        }

        const auto devicesResult = input.devices();
        if(!context.expectTrue("discontinuous summary descriptors query", devicesResult.ok()))
        {
            return;
        }
        const Input::Types::ElementInfo *axis =
            Input::Query::findElementByPersistentKey(devicesResult.value(), "absolute-axis", deviceResult.value());
        if(!context.expectTrue("discontinuous summary axis resolves", axis != nullptr))
        {
            return;
        }

        Input::Types::Frame frame;
        if(!context.expectTrue("discontinuous summary starts with a capture", input.capture(frame).ok()))
        {
            return;
        }
        static_cast<void>(context.expectTrue("discontinuous axis baseline publishes", source.setAbsoluteValue(axis->id, 0.1F).ok()));
        if(!context.expectTrue("discontinuous summary baseline captures", input.capture(frame).ok()))
        {
            return;
        }

        static_cast<void>(context.expectTrue("first observed change publishes", source.setAbsoluteValue(axis->id, 0.2F).ok()));
        static_cast<void>(context.expectTrue("dropped change updates state", source.setAbsoluteValue(axis->id, 0.3F).ok()));
        if(!context.expectTrue("discontinuous summary captures", input.capture(frame).ok()))
        {
            return;
        }

        static_cast<void>(context.expectTrue("event loss marks the frame discontinuous", frame.discontinuity()));
        static_cast<void>(context.expectEq("only the observed change is counted", std::uint32_t{1}, frame.changeCount({axis->id, 0})));
        const auto delta = frame.componentDelta({axis->id, 0});
        const float *deltaValue = delta.has_value() ? std::get_if<float>(&*delta) : nullptr;
        static_cast<void>(
            context.expectTrue("observed absolute delta remains available", deltaValue != nullptr && std::fabs(*deltaValue - 0.2F) < 0.000001F));

        static_cast<void>(context.expectTrue("discontinuous source detaches", source.detach().ok()));
        static_cast<void>(context.expectTrue("discontinuous system closes", input.close().ok()));
    }

} // namespace

namespace GameWIP::Test
{
    int runInputTests(const InputTestOptions &options)
    {
        const TestSupport::Types::Reporting::Options reportOptions = Internal::RunnerHelpers::makeReportingOptions(options);

        TestSupport::Runner runner(reportOptions);
        runner.runSuite("Input system lifecycle", testSystemLifecycle);
        runner.runSuite("Synthetic input source", testSyntheticSource);
        runner.runSuite("Input frame change summaries", testFrameChangeSummaries);
        runner.runSuite("Discontinuous frame change summaries", testDiscontinuousFrameChangeSummaries);
        InputPlatform::registerSuites(runner);

        return Internal::RunnerHelpers::finalizeRunner(runner);
    }
} // namespace GameWIP::Test
