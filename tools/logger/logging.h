/// @file logging.h
/// @brief Public lifecycle, filtering, and asynchronous logging APIs.

#pragma once

#include "logger/config.h"
#include "logger/detail/log_templates.h"
#include "logger/logger_export.h"
#include "logger/message_format.h"
#include "logger/record.h"

#include <chrono>
#include <cstddef>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

/// @brief Process-wide asynchronous logging module for runtime diagnostics.
/// @details Normal logs are filtered, queue-backed, and optimized for the runtime hot path. Reports are
/// synchronous emergency diagnostics: they bypass filters and queue pressure, try every eligible sink, enabled
/// debug output and the fatal popup, flush active normal sinks, and do not drain older asynchronous work.
namespace GameWIP::Logger
{
    // ------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------

    /// @name Lifecycle
    /// @{

    /// @brief Initializes Logger from a complete configuration.
    LOGGER_EXPORT Types::Init::Result init(const Types::Config &config) noexcept;
    /// @brief Initializes Logger with defaultConfig().
    LOGGER_EXPORT Types::Init::Result initDefault() noexcept;
    /// @brief Initializes a Console-only Logger at the selected minimum level.
    LOGGER_EXPORT Types::Init::Result initConsole(Types::Level minLevel = Types::Level::Info) noexcept;
    /// @brief Initializes a File-only Logger using a UTF-8 directory.
    LOGGER_EXPORT Types::Init::Result initFile(std::string_view directory = {}, Types::Level minLevel = Types::Level::Info) noexcept;
    /// @brief Stops Logger, drains accepted work, flushes and closes active sinks, and returns the first real failure.
    LOGGER_EXPORT IO::Types::Status shutdown() noexcept;
    /// @brief Drains accepted async work and flushes active sinks under an optional Logger-owned deadline.
    /// @details nullopt waits indefinitely, zero polls, positive durations are bounded, and negative durations are invalid.
    LOGGER_EXPORT Types::FlushResult flush(std::optional<std::chrono::milliseconds> timeout = std::nullopt) noexcept;

    /// @}

    // ------------------------------------------------------------
    // State queries and statistics
    // ------------------------------------------------------------

    /// @name State queries and statistics
    /// @{

    /// @brief Returns whether the worker accepts normal log records.
    LOGGER_EXPORT bool running() noexcept;
    /// @brief Returns the configured minimum severity.
    LOGGER_EXPORT Types::Level getMinLevel() noexcept;
    /// @brief Returns the effective normal-output mode.
    LOGGER_EXPORT Types::OutputMode getOutput() noexcept;
    /// @brief Returns the active log-file path as UTF-8 with explicit query status.
    LOGGER_EXPORT Types::LogFilePathResult getLogFilePath() noexcept;
    /// @brief Returns effective queue, batching, and message limits.
    LOGGER_EXPORT Types::QueueLimits getQueueLimits() noexcept;
    /// @brief Returns process-lifetime queue drops, unaffected by resetStats().
    LOGGER_EXPORT std::size_t getLifetimeDroppedLogCount() noexcept;
    /// @brief Returns a coherent Logger health snapshot.
    [[nodiscard]] LOGGER_EXPORT Types::Health::Snapshot getHealth() noexcept;
    /// @brief Returns relaxed resettable statistics counters.
    LOGGER_EXPORT Types::Stats getStats() noexcept;
    /// @brief Returns Logger-retained and available process-memory statistics.
    LOGGER_EXPORT Types::MemoryStats getMemoryStats() noexcept;
    /// @brief Resets statistics counters without changing health state.
    LOGGER_EXPORT void resetStats() noexcept;

    /// @}

    // ------------------------------------------------------------
    // Runtime filtering
    // ------------------------------------------------------------

    /// @name Runtime filtering
    /// @{

    /// @brief Applies the current severity-only normal-log gate.
    LOGGER_EXPORT bool shouldLog(Types::Level level) noexcept;
    /// @brief Applies the current severity-only gate for a string source.
    LOGGER_EXPORT bool shouldLog(Types::Level level, std::string_view source) noexcept;
    /// @brief Applies current severity and registered-source gates.
    LOGGER_EXPORT bool shouldLog(Types::Level level, Types::SourceId source) noexcept;

    /// @brief Applies current severity and source gates for an enum source.
    template <typename Source>
        requires(SourceEnum<Source>)
    bool shouldLog(Types::Level level, Source source) noexcept
    {
        return shouldLog(level, Detail::Core::sourceId(source));
    }

