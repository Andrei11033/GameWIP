/// @file input_devices.cpp
/// @brief Reports the Input device inventory and grouped descriptors.

#include "runtime_diagnostics.h"

#include "input_event_format.h"
#include "logger/logging.h"
#include "logger/message_format.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace
{
    namespace Logger = GameWIP::Logger;
    namespace Format = GameWIP::Game::RuntimeDiagnostics::InputEventFormat;
    namespace Types = GameWIP::Input::Types;
    using LogArea = GameWIP::Game::Logging::Area;

    constexpr std::size_t kMaxTraceSources = 8;
    constexpr std::size_t kMaxTraceNumericControls = 32;
    constexpr std::size_t kMaxTraceButtons = 16;
    constexpr std::size_t kMaxTraceOtherControls = 16;

    // ------------------------------------------------------------
    // Control and component formatting
    // ------------------------------------------------------------

    [[nodiscard]] std::string formatScalar(const Types::Scalar &value)
    {
        return std::visit(
            [](const auto &scalar)
            {
                return std::format("{}", scalar);
            },
            value);
    }

    [[nodiscard]] bool isKeyboardControl(const Types::ElementInfo &element) noexcept
    {
        return (element.identity.standard.has_value() && element.identity.standard->domain == Types::StandardDomain::Keyboard) ||
               (element.identity.hid.has_value() && element.identity.hid->usagePage == 0x07);
    }

    [[nodiscard]] std::string traceControlName(const Types::ElementInfo &element)
    {
        if(element.identity.standard.has_value())
        {
            if(std::string name = Format::controlName(*element.identity.standard); !name.empty())
            {
                return name;
            }
        }

        if(element.identity.hid.has_value())
        {
            return Format::controlName(*element.identity.hid);
        }

        if(!element.name.empty())
        {
            return element.name;
        }
        return std::format("Control #{}", element.id.slot);
    }

    void appendComponent(std::string &report, const Types::ComponentInfo &component)
    {
        std::string details;
        if(component.numeric.minimum.has_value() || component.numeric.maximum.has_value())
        {
            const std::string minimum = component.numeric.minimum.has_value() ? formatScalar(*component.numeric.minimum) : "…";
            const std::string maximum = component.numeric.maximum.has_value() ? formatScalar(*component.numeric.maximum) : "…";
            details = std::format("{}..{}", minimum, maximum);
        }

        if(component.numeric.neutral.has_value())
        {
            details += std::format(" neutral={}", formatScalar(*component.numeric.neutral));
        }

        if(component.numeric.unit != Types::Unit::Unspecified)
        {
            details += std::format(" {}", Format::unit(component.numeric.unit));
        }

        if(component.numeric.nativeStorage != Types::NumericStorage::Unknown)
        {
            details += std::format(" {}", Format::numericStorage(component.numeric.nativeStorage));
        }

        if(details.empty())
        {
            return;
        }

        const std::string_view name = component.name.empty() ? std::string_view{"component"} : std::string_view{component.name};
        report += std::format("; {} [{}]", Logger::quoted(name), details);
    }

    // ------------------------------------------------------------
    // Device report construction
    // ------------------------------------------------------------

    [[nodiscard]] std::string deviceAction(const Types::Event *event)
    {
        if(event == nullptr)
        {
            return {};
        }
        switch(event->kind)
        {
        case Types::EventKind::DeviceConnected:
            return "connected";
        case Types::EventKind::DeviceDisconnected:
            return "disconnected";
        case Types::EventKind::DeviceChanged:
            return "updated";
        case Types::EventKind::ElementChanged:
        case Types::EventKind::ElementRepeated:
        case Types::EventKind::ElementInvalidated:
        case Types::EventKind::TextCommitted:
        case Types::EventKind::CompositionStarted:
        case Types::EventKind::CompositionUpdated:
        case Types::EventKind::CompositionFinished:
            return "changed";
        }
        return "changed";
    }

    [[nodiscard]] std::string makeDeviceTraceReport(const Types::DeviceInfo &device, std::string_view action)
    {
        // Build the device summary.
        const std::string_view name = device.name.empty() ? std::string_view{"<unnamed>"} : std::string_view{device.name};
        const std::size_t standardControls = Format::standardControlCount(device);
        const std::string backends = Format::backendSummary(device.sources);
        const std::string_view manufacturer = device.manufacturer;

        std::string report = std::format(
            "{} #{}{}: {}",
            Logger::code(Format::deviceKind(device)),
            Logger::number(device.id.slot),
            action.empty() ? "" : std::format(" {}", action),
            Logger::quoted(name));
        if(!manufacturer.empty() && manufacturer != name)
        {
            report += std::format(" by {}", Logger::quoted(manufacturer));
        }
        report += std::format(
            " ({} controls, {} standard; {})",
            Logger::number(device.elements.size()),
            Logger::number(standardControls),
            Logger::code(backends));

        // List a bounded set of sources and their native paths.
        if(!device.sources.empty())
        {
            report += "\n  Sources:";
            const std::size_t sourceCount = std::min(device.sources.size(), kMaxTraceSources);
            for(std::size_t index = 0; index < sourceCount; ++index)
            {
                const Types::SourceInfo &source = device.sources[index];
                const std::string_view sourceName = source.backendName.empty() ? Format::backend(source.backend) : source.backendName;
                report += std::format("\n    {}{}", Logger::code(sourceName), source.active ? " (active)" : "");
                if(!source.persistentKey.empty())
                {
                    report += std::format(" — {}", Logger::path(source.persistentKey));
                }

                if(source.vendorId.has_value() && source.productId.has_value())
                {
                    report += std::format(" [VID:PID {:04X}:{:04X}]", *source.vendorId, *source.productId);
                }
            }
            const std::size_t omittedSources = device.sources.size() - sourceCount;
            if(omittedSources != 0)
            {
                report += std::format("\n    … {} more sources", omittedSources);
            }
        }

        // Count control groups before formatting their details.
        std::size_t numericControls = 0;
        std::size_t buttonControls = 0;
        std::size_t otherControls = 0;
        std::size_t keyboardControls = 0;
        for(const Types::ElementInfo &element : device.elements)
        {
            if(isKeyboardControl(element))
            {
                ++keyboardControls;
            }
            else if(element.kind == Types::ValueKind::Boolean)
            {
                ++buttonControls;
            }
            else if(element.kind == Types::ValueKind::None)
            {
                ++otherControls;
            }
            else
            {
                ++numericControls;
            }
        }

        // Append bounded control groups with native identities and ranges.
        const auto appendControls =
            [&report, &device](std::string_view heading, std::size_t maximum, auto shouldInclude, std::size_t total, std::string_view omittedLabel)
        {
            std::size_t displayedControls = 0;
            if(total == 0)
            {
                return;
            }

            report += std::format("\n  {}:", heading);
            for(const Types::ElementInfo &element : device.elements)
            {
                if(isKeyboardControl(element) || !shouldInclude(element))
                {
                    continue;
                }

                if(displayedControls == maximum)
                {
                    break;
                }

                report += std::format(
                    "\n    {} — {} {}",
                    Logger::quoted(traceControlName(element)),
                    Logger::code(Format::valueKind(element.kind)),
                    Format::reportingMode(element.reporting));
                if(element.identity.hid.has_value())
                {
                    const Types::HidControlId &hid = *element.identity.hid;
                    report += std::format(
                        " [{} collection={} report=0x{:02X} component={}]",
                        Logger::code(std::format("HID {:04X}:{:04X}", hid.usagePage, hid.usage)),
                        Logger::number(hid.collection),
                        hid.reportId,
                        Logger::number(hid.component));
                }
                for(const Types::ComponentInfo &component : element.components)
                {
                    appendComponent(report, component);
                }
                ++displayedControls;
            }
            const std::size_t omittedControls = total - displayedControls;
            if(omittedControls != 0)
            {
                report += std::format("\n    ... {} more {}", omittedControls, omittedLabel);
            }
        };

        // Keep numeric controls, buttons, and other reported elements separate.
        appendControls(
            "Numeric controls (axes and other values)",
            kMaxTraceNumericControls,
            [](const Types::ElementInfo &element)
            {
                return element.kind != Types::ValueKind::Boolean && element.kind != Types::ValueKind::None;
            },
            numericControls,
            "numeric controls");
        appendControls(
            "Buttons",
            kMaxTraceButtons,
            [](const Types::ElementInfo &element)
            {
                return element.kind == Types::ValueKind::Boolean;
            },
            buttonControls,
            "buttons");
        appendControls(
            "Other controls",
            kMaxTraceOtherControls,
            [](const Types::ElementInfo &element)
            {
                return element.kind == Types::ValueKind::None;
            },
            otherControls,
            "other controls");
        if(keyboardControls != 0)
        {
            report += std::format("\n  {} standard keyboard controls omitted", keyboardControls);
        }

        return report;
    }

} // namespace

