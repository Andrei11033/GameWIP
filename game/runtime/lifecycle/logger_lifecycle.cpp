/// @file logger_lifecycle.cpp
/// @brief Initializes, reports, and shuts down the runtime Logger session.

#include "logger_lifecycle.h"

#include "runtime/diagnostics/runtime_diagnostics.h"
#include "runtime/logging.h"
#include "runtime/profiling.h"

#include "logger/logging.h"

#include <gamewip/version.h>

#include <chrono>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>

namespace
{
    namespace Color = GameWIP::Profiling::Color;
    namespace Diagnostics = GameWIP::Game::RuntimeDiagnostics;
    namespace Logger = GameWIP::Logger;
    namespace Status = GameWIP::Status::Types;
    using LogArea = GameWIP::Game::Logging::Area;

    /// ------------------------------------------------------------
    /// Logger labels
    /// ------------------------------------------------------------

    /// @brief Converts Logger output modes into stable diagnostic labels.
    [[nodiscard]] constexpr std::string_view outputModeName(Logger::Types::OutputMode mode) noexcept
    {
        switch(mode)
        {
        case Logger::Types::OutputMode::None:
            return "none";
        case Logger::Types::OutputMode::Console:
            return "console";
        case Logger::Types::OutputMode::File:
            return "file";
        case Logger::Types::OutputMode::Both:
            return "both";
        }
        return "unknown";
    }

    /// @brief Converts Logger health states into stable diagnostic labels.
    [[nodiscard]] constexpr std::string_view loggerHealthName(Logger::Types::Health::State state) noexcept
    {
        switch(state)
        {
        case Logger::Types::Health::State::Healthy:
            return "healthy";
        case Logger::Types::Health::State::Degraded:
            return "degraded";
        case Logger::Types::Health::State::Disabled:
            return "disabled";
        }
        return "unknown";
    }

    /// @brief Converts Logger failure sources into stable diagnostic labels.
    [[nodiscard]] constexpr std::string_view loggerFailureSourceName(Logger::Types::Health::FailureSource source) noexcept
    {
        switch(source)
        {
        case Logger::Types::Health::FailureSource::None:
            return "none";
        case Logger::Types::Health::FailureSource::Console:
            return "console";
        case Logger::Types::Health::FailureSource::File:
            return "file";
        case Logger::Types::Health::FailureSource::DebugOutput:
            return "debug_output";
        case Logger::Types::Health::FailureSource::FatalPopup:
            return "fatal_popup";
        case Logger::Types::Health::FailureSource::TimeConversion:
            return "time_conversion";
        }
        return "unknown";
    }

    [[nodiscard]] std::string statusDescription(const Status::Status &status)
    {
        const Status::Diagnostic *diagnostic = status.diagnostic();
        const std::string nativeCode =
            diagnostic != nullptr && diagnostic->nativeCode.has_value()
                ? std::format("{}:{}", Status::nativeCodeDomainName(diagnostic->nativeCode->domain), diagnostic->nativeCode->value)
                : "<none>";
        const std::string message = diagnostic != nullptr && !diagnostic->message.empty() ? diagnostic->message : "<none>";
        return std::format("code={} nativeCode={} message=\"{}\"", Status::codeName(status.code()), nativeCode, message);
    }
} // namespace

namespace GameWIP::Game::Runtime
{
    /// ------------------------------------------------------------
    /// Logger initialization and reports
    /// ------------------------------------------------------------

    Logger::Types::Init::Result initializeLogger(const Options &options)
    {
        GAMEWIP_PROFILE_ZONE("Init Logger", Color::kInitialization);

        Logger::Types::Config config = Logger::defaultConfig();
        config.output = Logger::Types::OutputMode::Console;
        config.minLevel = options.minLevel;
        // Runtime snapshots and Trace event details use more fields than the general Logger default.
        config.maxRecordFields = 40;
        config.sources = Logging::kSources;

        return Logger::init(config);
    }

    void reportLoggerInitializationFailure(const Logger::Types::Init::Result &result)
    {
        const Logger::Types::Init::Info *info = result.hasValue() ? &result.value() : nullptr;
        const std::string setupStatus =
            info != nullptr && info->outputSetupStatus.has_value() ? statusDescription(*info->outputSetupStatus) : "<none>";
        Diagnostics::writeEmergencyMessage(
            std::format(
                "GameWIP: Logger initialization failed: status({}), outcome={}, requested_output={}, effective_output={}, output_setup({})",
                statusDescription(result.status()),
                info != nullptr && info->outcome == Logger::Types::Init::Outcome::Started ? "started" : "disabled",
                info != nullptr ? outputModeName(info->requestedOutput) : "unknown",
                info != nullptr ? outputModeName(info->effectiveOutput) : "unknown",
                setupStatus));
    }

