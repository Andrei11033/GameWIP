/// @file input_lifecycle.cpp
/// @brief Captures Input health and closes the runtime Input session.

#include "runtime/diagnostics/runtime_diagnostics.h"

#include "logger/logging.h"

namespace
{
    namespace Logger = GameWIP::Logger;
    using LogArea = GameWIP::Game::Logging::Area;
} // namespace

namespace GameWIP::Game::RuntimeDiagnostics
{
    bool finishInputSystem(Input::System &input, InputDetachCallback detach, void *context)
    {
        bool ok = true;

        // Capture final health before detaching or closing the session.
        const auto healthStatus = input.health();
        if(healthStatus.ok())
        {
            const GameWIP::Input::Types::Health &health = healthStatus.value();
            if(Logger::shouldLog(Logger::Types::Level::Trace, LogArea::Input))
            {
                Logger::trace(
                    LogArea::Input,
                    Logger::record()
                        .field("active_device_count", health.activeDeviceCount)
                        .field("dropped_events", health.totalDroppedEvents)
                        .field("invalid_reports", health.totalInvalidReports)
                        .field("backend_failures", health.totalBackendFailures),
                    "Input session health");
            }
            else if(health.totalInvalidReports != 0 || health.totalBackendFailures != 0)
            {
                Logger::warn(
                    LogArea::Input,
                    Logger::record().field("invalid_reports", health.totalInvalidReports).field("backend_failures", health.totalBackendFailures),
                    "Input session reported failures");
            }
        }
        else
        {
            GameWIP::Game::RuntimeDiagnostics::reportWarning(LogArea::Input, "Failed to query input health", healthStatus.status());
        }

        // Detach native sources before closing Input.
        if(detach != nullptr)
        {
            const auto detachStatus = detach(context);
            if(!detachStatus.ok())
            {
                GameWIP::Game::RuntimeDiagnostics::reportFailure(LogArea::Input, "Failed to detach input source", detachStatus.status());
                ok = false;
            }
        }

        // Close the portable session after all providers are detached.
        const auto closeStatus = input.close();
        if(!closeStatus.ok())
        {
            GameWIP::Game::RuntimeDiagnostics::reportFailure(LogArea::Input, "Failed to close input system", closeStatus.status());
            ok = false;
        }
        else
        {
            Logger::info(LogArea::Input, "Input stopped");
        }

        return ok;
    }
} // namespace GameWIP::Game::RuntimeDiagnostics