namespace GameWIP::Game::RuntimeDiagnostics
{
#if defined(_WIN32)

    // ------------------------------------------------------------
    // Backend status reporting
    // ------------------------------------------------------------

    void logInputBackends(Input::Platform::Win32::Attachment &attachment)
    {
        if(!Logger::shouldLog(Logger::Types::Level::Trace, LogArea::Input))
        {
            return;
        }

        const auto statusName = [](Input::Types::BackendStatus status) -> std::string_view
        {
            using Status = Input::Types::BackendStatus;
            switch(status)
            {
            case Status::Enabled:
                return "enabled";
            case Status::Disabled:
                return "disabled";
            case Status::Unavailable:
                return "unavailable";
            case Status::Unsupported:
                return "unsupported";
            case Status::Failed:
                return "failed";
            }
            return "unknown";
        };

        std::vector<Input::Types::BackendInfo> backends;
        const IO::Types::Status queryStatus = attachment.backends(backends);
        if(!queryStatus.ok())
        {
            reportWarning(LogArea::Input, "Failed to query Win32 input backends", queryStatus);
            return;
        }

        std::string report;
        for(const Input::Types::BackendInfo &backend : backends)
        {
            if(!report.empty())
            {
                report += '\n';
            }
            report += std::format("  {}: {}", Logger::code(InputEventFormat::backend(backend.backend)), statusName(backend.status));
            if(!backend.detail.empty())
            {
                report += std::format(" ({})", Logger::code(backend.detail));
            }
        }

        Logger::trace(LogArea::Input, Logger::record().field("backend_count", backends.size()), "Win32 input backends:\n{}", report);
    }
#endif

