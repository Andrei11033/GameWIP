/// @file input_devices.cpp
/// @brief Reports the connected Input device inventory.

#include "runtime_diagnostics.h"

#include "input_event_format.h"
#include "logger/logging.h"
#include "logger/message_format.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace
{
    namespace Logger = GameWIP::Logger;
    namespace Format = GameWIP::Game::RuntimeDiagnostics::InputEventFormat;
    using LogArea = GameWIP::Game::Logging::Area;
} // namespace

namespace GameWIP::Game::RuntimeDiagnostics
{
    /// ------------------------------------------------------------
    /// Device inventory
    /// ------------------------------------------------------------
    void logInputDevices(const Input::System &input, bool includeTraceDetails)
    {
        // Skip enumeration when the selected log levels do not need an inventory.
        const bool logInventory = Logger::shouldLog(Logger::Types::Level::Info, LogArea::Input);
        const bool logDevices = Logger::shouldLog(Logger::Types::Level::Debug, LogArea::Input);
        const bool logTrace = includeTraceDetails && Logger::shouldLog(Logger::Types::Level::Trace, LogArea::Input);
        if(!logInventory && !logDevices && !logTrace)
        {
            return;
        }

        // Resolve the descriptor list once for this inventory pass.
        const auto devicesResult = input.devices();
        if(!devicesResult.ok())
        {
            reportWarning(LogArea::Input, "Failed to enumerate input devices", devicesResult.status());
            return;
        }
        const std::span<const Input::Types::DeviceInfo> devices = devicesResult.value();

        // Count connected devices for the Info summary.
        std::size_t connectedDevices = 0;
        for(const auto &device : devices)
        {
            connectedDevices += device.connected ? 1U : 0U;
        }

        if(logInventory)
        {
            Logger::info(
                LogArea::Input,
                "Devices enumerated: {} connected of {} known",
                Logger::number(connectedDevices),
                Logger::number(devices.size()));
        }

        for(const Input::Types::DeviceInfo &device : devices)
        {
            if(!device.connected)
            {
                continue;
            }

            if(logTrace)
            {
                logInputDeviceTrace(&device, device.id);
            }
            else if(logDevices)
            {
                const std::string backends = Format::backendSummary(device.sources);
                const std::string_view name = device.name.empty() ? std::string_view{"<unnamed>"} : std::string_view{device.name};
                Logger::Record record = Logger::record();
                record.field("device_system_instance", device.id.instance)
                    .field("device_slot", device.id.slot)
                    .field("device_generation", device.id.generation);
                if(device.manufacturer.empty() || device.manufacturer == device.name)
                {
                    Logger::debug(
                        LogArea::Input,
                        record,
                        "{} #{}: {} ({} controls, {} standard; {})",
                        Logger::code(Format::deviceKind(device)),
                        Logger::number(device.id.slot),
                        Logger::quoted(name),
                        Logger::number(device.elements.size()),
                        Logger::number(Format::standardControlCount(device)),
                        Logger::code(backends));
                }
                else
                {
                    Logger::debug(
                        LogArea::Input,
                        record,
                        "{} #{}: {} by {} ({} controls, {} standard; {})",
                        Logger::code(Format::deviceKind(device)),
                        Logger::number(device.id.slot),
                        Logger::quoted(name),
                        Logger::quoted(device.manufacturer),
                        Logger::number(device.elements.size()),
                        Logger::number(Format::standardControlCount(device)),
                        Logger::code(backends));
                }
            }
        }
    }
} // namespace GameWIP::Game::RuntimeDiagnostics
