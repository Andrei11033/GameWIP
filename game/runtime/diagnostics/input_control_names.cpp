/// @file input_control_names.cpp
/// @brief Formats standard and HID control names.

#include "input_event_format.h"

#include "input/standard.h"

#include <array>
#include <format>
#include <string>
#include <string_view>

namespace
{
    namespace Standard = GameWIP::Input::Standard;
    namespace Types = GameWIP::Input::Types;

    /// ------------------------------------------------------------
    /// Standard control-name tables
    /// ------------------------------------------------------------

    struct StandardName
    {
        Types::StandardId id;
        std::string_view name;
    };

    constexpr std::array kKeyboardNames{
        StandardName{Standard::Key::Enter, "Enter"},
        StandardName{Standard::Key::Escape, "Escape"},
        StandardName{Standard::Key::Backspace, "Backspace"},
        StandardName{Standard::Key::Tab, "Tab"},
        StandardName{Standard::Key::Space, "Space"},
        StandardName{Standard::Key::Minus, "Minus"},
        StandardName{Standard::Key::Equals, "Equals"},
        StandardName{Standard::Key::LeftBracket, "Left Bracket"},
        StandardName{Standard::Key::RightBracket, "Right Bracket"},
        StandardName{Standard::Key::Backslash, "Backslash"},
        StandardName{Standard::Key::Semicolon, "Semicolon"},
        StandardName{Standard::Key::Apostrophe, "Apostrophe"},
        StandardName{Standard::Key::Grave, "Grave"},
        StandardName{Standard::Key::Comma, "Comma"},
        StandardName{Standard::Key::Period, "Period"},
        StandardName{Standard::Key::Slash, "Slash"},
        StandardName{Standard::Key::CapsLock, "Caps Lock"},
        StandardName{Standard::Key::PrintScreen, "Print Screen"},
        StandardName{Standard::Key::ScrollLock, "Scroll Lock"},
        StandardName{Standard::Key::Pause, "Pause"},
        StandardName{Standard::Key::Insert, "Insert"},
        StandardName{Standard::Key::Home, "Home"},
        StandardName{Standard::Key::PageUp, "Page Up"},
        StandardName{Standard::Key::Delete, "Delete"},
        StandardName{Standard::Key::End, "End"},
        StandardName{Standard::Key::PageDown, "Page Down"},
        StandardName{Standard::Key::RightArrow, "Right Arrow"},
        StandardName{Standard::Key::LeftArrow, "Left Arrow"},
        StandardName{Standard::Key::DownArrow, "Down Arrow"},
        StandardName{Standard::Key::UpArrow, "Up Arrow"},
        StandardName{Standard::Key::NumLock, "Num Lock"},
        StandardName{Standard::Key::KeypadSlash, "Numpad Slash"},
        StandardName{Standard::Key::KeypadAsterisk, "Numpad Asterisk"},
        StandardName{Standard::Key::KeypadMinus, "Numpad Minus"},
        StandardName{Standard::Key::KeypadPlus, "Numpad Plus"},
        StandardName{Standard::Key::KeypadEnter, "Numpad Enter"},
        StandardName{Standard::Key::KeypadPeriod, "Numpad Period"},
        StandardName{Standard::Key::Application, "Application"},
        StandardName{Standard::Key::LeftControl, "Left Control"},
        StandardName{Standard::Key::LeftShift, "Left Shift"},
        StandardName{Standard::Key::LeftAlt, "Left Alt"},
        StandardName{Standard::Key::LeftSuper, "Left Super"},
        StandardName{Standard::Key::RightControl, "Right Control"},
        StandardName{Standard::Key::RightShift, "Right Shift"},
        StandardName{Standard::Key::RightAlt, "Right Alt"},
        StandardName{Standard::Key::RightSuper, "Right Super"}};

    constexpr std::array kGamepadNames{
        StandardName{Standard::Gamepad::South, "South"},
        StandardName{Standard::Gamepad::East, "East"},
        StandardName{Standard::Gamepad::West, "West"},
        StandardName{Standard::Gamepad::North, "North"},
        StandardName{Standard::Gamepad::DpadUp, "D-pad Up"},
        StandardName{Standard::Gamepad::DpadDown, "D-pad Down"},
        StandardName{Standard::Gamepad::DpadLeft, "D-pad Left"},
        StandardName{Standard::Gamepad::DpadRight, "D-pad Right"},
        StandardName{Standard::Gamepad::LeftShoulder, "Left Shoulder"},
        StandardName{Standard::Gamepad::RightShoulder, "Right Shoulder"},
        StandardName{Standard::Gamepad::Back, "Back"},
        StandardName{Standard::Gamepad::Start, "Start"},
        StandardName{Standard::Gamepad::Guide, "Guide"},
        StandardName{Standard::Gamepad::LeftStickButton, "Left Stick Button"},
        StandardName{Standard::Gamepad::RightStickButton, "Right Stick Button"},
        StandardName{Standard::Gamepad::LeftStick, "Left Stick"},
        StandardName{Standard::Gamepad::RightStick, "Right Stick"},
        StandardName{Standard::Gamepad::LeftTrigger, "Left Trigger"},
        StandardName{Standard::Gamepad::RightTrigger, "Right Trigger"},
        StandardName{Standard::Gamepad::LeftPaddle, "Left Paddle"},
        StandardName{Standard::Gamepad::RightPaddle, "Right Paddle"},
        StandardName{Standard::Gamepad::Touchpad, "Touchpad"},
        StandardName{Standard::Gamepad::Miscellaneous, "Miscellaneous"},
        StandardName{Standard::Gamepad::Capture, "Capture"},
        StandardName{Standard::Gamepad::Mute, "Mute"}};

} // namespace