    /// @brief Sets one registered source filter and returns direct operation status.
    LOGGER_EXPORT IO::Types::Status setSourceFilter(Types::SourceId source, bool enabled) noexcept;
    /// @brief Resets one registered source filter to the default enabled state.
    LOGGER_EXPORT IO::Types::Status resetSourceFilter(Types::SourceId source) noexcept;
    /// @brief Resets every registered source filter to the default enabled state.
    LOGGER_EXPORT IO::Types::Status resetSourceFilters() noexcept;
    /// @brief Sets one exact-level filter and returns direct operation status.
    LOGGER_EXPORT IO::Types::Status setLevelFilter(Types::Level level, bool enabled) noexcept;
    /// @brief Resets one exact-level filter to the default enabled state.
    LOGGER_EXPORT IO::Types::Status resetLevelFilter(Types::Level level) noexcept;
    /// @brief Resets every exact-level filter to the default enabled state.
    LOGGER_EXPORT IO::Types::Status resetLevelFilters() noexcept;

    /// @brief Sets one enum-source filter and returns direct operation status.
    template <typename Source>
        requires(SourceEnum<Source>)
    IO::Types::Status setSourceFilter(Source source, bool enabled) noexcept
    {
        return setSourceFilter(Detail::Core::sourceId(source), enabled);
    }

    /// @brief Resets one enum-source filter to the default enabled state.
    template <typename Source>
        requires(SourceEnum<Source>)
    IO::Types::Status resetSourceFilter(Source source) noexcept
    {
        return resetSourceFilter(Detail::Core::sourceId(source));
    }

    /// @}

    // ------------------------------------------------------------
    // Asynchronous logging
    // ------------------------------------------------------------

    /// @cond INTERNAL
    namespace Detail::Core
    {
        /// @brief Applies the string-source normal-log gate before compile-time formatting.
        template <typename... Args>
        void formatAndLogIfEnabled(Types::Level level, std::string_view source, std::format_string<Args...> format, Args &&...args) noexcept
        {
            if(::GameWIP::Logger::shouldLog(level))
            {
                formatAndLog(level, source, format, std::forward<Args>(args)...);
            }
        }

        /// @brief Applies the registered-source normal-log gate before compile-time formatting.
        template <typename... Args>
        void formatAndLogIfEnabled(Types::Level level, Types::SourceId source, std::format_string<Args...> format, Args &&...args) noexcept
        {
            if(::GameWIP::Logger::shouldLog(level, source))
            {
                formatAndLog(level, source, format, std::forward<Args>(args)...);
            }
        }

        /// @brief Applies the string-source normal-log gate before runtime formatting.
        template <typename... Args>
        void runtimeFormatAndLogIfEnabled(Types::Level level, std::string_view source, Types::RuntimeFormat format, Args &&...args) noexcept
        {
            if(::GameWIP::Logger::shouldLog(level))
            {
                runtimeFormatAndLog(level, source, format, args...);
            }
        }

        /// @brief Applies the registered-source normal-log gate before runtime formatting.
        template <typename... Args>
        void runtimeFormatAndLogIfEnabled(Types::Level level, Types::SourceId source, Types::RuntimeFormat format, Args &&...args) noexcept
        {
            if(::GameWIP::Logger::shouldLog(level, source))
            {
                runtimeFormatAndLog(level, source, format, args...);
            }
        }
    } // namespace Detail::Core
    /// @endcond

    /// @name Asynchronous logging
    /// @{

    /// @brief Queues a preformatted normal record with a UTF-8 string source.
    LOGGER_EXPORT void log(Types::Level level, std::string_view source, std::string_view message) noexcept;
    /// @brief Queues a preformatted normal record with a registered source.
    LOGGER_EXPORT void log(Types::Level level, Types::SourceId source, std::string_view message) noexcept;

    /// @brief Queues a preformatted normal record with an enum source.
    template <typename Source>
        requires(SourceEnum<Source>)
    void log(Types::Level level, Source source, std::string_view message) noexcept
    {
        log(level, Detail::Core::sourceId(source), message);
    }

    /// @brief Queues a preformatted normal record with fields and metadata.
    template <typename Source> void log(Types::Level level, Source source, const Record &record, std::string_view message) noexcept
    {
        if(shouldLog(level, Detail::Core::normalizeSource(source)))
        {
            Detail::Core::preformattedLogWithRecord(level, Detail::Core::normalizeSource(source), record, message);
        }
    }

    /// @brief Formats and queues a normal record with fields and metadata.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    void log(Types::Level level, Source &&source, const Record &record, std::format_string<Args...> format, Args &&...args) noexcept
    {
        if(shouldLog(level, Detail::Core::normalizeSource(source)))
        {
            Detail::Core::formatAndLogWithRecord(level, std::forward<Source>(source), record, format, std::forward<Args>(args)...);
        }
    }

    /// @brief Runtime-formats and queues a normal record with fields and metadata.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    void log(Types::Level level, Source &&source, const Record &record, Types::RuntimeFormat format, Args &&...args) noexcept
    {
        if(shouldLog(level, Detail::Core::normalizeSource(source)))
        {
            Detail::Core::runtimeFormatAndLogWithRecord(level, std::forward<Source>(source), record, format, std::forward<Args>(args)...);
        }
    }

