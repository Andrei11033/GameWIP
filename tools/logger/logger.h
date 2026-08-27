/// @file logger.h
/// @brief Public API for the Logger library.

#pragma once

#include "logger/config.h"
#include "logger/logger_export.h"

#include <chrono>
#include <cstddef>
#include <exception>
#include <format>
#include <iterator>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

/// @brief Process-wide asynchronous logging module for runtime diagnostics.
/// @details Normal logs are filtered, queue-backed, and optimized for the runtime hot path. Reports are
/// synchronous emergency diagnostics: they bypass filters and queue pressure, try every enabled emergency
/// channel, flush active normal sinks, and do not drain older asynchronous work.
namespace GameWIP::Logger
{

    /// @cond INTERNAL
    namespace Detail::Core
    {
        template <typename Source> constexpr decltype(auto) normalizeSource(Source &&source) noexcept
        {
            if constexpr (isSourceEnum<Source>)
            {
                return sourceId(source);
            }
            else
            {
                return std::forward<Source>(source);
            }
        }

        GAMEWIP_LOGGER_EXPORT void enqueuePreformattedMessage(Types::Level level, std::string_view source, std::string_view message) noexcept;
        GAMEWIP_LOGGER_EXPORT void enqueuePreformattedMessage(
            Types::Level level,
            std::string_view source,
            std::string_view message,
            bool alreadyTruncated) noexcept;
        GAMEWIP_LOGGER_EXPORT void enqueuePreformattedMessage(Types::Level level, Types::SourceId source, std::string_view message) noexcept;
        GAMEWIP_LOGGER_EXPORT void enqueuePreformattedMessage(
            Types::Level level,
            Types::SourceId source,
            std::string_view message,
            bool alreadyTruncated) noexcept;

        GAMEWIP_LOGGER_EXPORT Types::Report::Result reportPreformattedMessage(
            Types::Level level,
            std::string_view source,
            std::string_view message,
            bool showPopup,
            bool alreadyTruncated,
            const std::chrono::milliseconds *timeout) noexcept;
        GAMEWIP_LOGGER_EXPORT Types::Report::Result reportPreformattedMessage(
            Types::Level level,
            Types::SourceId source,
            std::string_view message,
            bool showPopup,
            bool alreadyTruncated,
            const std::chrono::milliseconds *timeout) noexcept;

        GAMEWIP_LOGGER_EXPORT void recordAllocationFailure() noexcept;
        GAMEWIP_LOGGER_EXPORT void recordFormatFailure() noexcept;
        GAMEWIP_LOGGER_EXPORT std::string &formatScratch();
        GAMEWIP_LOGGER_EXPORT std::size_t getMaxMessageLengthForFormatting();
        GAMEWIP_LOGGER_EXPORT Types::FormatPolicy getFormatPolicyForFormatting();
        GAMEWIP_LOGGER_EXPORT void releaseFormatScratchIfNeeded(std::string &scratch) noexcept;

        class FormatScratchLease
        {
        public:
            FormatScratchLease()
                : scratch_(formatScratch())
            {
            }
            ~FormatScratchLease() noexcept
            {
                releaseFormatScratchIfNeeded(scratch_);
            }
            FormatScratchLease(const FormatScratchLease &) = delete;
            FormatScratchLease &operator=(const FormatScratchLease &) = delete;
            [[nodiscard]] std::string &text() noexcept
            {
                return scratch_;
            }

        private:
            std::string &scratch_;
        };

        [[nodiscard]] inline std::size_t utf8PrefixBoundary(std::string_view text, std::size_t limit) noexcept
        {
            if (limit >= text.size())
            {
                return text.size();
            }
            std::size_t boundary = limit;
            while (boundary > 0)
            {
                const auto value = static_cast<unsigned char>(text[boundary]);
                if ((value & 0xC0u) != 0x80u)
                {
                    break;
                }
                --boundary;
            }
            return boundary;
        }

