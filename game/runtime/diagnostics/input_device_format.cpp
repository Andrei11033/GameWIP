/// @file input_device_format.cpp
/// @brief Formats Input device summaries for diagnostics.

#include "input_event_format.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Game::RuntimeDiagnostics::InputEventFormat
{
    namespace Input = GameWIP::Input;

    /// ------------------------------------------------------------
    /// Device summaries
    /// ------------------------------------------------------------

    std::string backendSummary(std::span<const Input::Types::SourceInfo> sources)
    {
        std::string summary;
        std::vector<std::string_view> seen;
        seen.reserve(sources.size());
        for(const Input::Types::SourceInfo &source : sources)
        {
            const std::string_view name = source.backendName.empty() ? backend(source.backend) : std::string_view{source.backendName};
            if(std::find(seen.begin(), seen.end(), name) != seen.end())
            {
                continue;
            }

            seen.push_back(name);
            if(!summary.empty())
            {
                summary.append(", ");
            }
            summary.append(name);
        }
        return summary.empty() ? "no backend" : summary;
    }

    std::size_t standardControlCount(const Input::Types::DeviceInfo &device)
    {
        return static_cast<std::size_t>(std::count_if(
            device.elements.begin(),
            device.elements.end(),
            [](const Input::Types::ElementInfo &element)
            {
                return element.identity.standard.has_value() && element.identity.standard->isValid();
            }));
    }

    std::string_view deviceKind(const Input::Types::DeviceInfo &device) noexcept
    {
        bool keyboard = false;
        bool mouse = false;
        bool gamepad = false;
        bool pointer = false;
        bool hasHidControls = false;
        bool hasHidSource = false;

        for(const Input::Types::ElementInfo &element : device.elements)
        {
            if(element.identity.standard.has_value())
            {
                switch(element.identity.standard->domain)
                {
                case Input::Types::StandardDomain::Keyboard:
                    keyboard = true;
                    break;
                case Input::Types::StandardDomain::Mouse:
                    mouse = true;
                    break;
                case Input::Types::StandardDomain::Gamepad:
                    gamepad = true;
                    break;
                case Input::Types::StandardDomain::Pointer:
                    pointer = true;
                    break;
                case Input::Types::StandardDomain::None:
                    break;
                }
            }

            if(element.identity.hid.has_value())
            {
                hasHidControls = true;
                keyboard = keyboard || element.identity.hid->usagePage == 0x07;
            }
        }

        for(const Input::Types::SourceInfo &source : device.sources)
        {
            using Backend = Input::Types::BackendKind;
            hasHidSource = hasHidSource || source.backend == Backend::Win32Hid || source.backend == Backend::LinuxHidraw;
            gamepad = gamepad || source.backend == Backend::XInput;
        }

        const unsigned int recognizedKinds = static_cast<unsigned int>(keyboard) + static_cast<unsigned int>(mouse) +
                                             static_cast<unsigned int>(gamepad) + static_cast<unsigned int>(pointer);
        if(recognizedKinds > 1)
        {
            return "composite device";
        }

        if(keyboard)
        {
            return "keyboard";
        }

        if(mouse)
        {
            return "mouse";
        }

        if(gamepad)
        {
            return "gamepad";
        }

        if(pointer)
        {
            return "pointer";
        }
        return hasHidControls || hasHidSource ? "HID device" : "generic device";
    }

} // namespace GameWIP::Game::RuntimeDiagnostics::InputEventFormat
