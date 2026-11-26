/// @file input_events.cpp
/// @brief Reports Input device and control events at Debug and Trace.

#include "runtime_diagnostics.h"

#include "input_control_events.h"
#include "input_event_format.h"
#include "input_reports.h"
#include "logger/logging.h"
#include "logger/message_format.h"

#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace
{
    namespace Logger = GameWIP::Logger;
    namespace Types = GameWIP::Input::Types;
    namespace Format = GameWIP::Game::RuntimeDiagnostics::InputEventFormat;
    using LogArea = GameWIP::Game::Logging::Area;

    /// ------------------------------------------------------------
    /// Device lookup
    /// ------------------------------------------------------------

    [[nodiscard]] const Types::DeviceInfo *findDevice(const GameWIP::Input::System &input, Types::DeviceId id)
    {
        const auto deviceResult = input.findDevice(id);
        if(!deviceResult.ok())
        {
            return nullptr;
        }
        return deviceResult.value();
    }

    /// ------------------------------------------------------------
    /// Device connection events
    /// ------------------------------------------------------------

    void logInputDeviceChange(const GameWIP::Input::System &input, const Types::Event &event)
    {
        // Ignore non-device events before resolving the descriptor.
        const Types::DeviceEvent *deviceEvent = std::get_if<Types::DeviceEvent>(&event.data);
        if(deviceEvent == nullptr)
        {
            return;
        }

        const Types::DeviceInfo *device = findDevice(input, deviceEvent->id);
        Logger::Record record = Logger::record();
        record.field("event_sequence", event.sequence).field("cause", Format::eventCause(event.cause));
        if(device == nullptr)
        {
            record.field("device_slot", deviceEvent->id.slot);
            switch(event.kind)
            {
            case Types::EventKind::DeviceConnected:
                Logger::info(LogArea::Input, record, "Device #{} connected; descriptor unavailable", Logger::number(deviceEvent->id.slot));
                break;
            case Types::EventKind::DeviceDisconnected:
                Logger::info(LogArea::Input, record, "Device #{} disconnected; descriptor unavailable", Logger::number(deviceEvent->id.slot));
                break;
            case Types::EventKind::DeviceChanged:
                Logger::debug(LogArea::Input, record, "Device #{} updated; descriptor unavailable", Logger::number(deviceEvent->id.slot));
                break;
            case Types::EventKind::ElementChanged:
            case Types::EventKind::ElementRepeated:
            case Types::EventKind::ElementInvalidated:
            case Types::EventKind::TextCommitted:
            case Types::EventKind::CompositionStarted:
            case Types::EventKind::CompositionUpdated:
            case Types::EventKind::CompositionFinished:
                break;
            }
            return;
        }

        // Prepare shared labels and identity fields for the event report.
        const std::string_view name = device->name.empty() ? std::string_view{"<unnamed>"} : std::string_view{device->name};
        const std::string_view manufacturer = device->manufacturer;
        const std::string_view kind = Format::deviceKind(*device);
        const std::size_t standardControls = Format::standardControlCount(*device);
        const bool hasManufacturer = !manufacturer.empty() && manufacturer != name;
        record.field("device_system_instance", device->id.instance)
            .field("device_slot", device->id.slot)
            .field("device_generation", device->id.generation);

        switch(event.kind)
        {
        case Types::EventKind::DeviceConnected:
            if(hasManufacturer)
            {
                Logger::info(
                    LogArea::Input,
                    record,
                    "{} #{} connected: {} by {} ({} controls, {} standard)",
                    Logger::code(kind),
                    Logger::number(device->id.slot),
                    Logger::quoted(name),
                    Logger::quoted(manufacturer),
                    Logger::number(device->elements.size()),
                    Logger::number(standardControls));
            }
            else
            {
                Logger::info(
                    LogArea::Input,
                    record,
                    "{} #{} connected: {} ({} controls, {} standard)",
                    Logger::code(kind),
                    Logger::number(device->id.slot),
                    Logger::quoted(name),
                    Logger::number(device->elements.size()),
                    Logger::number(standardControls));
            }
            break;
        case Types::EventKind::DeviceDisconnected:
            if(hasManufacturer)
            {
                Logger::info(
                    LogArea::Input,
                    record,
                    "{} #{} disconnected: {} by {}",
                    Logger::code(kind),
                    Logger::number(device->id.slot),
                    Logger::quoted(name),
                    Logger::quoted(manufacturer));
            }
            else
            {
                Logger::info(
                    LogArea::Input,
                    record,
                    "{} #{} disconnected: {}",
                    Logger::code(kind),
                    Logger::number(device->id.slot),
                    Logger::quoted(name));
            }
            break;
        case Types::EventKind::DeviceChanged:
            if(hasManufacturer)
            {
                Logger::debug(
                    LogArea::Input,
                    record,
                    "{} #{} updated: {} by {} ({} controls, {} standard)",
                    Logger::code(kind),
                    Logger::number(device->id.slot),
                    Logger::quoted(name),
                    Logger::quoted(manufacturer),
                    Logger::number(device->elements.size()),
                    Logger::number(standardControls));
            }
            else
            {
                Logger::debug(
                    LogArea::Input,
                    record,
                    "{} #{} updated: {} ({} controls, {} standard)",
                    Logger::code(kind),
                    Logger::number(device->id.slot),
                    Logger::quoted(name),
                    Logger::number(device->elements.size()),
                    Logger::number(standardControls));
            }
            break;
        case Types::EventKind::ElementChanged:
        case Types::EventKind::ElementRepeated:
        case Types::EventKind::ElementInvalidated:
        case Types::EventKind::TextCommitted:
        case Types::EventKind::CompositionStarted:
        case Types::EventKind::CompositionUpdated:
        case Types::EventKind::CompositionFinished:
            break;
        }
    }

    void logInputDeviceEventTrace(const GameWIP::Input::System &input, const Types::Event &event, std::uint64_t captureSequence)
    {
        const Types::DeviceEvent *deviceEvent = std::get_if<Types::DeviceEvent>(&event.data);
        if(deviceEvent == nullptr)
        {
            return;
        }

        const Types::DeviceInfo *device = findDevice(input, deviceEvent->id);
        GameWIP::Game::RuntimeDiagnostics::logInputDeviceTrace(device, deviceEvent->id, &event, captureSequence);
    }

} // namespace