        inline void appendTruncationSuffix(std::string &scratch, std::size_t maxMessageLength)
        {
            constexpr std::string_view suffix = "... [truncated]";
            if (maxMessageLength == 0)
            {
                scratch.clear();
                return;
            }
            if (maxMessageLength <= suffix.size())
            {
                scratch.assign(suffix.substr(0, maxMessageLength));
                return;
            }
            scratch.append(suffix);
        }

        inline void truncateScratch(std::string &scratch, std::size_t maxMessageLength)
        {
            constexpr std::string_view suffix = "... [truncated]";
            if (maxMessageLength > suffix.size())
            {
                const std::size_t budget = maxMessageLength - suffix.size();
                scratch.resize(utf8PrefixBoundary(scratch, budget));
            }
            else
            {
                scratch.clear();
            }
            appendTruncationSuffix(scratch, maxMessageLength);
        }

        inline bool truncateScratchIfNeeded(std::string &scratch, std::size_t maxMessageLength)
        {
            if (scratch.size() <= maxMessageLength)
            {
                return false;
            }
            truncateScratch(scratch, maxMessageLength);
            return true;
        }

        class BoundedFormatIterator
        {
        public:
            using difference_type = std::ptrdiff_t;
            using value_type = char;
            using pointer = void;
            using reference = void;
            using iterator_category = std::output_iterator_tag;

            BoundedFormatIterator(std::string &output, std::size_t limit, std::size_t &written, bool &truncated) noexcept
                : output_(&output)
                , limit_(limit)
                , written_(&written)
                , truncated_(&truncated)
            {
            }
            BoundedFormatIterator &operator=(char value)
            {
                if (*written_ < limit_)
                {
                    output_->push_back(value);
                }
                else
                {
                    *truncated_ = true;
                }
                ++*written_;
                return *this;
            }
            BoundedFormatIterator &operator*() noexcept
            {
                return *this;
            }
            BoundedFormatIterator &operator++() noexcept
            {
                return *this;
            }
            BoundedFormatIterator operator++(int) noexcept
            {
                return *this;
            }

        private:
            std::string *output_ = nullptr;
            std::size_t limit_ = 0;
            std::size_t *written_ = nullptr;
            bool *truncated_ = nullptr;
        };

        template <typename Format, typename... Args>
        bool formatBounded(std::string &scratch, std::size_t maxMessageLength, Format format, Args &&...args)
        {
            scratch.clear();
            std::size_t written = 0;
            bool truncated = false;
            BoundedFormatIterator output(scratch, maxMessageLength, written, truncated);
            std::format_to(output, format, std::forward<Args>(args)...);
            if (!truncated)
            {
                return false;
            }
            truncateScratch(scratch, maxMessageLength);
            return true;
        }

        template <typename... Args>
        bool runtimeFormatBounded(std::string &scratch, std::size_t maxMessageLength, Types::RuntimeFormat format, Args &...args)
        {
            scratch.clear();
            std::size_t written = 0;
            bool truncated = false;
            BoundedFormatIterator output(scratch, maxMessageLength, written, truncated);
            std::vformat_to(output, format.text, std::make_format_args(args...));
            if (!truncated)
            {
                return false;
            }
            truncateScratch(scratch, maxMessageLength);
            return true;
        }

        template <typename Format, typename... Args>
        bool formatWithPolicy(std::string &scratch, std::size_t maxMessageLength, Format format, Args &&...args)
        {
            if (getFormatPolicyForFormatting() == Types::FormatPolicy::FastNormal)
            {
                scratch.clear();
                std::format_to(std::back_inserter(scratch), format, std::forward<Args>(args)...);
                return truncateScratchIfNeeded(scratch, maxMessageLength);
            }
            return formatBounded(scratch, maxMessageLength, format, std::forward<Args>(args)...);
        }

        template <typename... Args>
        bool runtimeFormatWithPolicy(std::string &scratch, std::size_t maxMessageLength, Types::RuntimeFormat format, Args &...args)
        {
            if (getFormatPolicyForFormatting() == Types::FormatPolicy::FastNormal)
            {
                scratch.clear();
                std::vformat_to(std::back_inserter(scratch), format.text, std::make_format_args(args...));
                return truncateScratchIfNeeded(scratch, maxMessageLength);
            }
            return runtimeFormatBounded(scratch, maxMessageLength, format, args...);
        }

