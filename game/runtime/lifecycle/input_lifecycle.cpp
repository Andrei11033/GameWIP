/// @file input_lifecycle.cpp
/// @brief Captures Input health and closes the runtime Input session.

#include "runtime/diagnostics/runtime_diagnostics.h"

#include "logger/logging.h"

#if defined(_WIN32)
#include "input/win32.h"
#endif

namespace
{
    namespace Logger = GameWIP::Logger;
    using LogArea = GameWIP::Game::Logging::Area;

    /// @brief Captures Input health, detaches an optional Win32 source, and closes the Input system.
    [[nodiscard]] bool finishInputSystemImpl(GameWIP::Input::System &input, GameWIP::Input::Platform::Win32::Attachment *attachment)
    {
        bool ok = true;

        // Capture final health before detaching or closing the session.
        GameWIP::Input::Types::Health health;
        const GameWIP::IO::Types::Status healthStatus = input.health(health);
        if(healthStatus.ok())
        {
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
            GameWIP::Game::RuntimeDiagnostics::reportWarning(LogArea::Input, "Failed to query input health", healthStatus);
        }

#if defined(_WIN32)
        // Detach native message routing before closing Input.
        if(attachment != nullptr && attachment->isAttached())
        {
            const GameWIP::IO::Types::Status detachStatus = attachment->detach();
            if(!detachStatus.ok())
            {
                GameWIP::Game::RuntimeDiagnostics::reportFailure(LogArea::Input, "Failed to detach input from window", detachStatus);
                ok = false;
            }
        }
#else
        static_cast<void>(attachment);
#endif

        // Close the portable session after all providers are detached.
        const GameWIP::IO::Types::Status closeStatus = input.close();
        if(!closeStatus.ok())
        {
            GameWIP::Game::RuntimeDiagnostics::reportFailure(LogArea::Input, "Failed to close input system", closeStatus);
            ok = false;
        }
        else
        {
            Logger::info(LogArea::Input, "Input stopped");
        }

        return ok;
    }
} // namespace

namespace GameWIP::Game::RuntimeDiagnostics
{
    bool finishInputSystem(Input::System &input)
    {
        return finishInputSystemImpl(input, nullptr);
    }

#if defined(_WIN32)
    bool finishInputSystem(Input::System &input, Input::Platform::Win32::Attachment &attachment)
    {
        return finishInputSystemImpl(input, &attachment);
    }
#endif
} // namespace GameWIP::Game::RuntimeDiagnostics
