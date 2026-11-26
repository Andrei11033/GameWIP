/// @file input_control_helpers.cpp
/// @brief Shared device and event metadata for runtime Input reports.

#include "../input_control_helpers.h"

#include "../../input_event_format.h"
#include "input/standard.h"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>

namespace GameWIP::Game::RuntimeDiagnostics::InputControlEvents::Internal
{
    namespace Standard = GameWIP::Input::Standard;
    namespace Format = GameWIP::Game::RuntimeDiagnostics::InputEventFormat;
    /// ------------------------------------------------------------
    /// Control lookup and classification
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
} // namespace GameWIP::Game::RuntimeDiagnostics::InputControlEvents::Internal
