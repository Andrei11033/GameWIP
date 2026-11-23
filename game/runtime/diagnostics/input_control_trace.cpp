/// @file input_control_trace.cpp
/// @brief Trace reports for detailed Input control and text events.

#include "input_control_events.h"
#include "internal/input_control_helpers.h"

#include "input_event_format.h"
#include "logger/logging.h"
#include "logger/message_format.h"
#include "runtime/logging.h"

#include <string>
#include <string_view>
#include <variant>

namespace GameWIP::Game::RuntimeDiagnostics::InputControlEvents
{
    namespace Logger = GameWIP::Logger;
    namespace Types = GameWIP::Input::Types;
    namespace Format = GameWIP::Game::RuntimeDiagnostics::InputEventFormat;
    namespace Helpers = GameWIP::Game::RuntimeDiagnostics::InputControlEvents::Internal;
    using LogArea = GameWIP::Game::Logging::Area;

    namespace
    {
        /// ------------------------------------------------------------
        /// Trace event details
        /// ------------------------------------------------------------

        void logInputEventTrace(const GameWIP::Input::System &input, const Types::Event &event, std::uint64_t captureSequence)
        {
            // Ignore uninformative first samples for absolute controls.
            if(event.kind == Types::EventKind::ElementChanged)
            {
                const Types::ElementEvent *elementEvent = std::get_if<Types::ElementEvent>(&event.data);
                if(elementEvent != nullptr && !elementEvent->previousKnown)
                {
                    const Types::ElementInfo *element = Helpers::findElement(Helpers::findDevice(input, elementEvent->id.device), elementEvent->id);
                    const bool relative = element != nullptr && element->reporting == Types::ReportingMode::Relative;
                    const bool pointerPosition = Helpers::isPointerPosition(element);
                    const bool *current = std::get_if<bool>(&elementEvent->value);
                    const bool firstPress = current != nullptr && *current && Helpers::isEventDrivenDiscreteControl(element);
                    if(!relative && !pointerPosition && !firstPress)
                    {
                        return;
                    }
                }
            }

            // Build structured event fields after filtering the initial sample.
            Logger::Record record = Helpers::eventRecord(event, captureSequence);
            switch(event.kind)
            {
            case Types::EventKind::ElementChanged:
            case Types::EventKind::ElementRepeated:
            case Types::EventKind::ElementInvalidated:
            {
                const Types::ElementEvent *elementEvent = std::get_if<Types::ElementEvent>(&event.data);
                if(elementEvent == nullptr)
                {
                    break;
                }
                const Types::DeviceInfo *device = Helpers::findDevice(input, elementEvent->id.device);
                const Types::ElementInfo *element = Helpers::findElement(device, elementEvent->id);
                if(Helpers::isDerivedGamepadAxis(element))
                {
                    return;
                }

                // Add device, element, and source identity before formatting the value.
                const std::string name = Helpers::controlName(element, elementEvent->id);
                const std::string_view deviceName =
                    device == nullptr || device->name.empty() ? std::string_view{"<unknown device>"} : std::string_view{device->name};
                const std::string_view deviceKind = device == nullptr ? std::string_view{"input device"} : Format::deviceKind(*device);
                Helpers::addDeviceFields(record, device, elementEvent->id.device);
                record.field("element_slot", elementEvent->id.slot)
                    .field("element_generation", elementEvent->id.generation)
                    .field("control", name)
                    .field(
                        "value_kind",
                        element == nullptr ? Format::valueKind(static_cast<Types::ValueKind>(elementEvent->value.index()))
                                           : Format::valueKind(element->kind));
                if(element != nullptr)
                {
                    record.field("reporting_mode", Format::reportingMode(element->reporting))
                        .field("component_count", element->components.size())
                        .field("value_provenance", Format::valueProvenance(element->provenance));
                    if(!element->persistentKey.empty())
                    {
                        record.field("persistent_key", element->persistentKey);
                    }

                    if(element->identity.standard.has_value())
                    {
                        record.field("standard_domain", Format::standardDomain(element->identity.standard->domain))
                            .field("standard_code", element->identity.standard->code);
                    }

                    if(element->identity.hid.has_value())
                    {
                        record.field("hid_usage_page", element->identity.hid->usagePage)
                            .field("hid_usage", element->identity.hid->usage)
                            .field("hid_collection", element->identity.hid->collection)
                            .field("hid_report_id", element->identity.hid->reportId)
                            .field("hid_component", element->identity.hid->component);
                    }
                }

                // Render the change according to its reporting mode and known history.
                const bool relative = element != nullptr && element->reporting == Types::ReportingMode::Relative;
                const std::string currentValue = Format::value(elementEvent->value);
                if(event.kind == Types::EventKind::ElementInvalidated)
                {
                    Logger::trace(
                        LogArea::Input,
                        record,
                        "Control unavailable: {} \"{}\" on {} #{} \"{}\"",
                        Logger::code(Helpers::controlCategory(element)),
                        Logger::quoted(name),
                        Logger::code(deviceKind),
                        Logger::number(elementEvent->id.device.slot),
                        Logger::quoted(deviceName));
                    break;
                }

                if(const bool *current = std::get_if<bool>(&elementEvent->value); current != nullptr)
                {
                    const bool *previous = elementEvent->previousKnown ? std::get_if<bool>(&elementEvent->previousValue) : nullptr;
                    if(previous != nullptr)
                    {
                        Logger::trace(
                            LogArea::Input,
                            record,
                            "Input {} \"{}\": {} -> {} on {} #{} \"{}\"",
                            Logger::code(Helpers::controlCategory(element)),
                            Logger::quoted(name),
                            *previous,
                            *current,
                            Logger::code(deviceKind),
                            Logger::number(elementEvent->id.device.slot),
                            Logger::quoted(deviceName));
                    }
                    else
                    {
                        Logger::trace(
                            LogArea::Input,
                            record,
                            "Input {} \"{}\": pressed on {} #{} \"{}\"",
                            Logger::code(Helpers::controlCategory(element)),
                            Logger::quoted(name),
                            Logger::code(deviceKind),
                            Logger::number(elementEvent->id.device.slot),
                            Logger::quoted(deviceName));
                    }
                }
                else if(relative)
                {
                    Logger::trace(
                        LogArea::Input,
                        record,
                        "Input {} \"{}\": delta={} on {} #{} \"{}\"",
                        Logger::code(Helpers::controlCategory(element)),
                        Logger::quoted(name),
                        Logger::number(currentValue),
                        Logger::code(deviceKind),
                        Logger::number(elementEvent->id.device.slot),
                        Logger::quoted(deviceName));
                }
                else if(elementEvent->previousKnown)
                {
                    const std::string previousValue = Format::value(elementEvent->previousValue);
                    Logger::trace(
                        LogArea::Input,
                        record,
                        "Input {} \"{}\": {} -> {} on {} #{} \"{}\"",
                        Logger::code(Helpers::controlCategory(element)),
                        Logger::quoted(name),
                        Logger::number(previousValue),
                        Logger::number(currentValue),
                        Logger::code(deviceKind),
                        Logger::number(elementEvent->id.device.slot),
                        Logger::quoted(deviceName));
                }
                else
                {
                    Logger::trace(
                        LogArea::Input,
                        record,
                        "Input {} \"{}\": current={} on {} #{} \"{}\"",
                        Logger::code(Helpers::controlCategory(element)),
                        Logger::quoted(name),
                        Logger::number(currentValue),
                        Logger::code(deviceKind),
                        Logger::number(elementEvent->id.device.slot),
                        Logger::quoted(deviceName));
                }
                break;
            }

            case Types::EventKind::DeviceConnected:
            case Types::EventKind::DeviceDisconnected:
            case Types::EventKind::DeviceChanged:
                // Device lifecycle reports are handled by input_events.cpp.
                break;

            case Types::EventKind::TextCommitted:
            case Types::EventKind::CompositionStarted:
            case Types::EventKind::CompositionUpdated:
            case Types::EventKind::CompositionFinished:
            {
                const Types::TextEvent *textEvent = std::get_if<Types::TextEvent>(&event.data);
                if(textEvent == nullptr)
                {
                    break;
                }
                // Keep text content private while retaining composition metadata.
                record.field("surface", textEvent->surface.value)
                    .field("text_bytes", textEvent->utf8.size())
                    .field("selection_start_bytes", textEvent->selectionStartBytes)
                    .field("selection_length_bytes", textEvent->selectionLengthBytes)
                    .field("canceled", textEvent->canceled);
                Logger::trace(
                    LogArea::Input,
                    record,
                    "Input text on surface #{} ({} bytes)",
                    Logger::number(textEvent->surface.value),
                    Logger::number(textEvent->utf8.size()));
                break;
            }
            }
        }
    } // namespace

    void logTrace(const GameWIP::Input::System &input, const GameWIP::Input::Types::Event &event, std::uint64_t captureSequence)
    {
        logInputEventTrace(input, event, captureSequence);
    }
} // namespace GameWIP::Game::RuntimeDiagnostics::InputControlEvents
