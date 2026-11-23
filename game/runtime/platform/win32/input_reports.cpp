/// @file input_reports.cpp
/// @brief Decodes Win32 keyboard and mouse details from retained Input reports.

#include "runtime/platform/input_reports.h"

#include "logger/logging.h"
#include "logger/message_format.h"
#include "runtime/logging.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace GameWIP::Game::Runtime::Platform
{
    /// ------------------------------------------------------------
    /// Win32 raw report details
    /// ------------------------------------------------------------

    namespace
    {
        namespace Logger = GameWIP::Logger;
        namespace Types = GameWIP::Input::Types;
        using LogArea = GameWIP::Game::Logging::Area;

        /// ------------------------------------------------------------
        /// Shared Raw Input decoding
        /// ------------------------------------------------------------

        [[nodiscard]] bool copyRawInput(const Types::RawReport &report, std::size_t requiredSize, RAWINPUT &raw)
        {
            if(report.backend != Types::BackendKind::Win32RawInput || report.bytes.size() < requiredSize)
            {
                return false;
            }

            raw = {};
            const std::span<const std::byte> reportBytes{report.bytes};
            const std::size_t copySize = std::min(reportBytes.size(), sizeof(raw));
            const std::span<std::byte> rawBytes = std::as_writable_bytes(std::span{&raw, std::size_t{1}});
            std::ranges::copy(reportBytes.first(copySize), rawBytes.begin());
            return true;
        }

        /// ------------------------------------------------------------
        /// Keyboard details
        /// ------------------------------------------------------------

        [[nodiscard]] std::string rawVirtualKeyName(USHORT virtualKey)
        {
            if(virtualKey >= VK_F13 && virtualKey <= VK_F24)
            {
                return std::format("F{}", virtualKey - VK_F13 + 13);
            }
            return std::format("0x{:04X}", virtualKey);
        }

        void logRawKeyboardReport(const Types::RawReport &report, std::uint64_t captureSequence)
        {
            RAWINPUT raw;
            if(!copyRawInput(report, offsetof(RAWINPUT, data.keyboard) + sizeof(RAWKEYBOARD), raw))
            {
                return;
            }

            if(raw.header.dwType != RIM_TYPEKEYBOARD)
            {
                return;
            }

            const RAWKEYBOARD &keyboard = raw.data.keyboard;
            // Preserve the raw E0/E1 prefix for scan-code diagnostics.
            const std::string_view prefix = (keyboard.Flags & RI_KEY_E1) != 0 ? "E1" : ((keyboard.Flags & RI_KEY_E0) != 0 ? "E0" : "none");
            const std::string keyName = rawVirtualKeyName(keyboard.VKey);
            Logger::trace(
                LogArea::Input,
                Logger::record()
                    .field("capture_sequence", captureSequence)
                    .field("device_system_instance", report.device.instance)
                    .field("device_slot", report.device.slot)
                    .field("device_generation", report.device.generation)
                    .field("raw_make_code", std::format("0x{:04X}", keyboard.MakeCode))
                    .field("raw_prefix", prefix)
                    .field("raw_virtual_key", keyName)
                    .field("raw_keyboard_flags", keyboard.Flags),
                "Raw keyboard key: scan={} prefix={} key={} state={} [device #{}]",
                Logger::code(std::format("0x{:04X}", keyboard.MakeCode)),
                Logger::code(prefix),
                Logger::code(keyName),
                (keyboard.Flags & RI_KEY_BREAK) == 0 ? "pressed" : "released",
                Logger::number(report.device.slot));
        }

        /// ------------------------------------------------------------
        /// Mouse button details
        /// ------------------------------------------------------------

        void appendMouseButtonFlag(std::string &buttons, USHORT flags, USHORT flag, std::string_view name)
        {
            if((flags & flag) == 0)
            {
                return;
            }

            if(!buttons.empty())
            {
                buttons += ", ";
            }
            buttons += name;
        }

        void logRawMouseButtonReport(const Types::RawReport &report, std::uint64_t captureSequence)
        {
            constexpr USHORT buttonFlags = RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_DOWN |
                                           RI_MOUSE_RIGHT_BUTTON_UP | RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP |
                                           RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP;

            RAWINPUT raw;
            if(!copyRawInput(report, offsetof(RAWINPUT, data.mouse) + sizeof(RAWMOUSE), raw))
            {
                return;
            }

            if(raw.header.dwType != RIM_TYPEMOUSE || (raw.data.mouse.usButtonFlags & buttonFlags) == 0)
            {
                return;
            }

            const USHORT flags = raw.data.mouse.usButtonFlags;
            std::string buttons;

            // Keep all transitions from this report in one concise log entry.
            appendMouseButtonFlag(buttons, flags, RI_MOUSE_LEFT_BUTTON_DOWN, "Left down");
            appendMouseButtonFlag(buttons, flags, RI_MOUSE_LEFT_BUTTON_UP, "Left up");
            appendMouseButtonFlag(buttons, flags, RI_MOUSE_RIGHT_BUTTON_DOWN, "Right down");
            appendMouseButtonFlag(buttons, flags, RI_MOUSE_RIGHT_BUTTON_UP, "Right up");
            appendMouseButtonFlag(buttons, flags, RI_MOUSE_MIDDLE_BUTTON_DOWN, "Middle down");
            appendMouseButtonFlag(buttons, flags, RI_MOUSE_MIDDLE_BUTTON_UP, "Middle up");
            appendMouseButtonFlag(buttons, flags, RI_MOUSE_BUTTON_4_DOWN, "X1 down");
            appendMouseButtonFlag(buttons, flags, RI_MOUSE_BUTTON_4_UP, "X1 up");
            appendMouseButtonFlag(buttons, flags, RI_MOUSE_BUTTON_5_DOWN, "X2 down");
            appendMouseButtonFlag(buttons, flags, RI_MOUSE_BUTTON_5_UP, "X2 up");

            Logger::trace(
                LogArea::Input,
                Logger::record()
                    .field("capture_sequence", captureSequence)
                    .field("device_system_instance", report.device.instance)
                    .field("device_slot", report.device.slot)
                    .field("device_generation", report.device.generation)
                    .field("raw_button_flags", std::format("0x{:04X}", flags)),
                "Raw mouse buttons: {} [device #{}]",
                Logger::code(buttons),
                Logger::number(report.device.slot));
        }
    } // namespace

    void logRawInputDetails(const Types::RawReport &report, std::uint64_t captureSequence)
    {
        logRawKeyboardReport(report, captureSequence);
        logRawMouseButtonReport(report, captureSequence);
    }
} // namespace GameWIP::Game::Runtime::Platform
