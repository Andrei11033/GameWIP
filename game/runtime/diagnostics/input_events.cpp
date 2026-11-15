/// @file input_events.cpp
/// @brief Reports Input device and control events at Debug and Trace.

#include "runtime_diagnostics.h"

#include "input_control_events.h"
#include "input_event_format.h"
#include "logger/logging.h"
#include "logger/message_format.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <variant>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
    namespace Logger = GameWIP::Logger;
    namespace Types = GameWIP::Input::Types;
    namespace Format = GameWIP::Game::RuntimeDiagnostics::InputEventFormat;
    using LogArea = GameWIP::Game::Logging::Area;

    // ------------------------------------------------------------
    // Device lookup
    // ------------------------------------------------------------

    [[nodiscard]] const Types::DeviceInfo *findDevice(GameWIP::Input::System &input, Types::DeviceId id)
    {
        const Types::DeviceInfo *device = nullptr;
        if(!input.findDevice(id, device).ok())
        {
            return nullptr;
        }
        return device;
    }

#if defined(_WIN32)

    // ------------------------------------------------------------
    // Raw keyboard, mouse, and HID reports
    // ------------------------------------------------------------

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
            if(report.backend != Types::BackendKind::Win32RawInput || report.bytes.size() < offsetof(RAWINPUT, data.keyboard) + sizeof(RAWKEYBOARD))
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
        constexpr USHORT buttonFlags = RI_MOUSE_LEFT_BUTTON_DOWN | RI_MOUSE_LEFT_BUTTON_UP | RI_MOUSE_RIGHT_BUTTON_DOWN | RI_MOUSE_RIGHT_BUTTON_UP |
                                       RI_MOUSE_MIDDLE_BUTTON_DOWN | RI_MOUSE_MIDDLE_BUTTON_UP | RI_MOUSE_BUTTON_4_DOWN | RI_MOUSE_BUTTON_4_UP |
                                       RI_MOUSE_BUTTON_5_DOWN | RI_MOUSE_BUTTON_5_UP;

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
#endif

    // ------------------------------------------------------------
    // Device connection events
    // ------------------------------------------------------------

    void logInputDeviceChange(GameWIP::Input::System &input, const Types::Event &event)
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

    void logInputDeviceEventTrace(GameWIP::Input::System &input, const Types::Event &event, std::uint64_t captureSequence)
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
    // ------------------------------------------------------------
    // Captured frame reporting
    // ------------------------------------------------------------

    void logInputEvents(Input::System &input, const Input::Types::Frame &frame, bool includeTraceDetails, bool includeControlEvents)
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
                if(logTrace)
                {
                    InputControlEvents::logTrace(input, event, frame.sequence());
                }
                break;
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

#if defined(_WIN32)
        // Include bounded native report previews at Trace.
        if(logTrace)
        {
            logRawKeyboardReports(frame);
            logRawMouseButtonReports(frame);
            logRawHidReports(frame);
        }
#endif
    }
} // namespace GameWIP::Game::RuntimeDiagnostics
