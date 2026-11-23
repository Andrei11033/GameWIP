/// @file input_control_changes.cpp
/// @brief Debug reports for discrete and raw Input changes.

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
        /// Debug control changes
        /// ------------------------------------------------------------

        void logInputControlChange(const GameWIP::Input::System &input, const Types::Event &event)
        {
            const Types::ElementEvent *elementEvent = std::get_if<Types::ElementEvent>(&event.data);
            if(elementEvent == nullptr)
            {
                return;
            }

            const Types::DeviceInfo *device = Helpers::findDevice(input, elementEvent->id.device);
            const Types::ElementInfo *element = Helpers::findElement(device, elementEvent->id);
            const bool *current = std::get_if<bool>(&elementEvent->value);
            const bool *previous = elementEvent->previousKnown ? std::get_if<bool>(&elementEvent->previousValue) : nullptr;
            const bool firstPress = current != nullptr && *current && previous == nullptr && Helpers::isEventDrivenDiscreteControl(element);
            if(current == nullptr || (previous == nullptr && !firstPress) || (previous != nullptr && *current == *previous))
            {
                return;
            }

            const std::string name = Helpers::controlName(element, elementEvent->id);
            const std::string_view category = Helpers::controlCategory(element);
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

            const Types::DeviceInfo *device = Helpers::findDevice(input, elementEvent->id.device);
            const Types::ElementInfo *element = Helpers::findElement(device, elementEvent->id);
            if(element == nullptr)
            {
                return;
            }

            if(Helpers::isDerivedGamepadAxis(element))
            {
                return;
            }

            const bool relative = element->reporting == Types::ReportingMode::Relative;
            const std::string_view category = Helpers::controlCategory(element);
            if(relative && category != "mouse wheel" && category != "mouse movement")
            {
                return;
            }

            if(!relative && !elementEvent->previousKnown && !Helpers::isPointerPosition(element))
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
                    Logger::code(Helpers::controlName(element, elementEvent->id)),
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
                    Logger::code(Helpers::controlName(element, elementEvent->id)),
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
                Logger::code(Helpers::controlName(element, elementEvent->id)),
                Logger::number(previousValue),
                Logger::number(currentValue),
                Logger::number(elementEvent->id.device.slot),
                Logger::code(Format::valueProvenance(element->provenance)));
        }

    } // namespace

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

} // namespace GameWIP::Game::RuntimeDiagnostics::InputControlEvents