    // ------------------------------------------------------------
    // Device inventory and descriptors
    // ------------------------------------------------------------

    void logInputDeviceTrace(
        const Input::Types::DeviceInfo *device,
        Input::Types::DeviceId id,
        const Input::Types::Event *event,
        std::uint64_t captureSequence)
    {
        Logger::Record record = Logger::record();
        std::string action = deviceAction(event);
        // Add event identity when this report follows a captured change.
        if(event != nullptr)
        {
            record.field("event_sequence", event->sequence)
                .field("capture_sequence", captureSequence)
                .field("cause", Format::eventCause(event->cause));
        }
        record.field("device_system_instance", id.instance).field("device_slot", id.slot).field("device_generation", id.generation);

        if(device == nullptr)
        {
            const std::string message =
                std::format("Input device #{} {}: descriptor unavailable", Logger::number(id.slot), action.empty() ? "changed" : action);
            Logger::trace(LogArea::Input, record, "{}", message);
            return;
        }

        // Record the descriptor version before formatting its contents.
        record.field("descriptor_revision", device->descriptorRevision)
            .field("identity_confidence", Format::identityConfidence(device->identityConfidence));

        const std::string report = makeDeviceTraceReport(*device, action);
        Logger::trace(LogArea::Input, record, "{}", report);
    }

    void logInputDevices(Input::System &input, bool includeTraceDetails)
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
        std::span<const Input::Types::DeviceInfo> devices;
        const IO::Types::Status devicesStatus = input.devices(devices);
        if(!devicesStatus.ok())
        {
            reportWarning(LogArea::Input, "Failed to enumerate input devices", devicesStatus);
            return;
        }

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