    void reportInvalidOptions(const Options &options)
    {
        if(!options.invalidLogLevel.empty())
        {
            Logger::warn(
                LogArea::Startup,
                Logger::record()
                    .field("setting", options.invalidLogLevelSetting)
                    .field("value", options.invalidLogLevel)
                    .field("fallback", levelName(options.minLevel))
                    .field("fallback_source", options.logLevelSource),
                "Invalid runtime setting; using fallback");
        }

        if(!options.invalidInputDevices.empty())
        {
            Logger::warn(
                LogArea::Startup,
                Logger::record().field("setting", "--inputDevices").field("value", options.invalidInputDevices),
                "Runtime flag does not take a value");
        }

        if(!options.invalidInputDebug.empty())
        {
            Logger::warn(
                LogArea::Startup,
                Logger::record().field("setting", "--inputDebug").field("value", options.invalidInputDebug),
                "Runtime flag does not take a value");
        }

        if(!options.invalidWindowEvents.empty())
        {
            Logger::warn(
                LogArea::Startup,
                Logger::record().field("setting", "--windowEvents").field("value", options.invalidWindowEvents),
                "Runtime flag does not take a value");
        }

        if(!options.invalidInputBackends.empty())
        {
            Logger::warn(
                LogArea::Startup,
                Logger::record()
                    .field("setting", "--inputBackends")
                    .field("value", options.invalidInputBackends)
                    .field("valid_values", "rawInput,hid,xinput,gameInput,directInput,windowMessages,none")
                    .field("fallback", "all_available"),
                "Invalid runtime setting; using fallback");
        }

        if(!options.invalidEventWait.empty())
        {
            const double fallbackMilliseconds = std::chrono::duration<double, std::milli>{options.eventLoopInterval}.count();
            Logger::warn(
                LogArea::Startup,
                Logger::record()
                    .field("setting", "GAMEWIP_EVENT_WAIT_MS")
                    .field("value", options.invalidEventWait)
                    .field("valid_range_ms", "1..1000")
                    .field("fallback_ms", Logger::number(fallbackMilliseconds)),
                "Invalid runtime setting; using fallback");
        }

        if(!options.invalidDisplayIndex.empty())
        {
            Logger::warn(
                LogArea::Startup,
                Logger::record()
                    .field("setting", "GAMEWIP_DISPLAY_INDEX")
                    .field("value", options.invalidDisplayIndex)
                    .field("fallback", "operating_system_default"),
                "Invalid runtime setting; using fallback");
        }
    }

    void logLoggerStartup(const Options &options)
    {
        Logger::info(
            LogArea::Startup,
            Logger::record().field("minimum_level", levelName(options.minLevel)).field("level_source", options.logLevelSource),
            "Logger started");
    }

    /// ------------------------------------------------------------
    /// Logger shutdown
    /// ------------------------------------------------------------

    int finishRuntime(int primaryExitCode)
    {
        GAMEWIP_PROFILE_ZONE("Logger shutdown", Color::kShutdown);

        // Flush runtime messages before reading Logger statistics.
        const Logger::Types::FlushResult flushResult = Logger::flush();
        if(!flushResult.ok())
        {
            Diagnostics::reportFailure(LogArea::Shutdown, "Logger flush failed", flushResult.status());
            primaryExitCode = EXIT_FAILURE;
        }

        // Report accumulated Logger health before the final stop record.
        const Logger::Types::Stats stats = Logger::getStats();
        const Logger::Types::Health::Snapshot health = Logger::getHealth();
        const std::string lastNativeCode = health.lastNativeCode.has_value() ? std::to_string(health.lastNativeCode->value) : "none";
        const std::string_view lastNativeCodeDomain = health.lastNativeCode.has_value()
                                                          ? Status::nativeCodeDomainName(health.lastNativeCode->domain)
                                                          : std::string_view{"none"};
        if(Logger::shouldLog(Logger::Types::Level::Trace, LogArea::Shutdown))
        {
            Logger::trace(
                LogArea::Shutdown,
                Logger::record()
                    .field("queued", stats.queued)
                    .field("written_before_summary", stats.written)
                    .field("queue_drops_soft", stats.queueDropsSoft)
                    .field("queue_drops_hard", stats.queueDropsHard)
                    .field("allocation_failures", stats.allocationFailures)
                    .field("file_write_failures", stats.fileWriteFailures)
                    .field("format_failures", stats.formatFailures)
                    .field("truncated", stats.truncated)
                    .field("peak_queue_depth", stats.peakQueueDepth)
                    .field("logger_health", loggerHealthName(health.state))
                    .field("effective_output", outputModeName(health.effectiveOutput))
                    .field("logger_failure_count", health.failureCount)
                    .field("last_failure_source", loggerFailureSourceName(health.lastFailureSource))
                    .field("last_error_code", GameWIP::Status::Types::codeName(health.lastError))
                    .field("last_native_code_domain", lastNativeCodeDomain)
                    .field("last_native_code", lastNativeCode)
                    .field("process_exit_code", primaryExitCode)
                    .field(
                        "flush_outcome",
                        flushResult.hasValue() && flushResult.value().outcome == Logger::Types::FlushOutcome::Completed ? "completed" : "timed_out"),
                "Logger session summary");
        }
        else if(
            stats.queueDropsSoft != 0 || stats.queueDropsHard != 0 || stats.allocationFailures != 0 || stats.fileWriteFailures != 0 ||
            stats.formatFailures != 0 || stats.truncated != 0 || health.failureCount != 0)
        {
            Logger::warn(
                LogArea::Shutdown,
                Logger::record()
                    .field("queue_drops_soft", stats.queueDropsSoft)
                    .field("queue_drops_hard", stats.queueDropsHard)
                    .field("allocation_failures", stats.allocationFailures)
                    .field("file_write_failures", stats.fileWriteFailures)
                    .field("format_failures", stats.formatFailures)
                    .field("truncated", stats.truncated)
                    .field("logger_failures", health.failureCount),
                "Logger session reported issues");
        }

        Logger::info(LogArea::Shutdown, Logger::record().field("exit_code", primaryExitCode), "Logger stopping");

        // Stop Logger after every shutdown report has been written.
        const auto shutdownResult = Logger::shutdown();
        if(!shutdownResult.ok())
        {
            Diagnostics::writeEmergencyStatus("Logger shutdown failed", shutdownResult.status());
            return EXIT_FAILURE;
        }
        return primaryExitCode;
    }
} // namespace GameWIP::Game::Runtime