namespace GameWIP::Game::RuntimeDiagnostics
{
    /// ------------------------------------------------------------
    /// Captured frame reporting
    /// ------------------------------------------------------------

    void logInputEvents(const Input::System &input, const Input::Types::Frame &frame, bool includeTraceDetails, bool includeControlEvents)
    {
        // Avoid descriptor and raw-report queries for an empty frame.
        const std::span<const Input::Types::Event> events = frame.events();
        if(events.empty() && frame.rawReports().empty() && frame.droppedEvents() == 0 && !frame.discontinuity())
        {
            return;
        }

        const bool logDebug = Logger::shouldLog(Logger::Types::Level::Debug, LogArea::Input);
        const bool logTrace = includeControlEvents && Logger::shouldLog(Logger::Types::Level::Trace, LogArea::Input);
        const bool logDeviceTrace = includeTraceDetails && Logger::shouldLog(Logger::Types::Level::Trace, LogArea::Input);
        // Route device and control events to their selected reporting levels.
        for(const Input::Types::Event &event : events)
        {
            switch(event.kind)
            {
            case Input::Types::EventKind::DeviceConnected:
            case Input::Types::EventKind::DeviceDisconnected:
                if(logDeviceTrace)
                {
                    logInputDeviceEventTrace(input, event, frame.sequence());
                }
                else
                {
                    logInputDeviceChange(input, event);
                }
                break;
            case Input::Types::EventKind::DeviceChanged:
                if(logDeviceTrace)
                {
                    logInputDeviceEventTrace(input, event, frame.sequence());
                }
                else if(logDebug)
                {
                    logInputDeviceChange(input, event);
                }
                break;
            case Input::Types::EventKind::ElementChanged:
                if(logTrace)
                {
                    InputControlEvents::logTrace(input, event, frame.sequence());
                }
                else if(includeControlEvents && logDebug)
                {
                    const Types::ElementEvent *elementEvent = std::get_if<Types::ElementEvent>(&event.data);
                    if(elementEvent != nullptr && std::holds_alternative<bool>(elementEvent->value))
                    {
                        InputControlEvents::logChange(input, event);
                    }
                    else
                    {
                        InputControlEvents::logRawChange(input, event);
                    }
                }
                break;
            case Input::Types::EventKind::ElementInvalidated:
            case Input::Types::EventKind::ElementRepeated:
            case Input::Types::EventKind::TextCommitted:
            case Input::Types::EventKind::CompositionStarted:
            case Input::Types::EventKind::CompositionUpdated:
            case Input::Types::EventKind::CompositionFinished:
                if(logTrace)
                {
                    InputControlEvents::logTrace(input, event, frame.sequence());
                }
                break;
            }
        }

        // Report interval loss once after processing the captured events.
        if(frame.droppedEvents() != 0)
        {
            Logger::warn(
                LogArea::Input,
                Logger::record()
                    .field("capture_sequence", frame.sequence())
                    .field("event_count", events.size())
                    .field("dropped_events", frame.droppedEvents())
                    .field("discontinuity", frame.discontinuity()),
                "Input event interval was incomplete");
        }

        // Include bounded native report previews at Trace.
        if(logTrace)
        {
            logRawInputReports(frame);
        }
    }
} // namespace GameWIP::Game::RuntimeDiagnostics
