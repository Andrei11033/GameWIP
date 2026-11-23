/// @file input_control_events.cpp
/// @brief Formats opt-in Input control changes at Debug and Trace.

#include "input_control_events.h"

#include "input_event_format.h"
#include "input/standard.h"
#include "logger/logging.h"
#include "logger/message_format.h"
#include "runtime/logging.h"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <variant>

namespace
{
    namespace Logger = GameWIP::Logger;
    namespace Standard = GameWIP::Input::Standard;
    namespace Types = GameWIP::Input::Types;
    namespace Format = GameWIP::Game::RuntimeDiagnostics::InputEventFormat;
    using LogArea = GameWIP::Game::Logging::Area;

    /// ------------------------------------------------------------
    /// Control lookup and classification
    /// ------------------------------------------------------------

    [[nodiscard]] const Types::DeviceInfo *findDevice(const GameWIP::Input::System &input, Types::DeviceId id)
    {
        const Types::DeviceInfo *device = nullptr;
        if(!input.findDevice(id, device).ok())
        {
            return nullptr;
        }
        return device;
    }

    [[nodiscard]] const Types::ElementInfo *findElement(const Types::DeviceInfo *device, Types::ElementId id)
    {
        if(device == nullptr)
        {
            return nullptr;
        }

        const auto found = std::find_if(
            device->elements.begin(),
            device->elements.end(),
            [id](const Types::ElementInfo &candidate)
            {
                return candidate.id == id;
            });
        return found == device->elements.end() ? nullptr : &*found;
    }

    [[nodiscard]] std::string controlName(const Types::ElementInfo *element, Types::ElementId id)
    {
        if(element != nullptr && element->identity.standard.has_value())
        {
            if(std::string name = Format::controlName(*element->identity.standard); !name.empty())
            {
                return name;
            }
        }

        if(element != nullptr && element->identity.hid.has_value())
        {
            return Format::controlName(*element->identity.hid);
        }

        if(element != nullptr && !element->name.empty())
        {
            return element->name;
        }
        return std::format("Element {}", id.slot);
    }

    [[nodiscard]] std::string_view controlCategory(const Types::ElementInfo *element)
    {
        if(element != nullptr && element->identity.standard.has_value())
        {
            const Types::StandardId id = *element->identity.standard;
            switch(id.domain)
            {
            case Types::StandardDomain::Keyboard:
                return "key";
            case Types::StandardDomain::Mouse:
                if(id == Standard::Mouse::Movement)
                {
                    return "mouse movement";
                }

                if(id == Standard::Mouse::Wheel)
                {
                    return "mouse wheel";
                }
                return "mouse button";
            case Types::StandardDomain::Gamepad:
                if(id == Standard::Gamepad::LeftStick || id == Standard::Gamepad::RightStick || id == Standard::Gamepad::LeftTrigger ||
                   id == Standard::Gamepad::RightTrigger)
                {
                    return "gamepad axis";
                }
                return element->kind == Types::ValueKind::Boolean ? "gamepad button" : "gamepad input";
            case Types::StandardDomain::Pointer:
                return "pointer input";
            case Types::StandardDomain::None:
                break;
            }
        }

        if(element != nullptr && element->identity.hid.has_value() && element->identity.hid->usagePage == 0x07)
        {
            return "key";
        }

        if(element != nullptr && element->identity.hid.has_value() && element->identity.hid->usagePage == 0x01)
        {
            const std::uint32_t usage = element->identity.hid->usage;
            if(usage >= 0x30 && usage <= 0x37)
            {
                return "axis";
            }

            if(usage == 0x38)
            {
                return "mouse wheel";
            }

            if(usage == 0x39)
            {
                return "hat switch";
            }
        }
        return element != nullptr && element->kind == Types::ValueKind::Boolean ? "button" : "input";
    }

    [[nodiscard]] bool isEventDrivenDiscreteControl(const Types::ElementInfo *element) noexcept
    {
        if(element == nullptr || !element->identity.standard.has_value())
        {
            return false;
        }
        const Types::StandardDomain domain = element->identity.standard->domain;
        return domain == Types::StandardDomain::Keyboard || domain == Types::StandardDomain::Mouse;
    }

