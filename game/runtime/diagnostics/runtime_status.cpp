/// @file runtime_status.cpp
/// @brief Runtime error and warning delivery.

#include "runtime_diagnostics.h"

#if GAMEWIP_TRACY_ENABLED
#include "runtime/profiling.h"
#endif

#include "logger/logging.h"
#include "logger/message_format.h"
#include "logger/reports.h"
#include "terminal/output.h"

#include <format>
#include <string>
#include <string_view>

namespace GameWIP::Game::RuntimeDiagnostics
{
    namespace Logger = GameWIP::Logger;
    namespace Terminal = GameWIP::Terminal;

    /// ------------------------------------------------------------
    /// Operation status
    /// ------------------------------------------------------------

    void writeEmergencyMessage(std::string_view message) noexcept
    {
        static_cast<void>(Terminal::writeLine(Terminal::Types::Output::Stream::Stderr, message));
    }

    void writeEmergencyStatus(std::string_view action, const IO::Types::Status &status)
    {
        const std::string_view message = status.message.empty() ? std::string_view{"<none>"} : std::string_view{status.message};
        const std::string diagnostic =
            std::format("GameWIP: {}: code={}, nativeCode={}, message=\"{}\"", action, IO::errorCodeName(status.code), status.nativeCode, message);
        writeEmergencyMessage(diagnostic);
    }

    void reportFailure(Logging::Area area, std::string_view action, const IO::Types::Status &status)
    {
        const std::string_view statusMessage = status.message.empty() ? std::string_view{"<none>"} : std::string_view{status.message};
        const std::string_view errorCode = IO::errorCodeName(status.code);

#if GAMEWIP_TRACY_ENABLED
        const std::string message = std::format("{}: {} (code={}, nativeCode={})", action, statusMessage, errorCode, status.nativeCode);
        Profiling::message(message);
#endif

        const Logger::Types::Report::Result report = Logger::reportError(
            area,
            Logger::record()
                .field("action", action)
                .field("error_code", errorCode)
                .field("native_code", status.nativeCode)
                .field("status_message", statusMessage),
            "{}: \"{}\" (code={}, nativeCode={})",
            action,
            Logger::quoted(statusMessage),
            Logger::code(errorCode),
            Logger::number(status.nativeCode));

        if(!report.status.ok())
        {
            writeEmergencyStatus(action, status);
            writeEmergencyStatus("Runtime error report delivery failed", report.status);
        }
        else if(report.delivery == Logger::Types::Report::Delivery::None)
        {
            writeEmergencyStatus(action, status);
        }
    }

    void reportWarning(Logging::Area area, std::string_view action, const IO::Types::Status &status)
    {
        const std::string_view statusMessage = status.message.empty() ? std::string_view{"<none>"} : std::string_view{status.message};
        const std::string_view errorCode = IO::errorCodeName(status.code);
        Logger::warn(
            area,
            Logger::record()
                .field("action", action)
                .field("error_code", errorCode)
                .field("native_code", status.nativeCode)
                .field("status_message", statusMessage),
            "{}: \"{}\" (code={}, nativeCode={})",
            action,
            Logger::quoted(statusMessage),
            Logger::code(errorCode),
            Logger::number(status.nativeCode));
    }
} // namespace GameWIP::Game::RuntimeDiagnostics