        template <typename Source, typename... Args>
        void formatAndLog(Types::Level level, Source source, std::format_string<Args...> format, Args &&...args) noexcept
        {
            try
            {
                FormatScratchLease lease;
                std::string &scratch = lease.text();
                const bool truncated = formatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, std::forward<Args>(args)...);
                enqueuePreformattedMessage(level, source, scratch, truncated);
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
            }
            catch (...)
            {
                recordAllocationFailure();
            }
        }

        template <typename Source, typename... Args>
        void runtimeFormatAndLog(Types::Level level, Source source, Types::RuntimeFormat format, Args &...args) noexcept
        {
            try
            {
                FormatScratchLease lease;
                std::string &scratch = lease.text();
                const bool truncated = runtimeFormatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, args...);
                enqueuePreformattedMessage(level, source, scratch, truncated);
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
            }
            catch (...)
            {
                recordAllocationFailure();
            }
        }

        [[nodiscard]] inline Types::Report::Result reportFailure(IO::Types::ErrorCode code) noexcept
        {
            Types::Report::Result result;
            result.status = IO::makeStatus(code);
            return result;
        }

        template <typename Source, typename... Args>
        Types::Report::Result formatAndReport(
            Types::Level level,
            Source source,
            bool showPopup,
            const std::chrono::milliseconds *timeout,
            std::format_string<Args...> format,
            Args &&...args) noexcept
        {
            try
            {
                FormatScratchLease lease;
                std::string &scratch = lease.text();
                const bool truncated = formatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, std::forward<Args>(args)...);
                return reportPreformattedMessage(level, normalizeSource(source), scratch, showPopup, truncated, timeout);
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
                return reportFailure(IO::Types::ErrorCode::InvalidArgument);
            }
            catch (const std::bad_alloc &)
            {
                recordAllocationFailure();
                return reportFailure(IO::Types::ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                recordAllocationFailure();
                return reportFailure(IO::Types::ErrorCode::Unknown);
            }
        }

        template <typename Source, typename... Args>
        Types::Report::Result runtimeFormatAndReport(
            Types::Level level,
            Source source,
            bool showPopup,
            const std::chrono::milliseconds *timeout,
            Types::RuntimeFormat format,
            Args &...args) noexcept
        {
            try
            {
                FormatScratchLease lease;
                std::string &scratch = lease.text();
                const bool truncated = runtimeFormatWithPolicy(scratch, getMaxMessageLengthForFormatting(), format, args...);
                return reportPreformattedMessage(level, normalizeSource(source), scratch, showPopup, truncated, timeout);
            }
            catch (const std::format_error &)
            {
                recordFormatFailure();
                return reportFailure(IO::Types::ErrorCode::InvalidArgument);
            }
            catch (const std::bad_alloc &)
            {
                recordAllocationFailure();
                return reportFailure(IO::Types::ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                recordAllocationFailure();
                return reportFailure(IO::Types::ErrorCode::Unknown);
            }
        }
    } // namespace Detail::Core
    /// @endcond

    /// @brief Marks a format string as runtime-provided for Logger formatting overloads.
    constexpr Types::RuntimeFormat runtimeFormat(std::string_view format) noexcept
    {
        return {format};
    }

    // ------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------

    /// @name Lifecycle
    /// @{

    /// @brief Initializes Logger from a complete configuration.
    GAMEWIP_LOGGER_EXPORT Types::Init::Result init(const Types::Config &config) noexcept;
    /// @brief Initializes Logger with defaultConfig().
    GAMEWIP_LOGGER_EXPORT Types::Init::Result initDefault() noexcept;
    /// @brief Initializes a Console-only Logger at the selected minimum level.
    GAMEWIP_LOGGER_EXPORT Types::Init::Result initConsole(Types::Level minLevel = Types::Level::Info) noexcept;
    /// @brief Initializes a File-only Logger using a UTF-8 directory.
    GAMEWIP_LOGGER_EXPORT Types::Init::Result initFile(std::string_view directory = {}, Types::Level minLevel = Types::Level::Info) noexcept;
    /// @brief Stops Logger, drains accepted work, flushes and closes active sinks, and returns the first real failure.
    GAMEWIP_LOGGER_EXPORT IO::Types::Status shutdown() noexcept;
    /// @brief Drains accepted async work and flushes active sinks under an optional Logger-owned deadline.
    /// @details nullopt waits indefinitely, zero polls, positive durations are bounded, and negative durations are invalid.
    GAMEWIP_LOGGER_EXPORT Types::FlushResult flush(std::optional<std::chrono::milliseconds> timeout = std::nullopt) noexcept;

    /// @}

    // ------------------------------------------------------------
    // State queries and statistics
    // ------------------------------------------------------------

    /// @name State queries and statistics
    /// @{

    /// @brief Returns whether the worker currently accepts normal log records.
    GAMEWIP_LOGGER_EXPORT bool isRunning() noexcept;
    /// @brief Returns the configured minimum severity.
    GAMEWIP_LOGGER_EXPORT Types::Level getMinLevel();
    /// @brief Returns the currently effective normal-output mode.
    GAMEWIP_LOGGER_EXPORT Types::OutputMode getOutput();
    /// @brief Returns the active log file path as UTF-8, or an empty string when unavailable.
    GAMEWIP_LOGGER_EXPORT std::string getLogFilePath();
    /// @brief Returns effective queue, batching, and message limits.
    GAMEWIP_LOGGER_EXPORT Types::QueueLimits getQueueLimits();
    /// @brief Returns process-lifetime queue drops, unaffected by resetStats().
    GAMEWIP_LOGGER_EXPORT std::size_t getLifetimeDroppedLogCount() noexcept;
    /// @brief Returns a coherent Logger health snapshot.
    [[nodiscard]] GAMEWIP_LOGGER_EXPORT Types::Health::Snapshot getHealth();
    /// @brief Returns relaxed resettable statistics counters.
    GAMEWIP_LOGGER_EXPORT Types::Stats getStats();
    /// @brief Returns Logger-retained and available process-memory statistics.
    GAMEWIP_LOGGER_EXPORT Types::MemoryStats getMemoryStats();
    /// @brief Resets statistics counters without changing health state.
    GAMEWIP_LOGGER_EXPORT void resetStats();

    /// @}

    // ------------------------------------------------------------
    // Runtime filtering
    // ------------------------------------------------------------

    /// @name Runtime filtering
    /// @{

    /// @brief Applies the current severity-only normal-log gate.
    GAMEWIP_LOGGER_EXPORT bool shouldLog(Types::Level level) noexcept;
    /// @brief Applies the current severity-only gate for a string source.
    GAMEWIP_LOGGER_EXPORT bool shouldLog(Types::Level level, std::string_view source) noexcept;
    /// @brief Applies current severity and registered-source gates.
    GAMEWIP_LOGGER_EXPORT bool shouldLog(Types::Level level, Types::SourceId source) noexcept;

    /// @brief Applies current severity and source gates for an enum source.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    bool shouldLog(Types::Level level, Source source) noexcept
    {
        return shouldLog(level, Detail::Core::sourceId(source));
    }

    /// @brief Sets one registered source filter and returns direct operation status.
    GAMEWIP_LOGGER_EXPORT IO::Types::Status setSourceFilter(Types::SourceId source, bool enabled) noexcept;
    /// @brief Resets one registered source filter to the default enabled state.
    GAMEWIP_LOGGER_EXPORT IO::Types::Status resetSourceFilter(Types::SourceId source) noexcept;
    /// @brief Resets every registered source filter to the default enabled state.
    GAMEWIP_LOGGER_EXPORT IO::Types::Status resetSourceFilters() noexcept;
    /// @brief Sets one exact-level filter and returns direct operation status.
    GAMEWIP_LOGGER_EXPORT IO::Types::Status setLevelFilter(Types::Level level, bool enabled) noexcept;
    /// @brief Resets one exact-level filter to the default enabled state.
    GAMEWIP_LOGGER_EXPORT IO::Types::Status resetLevelFilter(Types::Level level) noexcept;
    /// @brief Resets every exact-level filter to the default enabled state.
    GAMEWIP_LOGGER_EXPORT IO::Types::Status resetLevelFilters() noexcept;

    /// @brief Sets one enum-source filter and returns direct operation status.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    IO::Types::Status setSourceFilter(Source source, bool enabled) noexcept
    {
        return setSourceFilter(Detail::Core::sourceId(source), enabled);
    }

    /// @brief Resets one enum-source filter to the default enabled state.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    IO::Types::Status resetSourceFilter(Source source) noexcept
    {
        return resetSourceFilter(Detail::Core::sourceId(source));
    }

    /// @}

    // ------------------------------------------------------------
    // Asynchronous logging
    // ------------------------------------------------------------

    /// @name Asynchronous logging
    /// @{

    /// @brief Queues a preformatted normal record with a UTF-8 string source.
    GAMEWIP_LOGGER_EXPORT void log(Types::Level level, std::string_view source, std::string_view message) noexcept;
    /// @brief Queues a preformatted normal record with a registered source.
    GAMEWIP_LOGGER_EXPORT void log(Types::Level level, Types::SourceId source, std::string_view message) noexcept;

    /// @brief Queues a preformatted normal record with an enum source.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    void log(Types::Level level, Source source, std::string_view message) noexcept
    {
        log(level, Detail::Core::sourceId(source), message);
    }

    /// @brief Formats and queues a normal record with a UTF-8 string source.
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    void log(Types::Level level, std::string_view source, std::format_string<Args...> format, Args &&...args) noexcept
    {
        if (shouldLog(level))
        {
            Detail::Core::formatAndLog(level, source, format, std::forward<Args>(args)...);
        }
    }

    /// @brief Formats and queues a normal record with a registered source.
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    void log(Types::Level level, Types::SourceId source, std::format_string<Args...> format, Args &&...args) noexcept
    {
        if (shouldLog(level, source))
        {
            Detail::Core::formatAndLog(level, source, format, std::forward<Args>(args)...);
        }
    }

    /// @brief Formats and queues a normal record with an enum source.
    template <typename Source, typename... Args>
        requires(Detail::Core::isSourceEnum<Source> && sizeof...(Args) > 0)
    void log(Types::Level level, Source source, std::format_string<Args...> format, Args &&...args) noexcept
    {
        log(level, Detail::Core::sourceId(source), format, std::forward<Args>(args)...);
    }

    /// @brief Runtime-formats and queues a normal record with a UTF-8 string source.
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    void log(Types::Level level, std::string_view source, Types::RuntimeFormat format, Args &&...args) noexcept
    {
        if (shouldLog(level))
        {
            Detail::Core::runtimeFormatAndLog(level, source, format, args...);
        }
    }

    /// @brief Runtime-formats and queues a normal record with a registered source.
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    void log(Types::Level level, Types::SourceId source, Types::RuntimeFormat format, Args &&...args) noexcept
    {
        if (shouldLog(level, source))
        {
            Detail::Core::runtimeFormatAndLog(level, source, format, args...);
        }
    }

    /// @brief Runtime-formats and queues a normal record with an enum source.
    template <typename Source, typename... Args>
        requires(Detail::Core::isSourceEnum<Source> && sizeof...(Args) > 0)
    void log(Types::Level level, Source source, Types::RuntimeFormat format, Args &&...args) noexcept
    {
        log(level, Detail::Core::sourceId(source), format, std::forward<Args>(args)...);
    }

    /// @brief Declares one fixed-severity family of normal logging overloads.
