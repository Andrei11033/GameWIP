/// @file input_reports.cpp
/// @brief Logs bounded Win32 raw input report details at Trace.

#include "runtime/diagnostics/input_reports.h"

#include "logger/logging.h"
#include "logger/message_format.h"
#include "runtime/logging.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <format>
#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace GameWIP::Game::RuntimeDiagnostics
{
    /// ------------------------------------------------------------
    /// Raw input report logging
    /// ------------------------------------------------------------

    namespace
    {
        namespace Logger = GameWIP::Logger;
        namespace Types = GameWIP::Input::Types;
        using LogArea = GameWIP::Game::Logging::Area;

        /// ------------------------------------------------------------
        /// Raw keyboard, mouse, and HID reports
        /// ------------------------------------------------------------

        [[nodiscard]] std::string rawVirtualKeyName(USHORT virtualKey)
        {
            if(virtualKey >= VK_F13 && virtualKey <= VK_F24)
            {
                return std::format("F{}", virtualKey - VK_F13 + 13);
            }
            return std::format("0x{:04X}", virtualKey);
        }

        void logRawKeyboardReports(const Types::Frame &frame)
        {
            for(const Types::RawReport &report : frame.rawReports())
            {
                if(report.backend != Types::BackendKind::Win32RawInput ||
                   report.bytes.size() < offsetof(RAWINPUT, data.keyboard) + sizeof(RAWKEYBOARD))
                {
                    continue;
                }

                RAWINPUT raw{};
                std::memcpy(&raw, report.bytes.data(), std::min(report.bytes.size(), sizeof(raw)));
                if(raw.header.dwType != RIM_TYPEKEYBOARD)
                {
                    continue;
                }

                const RAWKEYBOARD &keyboard = raw.data.keyboard;
                // Preserve the raw E0/E1 prefix for scan-code diagnostics.
                const std::string_view prefix = (keyboard.Flags & RI_KEY_E1) != 0 ? "E1" : ((keyboard.Flags & RI_KEY_E0) != 0 ? "E0" : "none");
                const std::string keyName = rawVirtualKeyName(keyboard.VKey);
                Logger::trace(
                    LogArea::Input,
                    Logger::record()
                        .field("capture_sequence", frame.sequence())
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
        }

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

        void logRawMouseButtonReports(const Types::Frame &frame)
        {
            constexpr USHORT buttonFlags = RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_DOWN |
                                           RI_MOUSE_RIGHT_BUTTON_UP | RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP |
                                           RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP | RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP;

            for(const Types::RawReport &report : frame.rawReports())
            {
                if(report.backend != Types::BackendKind::Win32RawInput || report.bytes.size() < offsetof(RAWINPUT, data.mouse) + sizeof(RAWMOUSE))
                {
                    continue;
                }

                RAWINPUT raw{};
                std::memcpy(&raw, report.bytes.data(), std::min(report.bytes.size(), sizeof(raw)));
                if(raw.header.dwType != RIM_TYPEMOUSE || (raw.data.mouse.usButtonFlags & buttonFlags) == 0)
                {
                    continue;
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
                        .field("capture_sequence", frame.sequence())
                        .field("device_system_instance", report.device.instance)
                        .field("device_slot", report.device.slot)
                        .field("device_generation", report.device.generation)
                        .field("raw_button_flags", std::format("0x{:04X}", flags)),
                    "Raw mouse buttons: {} [device #{}]",
                    Logger::code(buttons),
                    Logger::number(report.device.slot));
            }
        }

        void logRawHidReports(const Types::Frame &frame)
        {
            // Emit one bounded report preview per HID device and frame.
            constexpr std::size_t kMaximumReportedDevices = 64;
            constexpr std::size_t kPreviewBytes = 32;
            std::array<Types::DeviceId, kMaximumReportedDevices> reportedDevices{};
            std::size_t reportedDeviceCount = 0;

            for(const Types::RawReport &report : frame.rawReports())
            {
                if(report.backend != Types::BackendKind::Win32Hid || report.bytes.empty())
                {
                    continue;
                }

                if(std::find(reportedDevices.begin(), reportedDevices.begin() + static_cast<std::ptrdiff_t>(reportedDeviceCount), report.device) !=
                   reportedDevices.begin() + static_cast<std::ptrdiff_t>(reportedDeviceCount))
                {
                    continue;
                }

                if(reportedDeviceCount == reportedDevices.size())
                {
                    return;
                }
                reportedDevices[reportedDeviceCount++] = report.device;

                // Keep the bytes in the message instead of repeating them as fields.
                const std::size_t previewCount = std::min(report.bytes.size(), kPreviewBytes);
                std::string preview;
                preview.reserve(previewCount * 3);
                for(std::size_t index = 0; index < previewCount; ++index)
                {
                    if(!preview.empty())
                    {
                        preview += ' ';
                    }
                    preview += std::format("{:02X}", std::to_integer<unsigned int>(report.bytes[index]));
                }

                Logger::trace(
                    LogArea::Input,
                    Logger::record()
                        .field("capture_sequence", frame.sequence())
                        .field("device_system_instance", report.device.instance)
                        .field("device_slot", report.device.slot)
                        .field("device_generation", report.device.generation)
                        .field("hid_report_id", std::to_integer<unsigned int>(report.bytes.front()))
                        .field("report_size", report.bytes.size())
                        .field("report_preview_truncated", report.bytes.size() > previewCount),
                    "Raw HID report: id={} size={} bytes=[{}] [device #{}]",
                    Logger::code(std::format("0x{:02X}", std::to_integer<unsigned int>(report.bytes.front()))),
                    Logger::number(report.bytes.size()),
                    Logger::code(preview),
                    Logger::number(report.device.slot));
            }
        }
    } // namespace

    void logRawInputReports(const GameWIP::Input::Types::Frame &frame)
    {
        logRawKeyboardReports(frame);
        logRawMouseButtonReports(frame);
        logRawHidReports(frame);
    }
} // namespace GameWIP::Game::RuntimeDiagnostics
