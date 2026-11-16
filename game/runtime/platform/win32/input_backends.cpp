/// @file input_backends.cpp
/// @brief Reports Win32 input backend status at Trace.

#include "runtime/diagnostics/runtime_diagnostics.h"

#include "runtime/diagnostics/input_event_format.h"
#include "runtime/platform/win32/input_backends.h"

#include "logger/logging.h"
#include "logger/message_format.h"

#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace GameWIP::Game::RuntimeDiagnostics::Win32
{
    /// ------------------------------------------------------------
    /// Backend status reporting
    /// ------------------------------------------------------------

    namespace Logger = GameWIP::Logger;
    using LogArea = GameWIP::Game::Logging::Area;

    void logInputBackends(const Input::Platform::Win32::Attachment &attachment)
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
} // namespace GameWIP::Game::RuntimeDiagnostics::Win32