#define GAMEWIP_LOGGER_DECLARE_LEVEL_API(name, levelValue) \
    GAMEWIP_LOGGER_EXPORT void name(std::string_view source, std::string_view message) noexcept; \
    GAMEWIP_LOGGER_EXPORT void name(Types::SourceId source, std::string_view message) noexcept; \
    template <typename Source> \
        requires(Detail::Core::isSourceEnum<Source>) \
    void name(Source source, std::string_view message) noexcept \
    { \
        name(Detail::Core::sourceId(source), message); \
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

    GAMEWIP_LOGGER_DECLARE_LEVEL_API(trace, Types::Level::Trace)
    GAMEWIP_LOGGER_DECLARE_LEVEL_API(debug, Types::Level::Debug)
    GAMEWIP_LOGGER_DECLARE_LEVEL_API(info, Types::Level::Info)
    GAMEWIP_LOGGER_DECLARE_LEVEL_API(warn, Types::Level::Warn)
    GAMEWIP_LOGGER_DECLARE_LEVEL_API(error, Types::Level::Error)
    GAMEWIP_LOGGER_DECLARE_LEVEL_API(fatal, Types::Level::Fatal)
#undef GAMEWIP_LOGGER_DECLARE_LEVEL_API

    /// @}

    // ------------------------------------------------------------
    // Synchronous reporting
    // ------------------------------------------------------------

    /// @name Synchronous reporting
    /// @{

    /// @brief Synchronously reports a preformatted diagnostic through eligible emergency channels.
    GAMEWIP_LOGGER_EXPORT Types::Report::Result report(Types::Level level, std::string_view source, std::string_view message) noexcept;
    /// @brief Synchronously reports a preformatted diagnostic under a bounded deadline.
    GAMEWIP_LOGGER_EXPORT Types::Report::Result report(
        Types::Level level,
        std::string_view source,
        std::chrono::milliseconds timeout,
        std::string_view message) noexcept;
    /// @brief Synchronously reports a preformatted diagnostic with a registered source.
    GAMEWIP_LOGGER_EXPORT Types::Report::Result report(Types::Level level, Types::SourceId source, std::string_view message) noexcept;
    /// @brief Synchronously reports a registered-source diagnostic under a bounded deadline.
    GAMEWIP_LOGGER_EXPORT Types::Report::Result report(
        Types::Level level,
        Types::SourceId source,
        std::chrono::milliseconds timeout,
        std::string_view message) noexcept;

    /// @brief Synchronously reports a preformatted diagnostic with an enum source.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    Types::Report::Result report(Types::Level level, Source source, std::string_view message) noexcept
    {
        return report(level, Detail::Core::sourceId(source), message);
    }

    /// @brief Synchronously reports an enum-source diagnostic under a bounded deadline.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    Types::Report::Result report(Types::Level level, Source source, std::chrono::milliseconds timeout, std::string_view message) noexcept
    {
        return report(level, Detail::Core::sourceId(source), timeout, message);
    }

    /// @brief Formats and synchronously reports a diagnostic.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result report(Types::Level level, Source source, std::format_string<Args...> format, Args &&...args) noexcept
    {
        return Detail::Core::formatAndReport(level, source, false, nullptr, format, std::forward<Args>(args)...);
    }

    /// @brief Formats and synchronously reports a diagnostic under a bounded deadline.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result report(
        Types::Level level,
        Source source,
        std::chrono::milliseconds timeout,
        std::format_string<Args...> format,
        Args &&...args) noexcept
    {
        return Detail::Core::formatAndReport(level, source, false, &timeout, format, std::forward<Args>(args)...);
    }

    /// @brief Runtime-formats and synchronously reports a diagnostic.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result report(Types::Level level, Source source, Types::RuntimeFormat format, Args &&...args) noexcept
    {
        return Detail::Core::runtimeFormatAndReport(level, source, false, nullptr, format, args...);
    }

    /// @brief Runtime-formats and synchronously reports a diagnostic under a bounded deadline.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::Report::Result report(
        Types::Level level,
        Source source,
        std::chrono::milliseconds timeout,
        Types::RuntimeFormat format,
        Args &&...args) noexcept
    {
        return Detail::Core::runtimeFormatAndReport(level, source, false, &timeout, format, args...);
    }

    /// @brief Synchronously reports a preformatted Error with a UTF-8 string source.
    GAMEWIP_LOGGER_EXPORT Types::Report::Result reportError(std::string_view source, std::string_view message) noexcept;
    /// @brief Synchronously reports a preformatted Error under a bounded deadline.
    GAMEWIP_LOGGER_EXPORT Types::Report::Result reportError(
        std::string_view source,
        std::chrono::milliseconds timeout,
        std::string_view message) noexcept;
    /// @brief Synchronously reports a preformatted Error with a registered source.
    GAMEWIP_LOGGER_EXPORT Types::Report::Result reportError(Types::SourceId source, std::string_view message) noexcept;
    /// @brief Synchronously reports a registered-source Error under a bounded deadline.
    GAMEWIP_LOGGER_EXPORT Types::Report::Result reportError(
        Types::SourceId source,
        std::chrono::milliseconds timeout,
        std::string_view message) noexcept;

    /// @brief Synchronously reports a preformatted Fatal diagnostic and optional popup.
    GAMEWIP_LOGGER_EXPORT Types::Report::Result reportFatal(std::string_view source, std::string_view message) noexcept;
    /// @brief Synchronously reports a preformatted Fatal diagnostic under a bounded deadline.
    GAMEWIP_LOGGER_EXPORT Types::Report::Result reportFatal(
        std::string_view source,
        std::chrono::milliseconds timeout,
        std::string_view message) noexcept;
    /// @brief Synchronously reports a registered-source Fatal diagnostic and optional popup.
    GAMEWIP_LOGGER_EXPORT Types::Report::Result reportFatal(Types::SourceId source, std::string_view message) noexcept;
    /// @brief Synchronously reports a registered-source Fatal diagnostic under a bounded deadline.
    GAMEWIP_LOGGER_EXPORT Types::Report::Result reportFatal(
        Types::SourceId source,
        std::chrono::milliseconds timeout,
        std::string_view message) noexcept;

    /// @brief Synchronously reports a preformatted Error with an enum source.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    Types::Report::Result reportError(Source source, std::string_view message) noexcept
    {
        return reportError(Detail::Core::sourceId(source), message);
    }
    /// @brief Synchronously reports an enum-source Error under a bounded deadline.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    Types::Report::Result reportError(Source source, std::chrono::milliseconds timeout, std::string_view message) noexcept
    {
        return reportError(Detail::Core::sourceId(source), timeout, message);
    }
    /// @brief Synchronously reports a preformatted Fatal diagnostic with an enum source.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    Types::Report::Result reportFatal(Source source, std::string_view message) noexcept
    {
        return reportFatal(Detail::Core::sourceId(source), message);
    }
    /// @brief Synchronously reports an enum-source Fatal diagnostic under a bounded deadline.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    Types::Report::Result reportFatal(Source source, std::chrono::milliseconds timeout, std::string_view message) noexcept
    {
        return reportFatal(Detail::Core::sourceId(source), timeout, message);
    }

    /// @brief Declares formatted fixed-severity report overloads.