namespace GameWIP::Game::RuntimeDiagnostics::InputEventFormat
{
    namespace Input = GameWIP::Input;

    /// ------------------------------------------------------------
    /// Standard control names
    /// ------------------------------------------------------------

    std::string controlName(Input::Types::StandardId id)
    {
        using Domain = Input::Types::StandardDomain;
        if(id.domain == Domain::Keyboard)
        {
            if(id.code >= Standard::Key::A.code && id.code <= Standard::Key::Z.code)
            {
                return std::string(1, static_cast<char>('A' + id.code - Standard::Key::A.code));
            }

            if(id.code >= Standard::Key::Digit1.code && id.code <= Standard::Key::Digit9.code)
            {
                return std::string(1, static_cast<char>('1' + id.code - Standard::Key::Digit1.code));
            }

            if(id.code == Standard::Key::Digit0.code)
            {
                return "0";
            }

            if(id.code >= Standard::Key::F1.code && id.code <= Standard::Key::F12.code)
            {
                return std::format("F{}", id.code - Standard::Key::F1.code + 1);
            }

            if(id.code >= 0x68 && id.code <= 0x73)
            {
                return std::format("F{}", id.code - 0x68 + 13);
            }

            if(id.code >= Standard::Key::Keypad1.code && id.code <= Standard::Key::Keypad9.code)
            {
                return std::format("Numpad {}", id.code - Standard::Key::Keypad1.code + 1);
            }

            if(id.code == Standard::Key::Keypad0.code)
            {
                return "Numpad 0";
            }

            if(id.code == 0x64)
            {
                return "Non-US Backslash";
            }

            if(id.code == 0x67)
            {
                return "Numpad Equals";
            }
            for(const StandardName &entry : kKeyboardNames)
            {
                if(entry.id == id)
                {
                    return std::string(entry.name);
                }
            }
            return {};
        }

        if(id.domain == Domain::Mouse)
        {
            switch(id.code)
            {
            case Standard::Mouse::Left.code:
                return "Left";
            case Standard::Mouse::Right.code:
                return "Right";
            case Standard::Mouse::Middle.code:
                return "Middle";
            case Standard::Mouse::X1.code:
                return "X1";
            case Standard::Mouse::X2.code:
                return "X2";
            case Standard::Mouse::Movement.code:
                return "Movement";
            case Standard::Mouse::Wheel.code:
                return "Wheel";
            default:
                if(id.code >= Standard::Mouse::Button6.code && id.code <= Standard::Mouse::Button16.code)
                {
                    return std::format("Button{}", id.code - Standard::Mouse::Button6.code + 6);
                }
                return {};
            }
        }

        if(id.domain == Domain::Gamepad)
        {
            for(const StandardName &entry : kGamepadNames)
            {
                if(entry.id == id)
                {
                    return std::string(entry.name);
                }
            }
        }
        else if(id.domain == Domain::Pointer && id.code == Standard::Pointer::Position.code)
        {
            return "Position";
        }

        return {};
    }

    /// ------------------------------------------------------------
    /// HID control names
    /// ------------------------------------------------------------

    std::string controlName(const Input::Types::HidControlId &id)
    {
        if(id.usagePage == 0x07)
        {
            return controlName({Input::Types::StandardDomain::Keyboard, id.usage});
        }

        if(id.usagePage == 0x01)
        {
            switch(id.usage)
            {
            case 0x30:
                return "X axis";
            case 0x31:
                return "Y axis";
            case 0x32:
                return "Z axis";
            case 0x33:
                return "X rotation";
            case 0x34:
                return "Y rotation";
            case 0x35:
                return "Z rotation";
            case 0x36:
                return "Slider";
            case 0x37:
                return "Dial";
            case 0x38:
                return "Wheel";
            case 0x39:
                return "Hat switch";
            default:
                break;
            }
        }

        return std::format("HID {:04X}:{:04X}", id.usagePage, id.usage);
    }

} // namespace GameWIP::Game::RuntimeDiagnostics::InputEventFormat