    [[nodiscard]] bool isDerivedGamepadAxis(const Types::ElementInfo *element) noexcept
    {
        return element != nullptr && element->identity.standard.has_value() && element->identity.standard->domain == Types::StandardDomain::Gamepad &&
               controlCategory(element) == "gamepad axis";
    }

    [[nodiscard]] bool isPointerPosition(const Types::ElementInfo *element) noexcept
    {
        return element != nullptr && element->identity.standard.has_value() && *element->identity.standard == Standard::Pointer::Position;
    }

    /// ------------------------------------------------------------
    /// Structured event metadata
    /// ------------------------------------------------------------

    [[nodiscard]] Logger::Record eventRecord(const Types::Event &event, std::uint64_t captureSequence)
    {
        Logger::Record record = Logger::record();
        record.field("capture_sequence", captureSequence)
            .field("event_sequence", event.sequence)
            .field("event_kind", Format::eventKind(event.kind))
            .field("cause", Format::eventCause(event.cause));
        if(event.timestamp.nativeTicks != 0)
        {
            record.field("native_ticks", event.timestamp.nativeTicks);
        }

        if(event.timestamp.nativeFrequency != 0)
        {
            record.field("native_frequency", event.timestamp.nativeFrequency).field("native_clock_domain", event.timestamp.nativeClockDomain);
        }
        return record;
    }

    void addDeviceFields(Logger::Record &record, const Types::DeviceInfo *device, Types::DeviceId id)
    {
        record.field("device_system_instance", id.instance).field("device_slot", id.slot).field("device_generation", id.generation);
        if(device == nullptr)
        {
            record.field("device_descriptor", "unavailable");
            return;
        }

        record.field("device_connected", device->connected);
    }

    /// ------------------------------------------------------------
    /// Debug control changes
    /// ------------------------------------------------------------

    void logInputControlChange(const GameWIP::Input::System &input, const Types::Event &event)
    {
        const Types::ElementEvent *elementEvent = std::get_if<Types::ElementEvent>(&event.data);
        if(elementEvent == nullptr)
        {
            return;
        }

        const Types::DeviceInfo *device = findDevice(input, elementEvent->id.device);
        const Types::ElementInfo *element = findElement(device, elementEvent->id);
        const bool *current = std::get_if<bool>(&elementEvent->value);
        const bool *previous = elementEvent->previousKnown ? std::get_if<bool>(&elementEvent->previousValue) : nullptr;
        const bool firstPress = current != nullptr && *current && previous == nullptr && isEventDrivenDiscreteControl(element);
        if(current == nullptr || (previous == nullptr && !firstPress) || (previous != nullptr && *current == *previous))
        {
            return;
        }

        const std::string name = controlName(element, elementEvent->id);
        const std::string_view category = controlCategory(element);
        if(firstPress)
        {
            Logger::debug(
                LogArea::Input,
                "{} pressed: {} [device #{}]",
                Logger::code(category),
                Logger::code(name),
                Logger::number(elementEvent->id.device.slot));
        }
        else
        {
            Logger::debug(
                LogArea::Input,
                "{} {}: {} ({} -> {}) [device #{}]",
                Logger::code(category),
                *current ? "pressed" : "released",
                Logger::code(name),
                Logger::number(*previous),
                Logger::number(*current),
                Logger::number(elementEvent->id.device.slot));
        }
    }