#define GAMEWIP_LOGGER_DECLARE_FORMATTED_REPORT(name, levelValue, popupValue) \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    Types::Report::Result name(Source source, std::format_string<Args...> format, Args &&...args) noexcept \
    { \
        return Detail::Core::formatAndReport(levelValue, source, popupValue, nullptr, format, std::forward<Args>(args)...); \
    } \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    Types::Report::Result name(Source source, std::chrono::milliseconds timeout, std::format_string<Args...> format, Args &&...args) noexcept \
    { \
        return Detail::Core::formatAndReport(levelValue, source, popupValue, &timeout, format, std::forward<Args>(args)...); \
    } \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    Types::Report::Result name(Source source, Types::RuntimeFormat format, Args &&...args) noexcept \
    { \
        return Detail::Core::runtimeFormatAndReport(levelValue, source, popupValue, nullptr, format, args...); \
    } \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    Types::Report::Result name(Source source, std::chrono::milliseconds timeout, Types::RuntimeFormat format, Args &&...args) noexcept \
    { \
        return Detail::Core::runtimeFormatAndReport(levelValue, source, popupValue, &timeout, format, args...); \
    }

    GAMEWIP_LOGGER_DECLARE_FORMATTED_REPORT(reportError, Types::Level::Error, false)
    GAMEWIP_LOGGER_DECLARE_FORMATTED_REPORT(reportFatal, Types::Level::Fatal, true)
