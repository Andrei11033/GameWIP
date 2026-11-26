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
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    namespace Logger = GameWIP::Logger;
    namespace Status = GameWIP::Status::Types;
    using LogArea = GameWIP::Game::Logging::Area;

    /// ------------------------------------------------------------
    /// Status formatting
    /// ------------------------------------------------------------

    struct StatusDetails
    {
        std::string_view code{};
        std::optional<std::int64_t> nativeCode{};
        std::string_view nativeDomain{};
        std::string_view message{};
    };

    [[nodiscard]] StatusDetails describe(const Status::Status &status) noexcept
    {
        const Status::Diagnostic *diagnostic = status.diagnostic();
        const std::optional<Status::NativeCode> nativeCode = diagnostic != nullptr ? diagnostic->nativeCode : std::nullopt;
        return {
            .code = Status::codeName(status.code()),
            .nativeCode = nativeCode.has_value() ? std::optional<std::int64_t>{nativeCode->value} : std::nullopt,
            .nativeDomain = nativeCode.has_value() ? Status::nativeCodeDomainName(nativeCode->domain) : std::string_view{},
            .message = diagnostic != nullptr ? std::string_view{diagnostic->message} : std::string_view{}};
    }

    [[nodiscard]] std::string statusMessage(const StatusDetails &status)
    {
        const std::string_view message = status.message.empty() ? std::string_view{"<none>"} : status.message;
        const std::string native = status.nativeCode.has_value()
                                       ? std::format("{}:{}", status.nativeDomain, *status.nativeCode)
                                       : std::string("<none>");
        return std::format("code={}, nativeCode={}, message=\"{}\"", status.code, native, message);
    }

    void writeEmergencyDetails(std::string_view action, const StatusDetails &status)
    {
        const std::string details = statusMessage(status);
        static_cast<void>(GameWIP::Terminal::writeLine(
            GameWIP::Terminal::Types::Output::Stream::Stderr,
            std::format("GameWIP: {}: {}", action, details)));
    }

    void reportFailureDetails(LogArea area, std::string_view action, const StatusDetails &status)
    {
        const std::string_view message = status.message.empty() ? std::string_view{"<none>"} : status.message;
        const std::string_view nativeDomain = status.nativeDomain.empty() ? std::string_view{"none"} : status.nativeDomain;
        const std::string nativeCode = status.nativeCode.has_value() ? std::to_string(*status.nativeCode) : "none";

#if GAMEWIP_TRACY_ENABLED
        const std::string profilingMessage = std::format(
            "{}: {} (code={}, nativeCode={}:{})",
            action,
            message,
            status.code,
            nativeDomain,
            nativeCode);
        GameWIP::Profiling::message(profilingMessage);
#endif

        Logger::Record fields = Logger::record()
                                    .field("action", action)
                                    .field("error_code", status.code)
                                    .field("native_code_domain", nativeDomain)
                                    .field("status_message", message);
        if(status.nativeCode.has_value())
        {
            fields.field("native_code", *status.nativeCode);
        }
        else
        {
            fields.field("native_code", "none");
        }

        const Logger::Types::Report::Result report = Logger::reportError(
            area,
            std::move(fields),
            "{}: \"{}\" (code={}, nativeCode={}:{})",
            action,
            Logger::quoted(message),
            Logger::code(status.code),
            Logger::code(nativeDomain),
            nativeCode);

        if(!report.ok())
        {
            writeEmergencyDetails(action, status);
            writeEmergencyDetails("Runtime error report delivery failed", describe(report.status()));
        }
        else if(!report.hasValue() || report.value().delivery == Logger::Types::Report::Delivery::None)
        {
            writeEmergencyDetails(action, status);
        }
    }

    void reportWarningDetails(LogArea area, std::string_view action, const StatusDetails &status)
    {
        const std::string_view message = status.message.empty() ? std::string_view{"<none>"} : status.message;
        const std::string_view nativeDomain = status.nativeDomain.empty() ? std::string_view{"none"} : status.nativeDomain;
        const std::string nativeCode = status.nativeCode.has_value() ? std::to_string(*status.nativeCode) : "none";
        Logger::Record fields = Logger::record()
                                    .field("action", action)
                                    .field("error_code", status.code)
                                    .field("native_code_domain", nativeDomain)
                                    .field("status_message", message);
        if(status.nativeCode.has_value())
        {
            fields.field("native_code", *status.nativeCode);
        }
        else
        {
            fields.field("native_code", "none");
        }

        Logger::warn(
            area,
            std::move(fields),
            "{}: \"{}\" (code={}, nativeCode={}:{})",
            action,
            Logger::quoted(message),
            Logger::code(status.code),
            Logger::code(nativeDomain),
            nativeCode);
    }
} // namespace

namespace GameWIP::Game::RuntimeDiagnostics
{
    namespace Terminal = GameWIP::Terminal;

    /// ------------------------------------------------------------
    /// Operation status
    /// ------------------------------------------------------------

    void writeEmergencyMessage(std::string_view message) noexcept
    {
        static_cast<void>(Terminal::writeLine(Terminal::Types::Output::Stream::Stderr, message));
    }

    void writeEmergencyStatus(std::string_view action, const Status::Types::Status &status)
    {
        writeEmergencyDetails(action, describe(status));
    }

    void reportFailure(Logging::Area area, std::string_view action, const Status::Types::Status &status)
    {
        reportFailureDetails(area, action, describe(status));
    }

    void reportWarning(Logging::Area area, std::string_view action, const Status::Types::Status &status)
    {
        reportWarningDetails(area, action, describe(status));
    }
} // namespace GameWIP::Game::RuntimeDiagnostics