    void logInputControlRaw(const GameWIP::Input::System &input, const Types::Event &event)
    {
        const Types::ElementEvent *elementEvent = std::get_if<Types::ElementEvent>(&event.data);
        if(elementEvent == nullptr)
        {
            return;
        }

        const Types::DeviceInfo *device = findDevice(input, elementEvent->id.device);
        const Types::ElementInfo *element = findElement(device, elementEvent->id);
        if(element == nullptr)
        {
            return;
        }

        if(isDerivedGamepadAxis(element))
        {
            return;
        }

        const bool relative = element->reporting == Types::ReportingMode::Relative;
        const std::string_view category = controlCategory(element);
        if(relative && category != "mouse wheel" && category != "mouse movement")
        {
            return;
        }

        if(!relative && !elementEvent->previousKnown && !isPointerPosition(element))
        {
            return;
        }

        const std::string currentValue = Format::value(elementEvent->value);
        if(relative)
        {
            Logger::debug(
                LogArea::Input,
                "{} value: {} (delta={}) [device #{}, {}]",
                Logger::code(category),
                Logger::code(controlName(element, elementEvent->id)),
                Logger::number(currentValue),
                Logger::number(elementEvent->id.device.slot),
                Logger::code(Format::valueProvenance(element->provenance)));
            return;
        }

        if(!elementEvent->previousKnown)
        {
            Logger::debug(
                LogArea::Input,
                "{} value: {} (current={}) [device #{}, {}]",
                Logger::code(category),
                Logger::code(controlName(element, elementEvent->id)),
                Logger::number(currentValue),
                Logger::number(elementEvent->id.device.slot),
                Logger::code(Format::valueProvenance(element->provenance)));
            return;
        }

        const std::string previousValue = Format::value(elementEvent->previousValue);
        if(previousValue == currentValue)
        {
            return;
        }

        Logger::debug(
            LogArea::Input,
            "{} value: {} ({} -> {}) [device #{}, {}]",
            Logger::code(category),
            Logger::code(controlName(element, elementEvent->id)),
            Logger::number(previousValue),
            Logger::number(currentValue),
            Logger::number(elementEvent->id.device.slot),
            Logger::code(Format::valueProvenance(element->provenance)));
    }

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
                const Types::ElementInfo *element = findElement(findDevice(input, elementEvent->id.device), elementEvent->id);
                const bool relative = element != nullptr && element->reporting == Types::ReportingMode::Relative;
                const bool pointerPosition = isPointerPosition(element);
                const bool *current = std::get_if<bool>(&elementEvent->value);
                const bool firstPress = current != nullptr && *current && isEventDrivenDiscreteControl(element);
                if(!relative && !pointerPosition && !firstPress)
                {
                    return;
                }
            }
        }

        // Build structured event fields after filtering the initial sample.
        Logger::Record record = eventRecord(event, captureSequence);
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
            const Types::DeviceInfo *device = findDevice(input, elementEvent->id.device);
            const Types::ElementInfo *element = findElement(device, elementEvent->id);
            if(isDerivedGamepadAxis(element))
            {
                return;
            }

            // Add device, element, and source identity before formatting the value.
            const std::string name = controlName(element, elementEvent->id);
            const std::string_view deviceName =
                device == nullptr || device->name.empty() ? std::string_view{"<unknown device>"} : std::string_view{device->name};
            const std::string_view deviceKind = device == nullptr ? std::string_view{"input device"} : Format::deviceKind(*device);
            addDeviceFields(record, device, elementEvent->id.device);
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
                    Logger::code(controlCategory(element)),
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
                        Logger::code(controlCategory(element)),
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
                        Logger::code(controlCategory(element)),
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
                    Logger::code(controlCategory(element)),
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
                    Logger::code(controlCategory(element)),
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
                    Logger::code(controlCategory(element)),
                    Logger::quoted(name),
                    Logger::number(currentValue),
                    Logger::code(deviceKind),
                    Logger::number(elementEvent->id.device.slot),
                    Logger::quoted(deviceName));
            }
            break;
        }

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

namespace GameWIP::Game::RuntimeDiagnostics::InputControlEvents
{
    /// ------------------------------------------------------------
    /// Runtime logging entry points
    /// ------------------------------------------------------------

    void logChange(const GameWIP::Input::System &input, const GameWIP::Input::Types::Event &event)
    {
        logInputControlChange(input, event);
    }

    void logRawChange(const GameWIP::Input::System &input, const GameWIP::Input::Types::Event &event)
    {
        logInputControlRaw(input, event);
    }

    void logTrace(const GameWIP::Input::System &input, const GameWIP::Input::Types::Event &event, std::uint64_t captureSequence)
    {
        logInputEventTrace(input, event, captureSequence);
    }
} // namespace GameWIP::Game::RuntimeDiagnostics::InputControlEvents