#undef GAMEWIP_LOGGER_DECLARE_FORMATTED_REPORT

    /// @}

    // ------------------------------------------------------------
    // Fatal termination
    // ------------------------------------------------------------

    /// @name Fatal termination
    /// @{

    /// @brief Reports a Fatal diagnostic and then calls std::terminate().
    [[noreturn]] GAMEWIP_LOGGER_EXPORT void fatalTerminate(std::string_view source, std::string_view message) noexcept;
    /// @brief Reports a registered-source Fatal diagnostic and then calls std::terminate().
    [[noreturn]] GAMEWIP_LOGGER_EXPORT void fatalTerminate(Types::SourceId source, std::string_view message) noexcept;
    /// @brief Reports a Fatal diagnostic under a bounded deadline and then calls std::terminate().
    [[noreturn]] GAMEWIP_LOGGER_EXPORT void fatalTerminate(
        std::string_view source,
        std::chrono::milliseconds timeout,
        std::string_view message) noexcept;
    /// @brief Reports a registered-source Fatal diagnostic under a bounded deadline and then calls std::terminate().
    [[noreturn]] GAMEWIP_LOGGER_EXPORT void fatalTerminate(
        Types::SourceId source,
        std::chrono::milliseconds timeout,
        std::string_view message) noexcept;

    /// @brief Reports an enum-source Fatal diagnostic and then calls std::terminate().
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    [[noreturn]] void fatalTerminate(Source source, std::string_view message) noexcept
    {
        fatalTerminate(Detail::Core::sourceId(source), message);
    }
    /// @brief Reports an enum-source Fatal diagnostic under a deadline and then terminates.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    [[noreturn]] void fatalTerminate(Source source, std::chrono::milliseconds timeout, std::string_view message) noexcept
    {
        fatalTerminate(Detail::Core::sourceId(source), timeout, message);
    }

    /// @brief Formats a Fatal diagnostic, reports it, and then calls std::terminate().
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    [[noreturn]] void fatalTerminate(Source source, std::format_string<Args...> format, Args &&...args) noexcept
    {
        static_cast<void>(Detail::Core::formatAndReport(Types::Level::Fatal, source, true, nullptr, format, std::forward<Args>(args)...));
        std::terminate();
    }
    /// @brief Formats and reports a Fatal diagnostic under a deadline, then terminates.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    [[noreturn]] void fatalTerminate(Source source, std::chrono::milliseconds timeout, std::format_string<Args...> format, Args &&...args) noexcept
    {
        static_cast<void>(Detail::Core::formatAndReport(Types::Level::Fatal, source, true, &timeout, format, std::forward<Args>(args)...));
        std::terminate();
    }
    /// @brief Runtime-formats a Fatal diagnostic, reports it, and then terminates.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    [[noreturn]] void fatalTerminate(Source source, Types::RuntimeFormat format, Args &&...args) noexcept
    {
        static_cast<void>(Detail::Core::runtimeFormatAndReport(Types::Level::Fatal, source, true, nullptr, format, args...));
        std::terminate();
    }
    /// @brief Runtime-formats and reports a Fatal diagnostic under a deadline, then terminates.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    [[noreturn]] void fatalTerminate(Source source, std::chrono::milliseconds timeout, Types::RuntimeFormat format, Args &&...args) noexcept
    {
        static_cast<void>(Detail::Core::runtimeFormatAndReport(Types::Level::Fatal, source, true, &timeout, format, args...));
        std::terminate();
    }

    /// @}

    // ------------------------------------------------------------
    // Debugger output
    // ------------------------------------------------------------

    /// @name Debugger output
    /// @{

    /// @brief Writes one validated UTF-8 diagnostic directly to the debugger channel.
    GAMEWIP_LOGGER_EXPORT IO::Types::Status writeDebugOutput(Types::Level level, std::string_view source, std::string_view message) noexcept;

    /// @}
} // namespace GameWIP::Logger