    /// @brief Formats and queues a normal record with a UTF-8 string source.
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    void log(Types::Level level, std::string_view source, std::format_string<Args...> format, Args &&...args) noexcept
    {
        Detail::Core::formatAndLogIfEnabled(level, source, format, std::forward<Args>(args)...);
    }

    /// @brief Formats and queues a normal record with a registered source.
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    void log(Types::Level level, Types::SourceId source, std::format_string<Args...> format, Args &&...args) noexcept
    {
        Detail::Core::formatAndLogIfEnabled(level, source, format, std::forward<Args>(args)...);
    }

    /// @brief Formats and queues a normal record with an enum source.
    template <typename Source, typename... Args>
        requires(SourceEnum<Source> && sizeof...(Args) > 0)
    void log(Types::Level level, Source source, std::format_string<Args...> format, Args &&...args) noexcept
    {
        log(level, Detail::Core::sourceId(source), format, std::forward<Args>(args)...);
    }

    /// @brief Runtime-formats and queues a normal record with a UTF-8 string source.
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    void log(Types::Level level, std::string_view source, Types::RuntimeFormat format, Args &&...args) noexcept
    {
        Detail::Core::runtimeFormatAndLogIfEnabled(level, source, format, std::forward<Args>(args)...);
    }

    /// @brief Runtime-formats and queues a normal record with a registered source.
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    void log(Types::Level level, Types::SourceId source, Types::RuntimeFormat format, Args &&...args) noexcept
    {
        Detail::Core::runtimeFormatAndLogIfEnabled(level, source, format, std::forward<Args>(args)...);
    }

    /// @brief Runtime-formats and queues a normal record with an enum source.
    template <typename Source, typename... Args>
        requires(SourceEnum<Source> && sizeof...(Args) > 0)
    void log(Types::Level level, Source source, Types::RuntimeFormat format, Args &&...args) noexcept
    {
        log(level, Detail::Core::sourceId(source), format, std::forward<Args>(args)...);
    }

    /// @brief Declares one fixed-severity family of normal logging overloads.
#define LOGGER_DECLARE_LEVEL_API(name, levelValue) \
    LOGGER_EXPORT void name(std::string_view source, std::string_view message) noexcept; \
    LOGGER_EXPORT void name(Types::SourceId source, std::string_view message) noexcept; \
    LOGGER_EXPORT void name(std::string_view source, const Record &record, std::string_view message) noexcept; \
    LOGGER_EXPORT void name(Types::SourceId source, const Record &record, std::string_view message) noexcept; \
    template <typename Source> \
        requires(SourceEnum<Source>) \
    void name(Source source, std::string_view message) noexcept \
    { \
        name(Detail::Core::sourceId(source), message); \
    } \
    template <typename Source> \
        requires(SourceEnum<Source>) \
    void name(Source source, const Record &record, std::string_view message) noexcept \
    { \
        name(Detail::Core::sourceId(source), record, message); \
    } \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    void name(Source &&source, const Record &record, std::format_string<Args...> format, Args &&...args) noexcept \
    { \
        if(shouldLog(levelValue, Detail::Core::normalizeSource(source))) \
        { \
            Detail::Core::formatAndLogWithRecord(levelValue, std::forward<Source>(source), record, format, std::forward<Args>(args)...); \
        } \
    } \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    void name(Source &&source, const Record &record, Types::RuntimeFormat format, Args &&...args) noexcept \
    { \
        if(shouldLog(levelValue, Detail::Core::normalizeSource(source))) \
        { \
            Detail::Core::runtimeFormatAndLogWithRecord(levelValue, std::forward<Source>(source), record, format, std::forward<Args>(args)...); \
        } \
    } \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    void name(Source &&source, std::format_string<Args...> format, Args &&...args) noexcept \
    { \
        log(levelValue, std::forward<Source>(source), format, std::forward<Args>(args)...); \
    } \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    void name(Source &&source, Types::RuntimeFormat format, Args &&...args) noexcept \
    { \
        log(levelValue, std::forward<Source>(source), format, std::forward<Args>(args)...); \
    }

    LOGGER_DECLARE_LEVEL_API(trace, Types::Level::Trace)
    LOGGER_DECLARE_LEVEL_API(debug, Types::Level::Debug)
    LOGGER_DECLARE_LEVEL_API(info, Types::Level::Info)
    LOGGER_DECLARE_LEVEL_API(warn, Types::Level::Warn)
    LOGGER_DECLARE_LEVEL_API(error, Types::Level::Error)
    LOGGER_DECLARE_LEVEL_API(fatal, Types::Level::Fatal)
#undef LOGGER_DECLARE_LEVEL_API

    /// @}

} // namespace GameWIP::Logger
