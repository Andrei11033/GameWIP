/// @file logger.h
/// @brief Public API for the Logger library.

#pragma once

#include "io/io.h"
#include "logger/logger_export.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <iterator>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

/// @brief Process-wide asynchronous logging module for runtime diagnostics.
/// @details Normal logs are filtered, queue-backed, and optimized for the runtime hot path. Reports are
/// synchronous emergency diagnostics: they bypass filters and queue pressure, try every enabled emergency
/// channel, flush active normal sinks, and do not drain older asynchronous work.
namespace GameWIP::Logger
{
    namespace Types
    {
        /// @brief Severity assigned to a log record or report.
        enum class Level : std::uint8_t
        {
            Trace,
            Debug,
            Info,
            Warn,
            Error,
            Fatal
        };

        /// @brief Enabled normal-output sink combination.
        enum class OutputMode : std::uint8_t
        {
            None,
            Console,
            File,
            Both
        };

        /// @brief Formatting strategy used before a message enters Logger-owned storage.
        enum class FormatPolicy : std::uint8_t
        {
            StrictBounded,
            FastNormal
        };

        /// @brief Stable numeric identifier for a registered source.
        using SourceId = std::uint32_t;

        /// @brief Associates a SourceId with its UTF-8 display name during initialization.
        struct SourceDefinition
        {
            SourceId id = 0;            ///< Source identifier.
            std::string_view name = {}; ///< Valid non-empty UTF-8 display name.
        };

        /// @brief Initial enabled state for one registered source.
        struct SourceFilter
        {
            SourceId source = 0; ///< Registered source to configure.
            bool enabled = true; ///< Whether the source is initially enabled.
        };

        /// @brief Initial enabled state for one exact severity level.
        struct LevelFilter
        {
            Level level = Level::Trace; ///< Exact level to configure.
            bool enabled = true;        ///< Whether the level is initially enabled.
        };

        /// @brief Explicit wrapper for a runtime-provided format string.
        struct RuntimeFormat
        {
            std::string_view text = {}; ///< Runtime format text.
        };

        /// @brief Complete Logger initialization configuration.
        struct Config
        {
            OutputMode output = OutputMode::Both;                    ///< Requested normal-output sinks.
            Level minLevel = Level::Info;                            ///< Minimum accepted severity.
            std::size_t maxQueueSize = 1024;                         ///< Soft queue limit.
            double hardQueueMultiplier = 1.25;                       ///< Hard-limit multiplier applied to the soft limit.
            std::size_t maxMessageLength = 4096;                     ///< Maximum retained UTF-8 message bytes.
            FormatPolicy formatPolicy = FormatPolicy::StrictBounded; ///< Formatting memory policy.
            std::size_t inlineMessageCapacity = 256;                 ///< Inline bytes reserved per queued message.
            std::size_t workerBatchSize = 256;                       ///< Maximum records drained per worker batch.
            std::string_view logDirectory = "logs";                  ///< UTF-8 file-output directory.
            bool fallbackToConsoleOnFileFailure = true;              ///< Allow File to fall back to Console.
            std::span<const SourceDefinition> sources = {};          ///< Registered source definitions copied by init().
            std::span<const SourceFilter> sourceFilters = {};        ///< Initial per-source filters.
            std::span<const LevelFilter> levelFilters = {};          ///< Initial exact-level filters.
            bool enableConsoleColor = true;                          ///< Enable supported console styling.
            bool enableDebugOutput = true;                           ///< Mirror reports to the debugger channel.
            bool enableFatalPopup = true;                            ///< Enable the fatal-report popup channel.
            bool flushFileEveryBatch = false;                        ///< Flush File after each worker batch.
            bool flushConsoleEveryWrite = false;                     ///< Flush Console after each normal write.
            bool releaseMessageMemoryAfterWrite = false;             ///< Release excess message scratch after writes.
            bool releaseStorageOnShutdown = true;                    ///< Release queue storage during shutdown.
        };

        /// @brief Effective queue and message limits selected by initialization.
        struct QueueLimits
        {
            std::size_t softQueueSize = 0;         ///< Low-priority drop threshold.
            std::size_t hardQueueSize = 0;         ///< Absolute queue limit.
            double hardQueueMultiplier = 1.0;      ///< Effective hard-limit multiplier.
            std::size_t maxMessageLength = 0;      ///< Effective retained message byte limit.
            std::size_t inlineMessageCapacity = 0; ///< Effective inline message bytes per entry.
            std::size_t workerBatchSize = 0;       ///< Effective worker batch size.
        };

        /// @brief Resettable relaxed Logger counters.
        struct Stats
        {
            std::size_t queued = 0;             ///< Records accepted into the async queue.
            std::size_t written = 0;            ///< Records delivered to at least one normal sink.
            std::size_t queueDropsSoft = 0;     ///< Low-priority records dropped at the soft limit.
            std::size_t queueDropsHard = 0;     ///< Records dropped at the hard limit.
            std::size_t allocationFailures = 0; ///< Contained allocation failures.
            std::size_t fileWriteFailures = 0;  ///< File write or flush failures.
            std::size_t unknownSourceUses = 0;  ///< Delivered records using an unknown SourceId.
            std::size_t formatFailures = 0;     ///< Contained formatting failures.
            std::size_t truncated = 0;          ///< Delivered or queued messages truncated by Logger.
            std::size_t peakQueueDepth = 0;     ///< Highest observed queue depth since reset.
        };

        /// @brief Snapshot of memory retained by Logger and the process.
        struct MemoryStats
        {
            std::size_t loggerRetainedBytes = 0;         ///< Estimated total Logger-retained bytes.
            std::size_t queueStorageBytes = 0;           ///< Ring and worker-batch object storage.
            std::size_t messageArenaBytes = 0;           ///< Inline message arena storage.
            std::size_t sourceRegistryBytes = 0;         ///< Registered-source storage.
            std::size_t entryTextHeapCapacityBytes = 0;  ///< Retained overflow string capacity when observable.
            bool entryTextHeapCapacityAvailable = false; ///< Whether entry heap capacity was safely observable.
            std::size_t processWorkingSetBytes = 0;      ///< Process working-set bytes when available.
            std::size_t processPrivateBytes = 0;         ///< Process private bytes when available.
            bool processMemoryAvailable = false;         ///< Whether process memory metrics were available.
        };

        /// @brief Final lifecycle state produced by init().
        enum class InitOutcome : std::uint8_t
        {
            Started,
            Disabled
        };

        /// @brief Recoverable configuration or storage adjustments made by init().
        enum class InitAdjustment : std::uint32_t
        {
            None = 0,
            QueueLimitsAdjusted = 1u << 0u,
            MessageLengthAdjusted = 1u << 1u,
            InlineCapacityAdjusted = 1u << 2u,
            WorkerBatchAdjusted = 1u << 3u,
            QueueStorageFallback = 1u << 4u
        };

        /// @brief Combines initialization-adjustment flags.
        [[nodiscard]] constexpr InitAdjustment operator|(InitAdjustment left, InitAdjustment right) noexcept
        {
            return static_cast<InitAdjustment>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
        }

        /// @brief Adds an initialization-adjustment flag in place.
        constexpr InitAdjustment &operator|=(InitAdjustment &left, InitAdjustment right) noexcept
        {
            left = left | right;
            return left;
        }

        /// @brief Tests whether an initialization adjustment contains a flag.
        [[nodiscard]] constexpr bool hasAdjustment(InitAdjustment value, InitAdjustment flag) noexcept
        {
            return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
        }

        /// @brief Rich initialization result separating operation status from final lifecycle state.
        struct InitResult
        {
            IO::Types::Status status;                          ///< Overall operational status.
            InitOutcome outcome = InitOutcome::Disabled;       ///< Final lifecycle outcome.
            InitAdjustment adjustments = InitAdjustment::None; ///< Recoverable changes made by init().
            OutputMode requestedOutput = OutputMode::None;     ///< Caller-requested output mode.
            OutputMode effectiveOutput = OutputMode::None;     ///< Output mode actually left active.
            IO::Types::Status outputSetupStatus;               ///< Direct File/output setup status.
        };

        /// @brief Completion state for a Logger-owned flush deadline.
        enum class FlushOutcome : std::uint8_t
        {
            Completed,
            TimedOut
        };

        /// @brief Result of draining queued logs and flushing active sinks.
        struct FlushResult
        {
            IO::Types::Status status;                       ///< Real IO or operation failure, if any.
            FlushOutcome outcome = FlushOutcome::Completed; ///< Independent deadline outcome.
        };

        /// @brief Completion state for a synchronous report deadline.
        enum class ReportOutcome : std::uint8_t
        {
            Completed,
            TimedOut
        };

        /// @brief Fraction of eligible emergency channels that accepted a report.
        enum class ReportDelivery : std::uint8_t
        {
            None,
            Partial,
            Complete
        };

        /// @brief Result of a synchronous emergency report attempt.
        struct ReportResult
        {
            IO::Types::Status status;                         ///< First real operation failure, if any.
            ReportOutcome outcome = ReportOutcome::Completed; ///< Independent deadline outcome.
            ReportDelivery delivery = ReportDelivery::None;   ///< Delivery across eligible channels.
        };

        /// @brief Current aggregate Logger health for the active initialization epoch.
        enum class HealthState : std::uint8_t
        {
            Healthy,
            Degraded,
            Disabled
        };

        /// @brief Channel associated with the most recent health failure.
        enum class FailureSource : std::uint8_t
        {
            None,
            Console,
            File,
            DebugOutput,
            FatalPopup,
            TimeConversion
        };

        /// @brief Coherent snapshot of Logger health and failure metadata.
        struct HealthSnapshot
        {
            HealthState state = HealthState::Disabled;                      ///< Aggregate health state.
            OutputMode effectiveOutput = OutputMode::None;                  ///< Currently usable normal sinks.
            FailureSource lastFailureSource = FailureSource::None;          ///< Most recent failed channel.
            IO::Types::ErrorCode lastError = IO::Types::ErrorCode::Success; ///< Most recent portable error.
            std::int64_t lastNativeCode = 0;                                ///< Associated backend-native error.
            std::uint64_t failureCount = 0;                                 ///< Failures in the current init epoch.
        };
    } // namespace Types

    /// @cond INTERNAL
    namespace Detail::Core
    {
        template <typename Enum>
        inline constexpr bool isSourceEnum =
            std::is_enum_v<std::remove_cvref_t<Enum>> && !std::is_same_v<std::remove_cvref_t<Enum>, Types::Level> &&
            !std::is_same_v<std::remove_cvref_t<Enum>, Types::OutputMode> && !std::is_same_v<std::remove_cvref_t<Enum>, Types::FormatPolicy> &&
            !std::is_same_v<std::remove_cvref_t<Enum>, Types::InitOutcome> && !std::is_same_v<std::remove_cvref_t<Enum>, Types::InitAdjustment> &&
            !std::is_same_v<std::remove_cvref_t<Enum>, Types::FlushOutcome> && !std::is_same_v<std::remove_cvref_t<Enum>, Types::ReportOutcome> &&
            !std::is_same_v<std::remove_cvref_t<Enum>, Types::ReportDelivery> && !std::is_same_v<std::remove_cvref_t<Enum>, Types::HealthState> &&
            !std::is_same_v<std::remove_cvref_t<Enum>, Types::FailureSource>;

        template <typename Enum>
            requires(isSourceEnum<Enum>)
        constexpr Types::SourceId sourceId(Enum value) noexcept
        {
            using Underlying = std::underlying_type_t<std::remove_cvref_t<Enum>>;
            static_assert(std::is_unsigned_v<Underlying>, "Logger source enums must use an unsigned underlying type.");
            static_assert(sizeof(Underlying) <= sizeof(Types::SourceId), "Logger source enum values must fit in SourceId.");
            return static_cast<Types::SourceId>(static_cast<Underlying>(value));
        }

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

        GAMEWIP_LOGGER_EXPORT void enqueuePreformattedMessage(Types::Level level, std::string_view source, std::string_view message);
        GAMEWIP_LOGGER_EXPORT void enqueuePreformattedMessage(
            Types::Level level,
            std::string_view source,
            std::string_view message,
            bool alreadyTruncated);
        GAMEWIP_LOGGER_EXPORT void enqueuePreformattedMessage(Types::Level level, Types::SourceId source, std::string_view message);
        GAMEWIP_LOGGER_EXPORT void enqueuePreformattedMessage(
            Types::Level level,
            Types::SourceId source,
            std::string_view message,
            bool alreadyTruncated);

        GAMEWIP_LOGGER_EXPORT Types::ReportResult reportPreformattedMessage(
            Types::Level level,
            std::string_view source,
            std::string_view message,
            bool showPopup,
            bool alreadyTruncated,
            const std::chrono::milliseconds *timeout);
        GAMEWIP_LOGGER_EXPORT Types::ReportResult reportPreformattedMessage(
            Types::Level level,
            Types::SourceId source,
            std::string_view message,
            bool showPopup,
            bool alreadyTruncated,
            const std::chrono::milliseconds *timeout);

        GAMEWIP_LOGGER_EXPORT void recordAllocationFailure();
        GAMEWIP_LOGGER_EXPORT void recordFormatFailure();
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
        void formatAndLog(Types::Level level, Source source, std::format_string<Args...> format, Args &&...args)
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
        void runtimeFormatAndLog(Types::Level level, Source source, Types::RuntimeFormat format, Args &...args)
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

        [[nodiscard]] inline Types::ReportResult reportFailure(IO::Types::ErrorCode code)
        {
            Types::ReportResult result;
            result.status = IO::makeStatus(code);
            return result;
        }

        template <typename Source, typename... Args>
        Types::ReportResult formatAndReport(
            Types::Level level,
            Source source,
            bool showPopup,
            const std::chrono::milliseconds *timeout,
            std::format_string<Args...> format,
            Args &&...args)
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
        Types::ReportResult runtimeFormatAndReport(
            Types::Level level,
            Source source,
            bool showPopup,
            const std::chrono::milliseconds *timeout,
            Types::RuntimeFormat format,
            Args &...args)
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

    /// @brief Creates a registered-source definition from an unsigned enum value and UTF-8 name.
    template <typename Enum>
        requires(Detail::Core::isSourceEnum<Enum>)
    constexpr Types::SourceDefinition defineSource(Enum value, std::string_view name) noexcept
    {
        return {Detail::Core::sourceId(value), name};
    }

    /// @brief Marks a format string as runtime-provided for Logger formatting overloads.
    constexpr Types::RuntimeFormat runtimeFormat(std::string_view format) noexcept
    {
        return {format};
    }

    /// @brief Initializes Logger from a complete configuration.
    GAMEWIP_LOGGER_EXPORT Types::InitResult init(const Types::Config &config);
    /// @brief Returns the balanced default Logger configuration.
    GAMEWIP_LOGGER_EXPORT Types::Config defaultConfig();
    /// @brief Returns a configuration favoring lower retained memory.
    GAMEWIP_LOGGER_EXPORT Types::Config lowMemoryConfig();
    /// @brief Returns a configuration favoring logging throughput.
    GAMEWIP_LOGGER_EXPORT Types::Config throughputConfig();
    /// @brief Initializes Logger with defaultConfig().
    GAMEWIP_LOGGER_EXPORT Types::InitResult initDefault();
    /// @brief Initializes a Console-only Logger at the selected minimum level.
    GAMEWIP_LOGGER_EXPORT Types::InitResult initConsole(Types::Level minLevel = Types::Level::Info);
    /// @brief Initializes a File-only Logger using a UTF-8 directory.
    GAMEWIP_LOGGER_EXPORT Types::InitResult initFile(std::string_view directory = {}, Types::Level minLevel = Types::Level::Info);
    /// @brief Stops Logger, drains accepted work, flushes and closes active sinks, and returns the first real failure.
    GAMEWIP_LOGGER_EXPORT IO::Types::Status shutdown();
    /// @brief Drains accepted async work and flushes active sinks under an optional Logger-owned deadline.
    /// @details nullopt waits indefinitely, zero polls, positive durations are bounded, and negative durations are invalid.
    GAMEWIP_LOGGER_EXPORT Types::FlushResult flush(std::optional<std::chrono::milliseconds> timeout = std::nullopt);

    /// @brief Returns whether the worker currently accepts normal log records.
    GAMEWIP_LOGGER_EXPORT bool isRunning();
    /// @brief Returns the configured minimum severity.
    GAMEWIP_LOGGER_EXPORT Types::Level getMinLevel();
    /// @brief Returns the currently effective normal-output mode.
    GAMEWIP_LOGGER_EXPORT Types::OutputMode getOutput();
    /// @brief Returns the active log file path as UTF-8, or an empty string when unavailable.
    GAMEWIP_LOGGER_EXPORT std::string getLogFilePath();
    /// @brief Returns effective queue, batching, and message limits.
    GAMEWIP_LOGGER_EXPORT Types::QueueLimits getQueueLimits();
    /// @brief Returns process-lifetime queue drops, unaffected by resetStats().
    GAMEWIP_LOGGER_EXPORT std::size_t getLifetimeDroppedLogCount();
    /// @brief Returns a coherent Logger health snapshot.
    [[nodiscard]] GAMEWIP_LOGGER_EXPORT Types::HealthSnapshot getHealth();
    /// @brief Returns relaxed resettable statistics counters.
    GAMEWIP_LOGGER_EXPORT Types::Stats getStats();
    /// @brief Returns Logger-retained and available process-memory statistics.
    GAMEWIP_LOGGER_EXPORT Types::MemoryStats getMemoryStats();
    /// @brief Resets statistics counters without changing health state.
    GAMEWIP_LOGGER_EXPORT void resetStats();

    /// @brief Applies the current severity-only normal-log gate.
    GAMEWIP_LOGGER_EXPORT bool shouldLog(Types::Level level);
    /// @brief Applies the current severity-only gate for a string source.
    GAMEWIP_LOGGER_EXPORT bool shouldLog(Types::Level level, std::string_view source);
    /// @brief Applies current severity and registered-source gates.
    GAMEWIP_LOGGER_EXPORT bool shouldLog(Types::Level level, Types::SourceId source);

    /// @brief Applies current severity and source gates for an enum source.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    bool shouldLog(Types::Level level, Source source)
    {
        return shouldLog(level, Detail::Core::sourceId(source));
    }

    /// @brief Sets one registered source filter and returns direct operation status.
    GAMEWIP_LOGGER_EXPORT IO::Types::Status setSourceFilter(Types::SourceId source, bool enabled);
    /// @brief Enables one registered source and returns direct operation status.
    GAMEWIP_LOGGER_EXPORT IO::Types::Status clearSourceFilter(Types::SourceId source);
    /// @brief Enables every registered source.
    GAMEWIP_LOGGER_EXPORT IO::Types::Status clearSourceFilters();
    /// @brief Sets one exact-level filter and returns direct operation status.
    GAMEWIP_LOGGER_EXPORT IO::Types::Status setLevelFilter(Types::Level level, bool enabled);
    /// @brief Enables one exact severity level.
    GAMEWIP_LOGGER_EXPORT IO::Types::Status clearLevelFilter(Types::Level level);
    /// @brief Enables every exact severity level.
    GAMEWIP_LOGGER_EXPORT IO::Types::Status clearLevelFilters();

    /// @brief Sets one enum-source filter and returns direct operation status.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    IO::Types::Status setSourceFilter(Source source, bool enabled)
    {
        return setSourceFilter(Detail::Core::sourceId(source), enabled);
    }

    /// @brief Enables one enum source and returns direct operation status.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    IO::Types::Status clearSourceFilter(Source source)
    {
        return clearSourceFilter(Detail::Core::sourceId(source));
    }

    /// @brief Queues a preformatted normal record with a UTF-8 string source.
    GAMEWIP_LOGGER_EXPORT void log(Types::Level level, std::string_view source, std::string_view message);
    /// @brief Queues a preformatted normal record with a registered source.
    GAMEWIP_LOGGER_EXPORT void log(Types::Level level, Types::SourceId source, std::string_view message);

    /// @brief Queues a preformatted normal record with an enum source.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    void log(Types::Level level, Source source, std::string_view message)
    {
        log(level, Detail::Core::sourceId(source), message);
    }

    /// @brief Formats and queues a normal record with a UTF-8 string source.
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    void log(Types::Level level, std::string_view source, std::format_string<Args...> format, Args &&...args)
    {
        if (shouldLog(level))
        {
            Detail::Core::formatAndLog(level, source, format, std::forward<Args>(args)...);
        }
    }

    /// @brief Formats and queues a normal record with a registered source.
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    void log(Types::Level level, Types::SourceId source, std::format_string<Args...> format, Args &&...args)
    {
        if (shouldLog(level, source))
        {
            Detail::Core::formatAndLog(level, source, format, std::forward<Args>(args)...);
        }
    }

    /// @brief Formats and queues a normal record with an enum source.
    template <typename Source, typename... Args>
        requires(Detail::Core::isSourceEnum<Source> && sizeof...(Args) > 0)
    void log(Types::Level level, Source source, std::format_string<Args...> format, Args &&...args)
    {
        log(level, Detail::Core::sourceId(source), format, std::forward<Args>(args)...);
    }

    /// @brief Runtime-formats and queues a normal record with a UTF-8 string source.
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    void log(Types::Level level, std::string_view source, Types::RuntimeFormat format, Args &&...args)
    {
        if (shouldLog(level))
        {
            Detail::Core::runtimeFormatAndLog(level, source, format, args...);
        }
    }

    /// @brief Runtime-formats and queues a normal record with a registered source.
    template <typename... Args>
        requires(sizeof...(Args) > 0)
    void log(Types::Level level, Types::SourceId source, Types::RuntimeFormat format, Args &&...args)
    {
        if (shouldLog(level, source))
        {
            Detail::Core::runtimeFormatAndLog(level, source, format, args...);
        }
    }

    /// @brief Runtime-formats and queues a normal record with an enum source.
    template <typename Source, typename... Args>
        requires(Detail::Core::isSourceEnum<Source> && sizeof...(Args) > 0)
    void log(Types::Level level, Source source, Types::RuntimeFormat format, Args &&...args)
    {
        log(level, Detail::Core::sourceId(source), format, std::forward<Args>(args)...);
    }

    /// @brief Declares one fixed-severity family of normal logging overloads.
#define GAMEWIP_LOGGER_DECLARE_LEVEL_API(name, levelValue) \
    GAMEWIP_LOGGER_EXPORT void name(std::string_view source, std::string_view message); \
    GAMEWIP_LOGGER_EXPORT void name(Types::SourceId source, std::string_view message); \
    template <typename Source> \
        requires(Detail::Core::isSourceEnum<Source>) \
    void name(Source source, std::string_view message) \
    { \
        name(Detail::Core::sourceId(source), message); \
    } \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    void name(Source &&source, std::format_string<Args...> format, Args &&...args) \
    { \
        log(levelValue, std::forward<Source>(source), format, std::forward<Args>(args)...); \
    } \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    void name(Source &&source, Types::RuntimeFormat format, Args &&...args) \
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

    /// @brief Synchronously reports a preformatted diagnostic through eligible emergency channels.
    GAMEWIP_LOGGER_EXPORT Types::ReportResult report(Types::Level level, std::string_view source, std::string_view message);
    /// @brief Synchronously reports a preformatted diagnostic under a bounded deadline.
    GAMEWIP_LOGGER_EXPORT Types::ReportResult report(
        Types::Level level,
        std::string_view source,
        std::chrono::milliseconds timeout,
        std::string_view message);
    /// @brief Synchronously reports a preformatted diagnostic with a registered source.
    GAMEWIP_LOGGER_EXPORT Types::ReportResult report(Types::Level level, Types::SourceId source, std::string_view message);
    /// @brief Synchronously reports a registered-source diagnostic under a bounded deadline.
    GAMEWIP_LOGGER_EXPORT Types::ReportResult report(
        Types::Level level,
        Types::SourceId source,
        std::chrono::milliseconds timeout,
        std::string_view message);

    /// @brief Synchronously reports a preformatted diagnostic with an enum source.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    Types::ReportResult report(Types::Level level, Source source, std::string_view message)
    {
        return report(level, Detail::Core::sourceId(source), message);
    }

    /// @brief Synchronously reports an enum-source diagnostic under a bounded deadline.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    Types::ReportResult report(Types::Level level, Source source, std::chrono::milliseconds timeout, std::string_view message)
    {
        return report(level, Detail::Core::sourceId(source), timeout, message);
    }

    /// @brief Formats and synchronously reports a diagnostic.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::ReportResult report(Types::Level level, Source source, std::format_string<Args...> format, Args &&...args)
    {
        return Detail::Core::formatAndReport(level, source, false, nullptr, format, std::forward<Args>(args)...);
    }

    /// @brief Formats and synchronously reports a diagnostic under a bounded deadline.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::ReportResult report(
        Types::Level level,
        Source source,
        std::chrono::milliseconds timeout,
        std::format_string<Args...> format,
        Args &&...args)
    {
        return Detail::Core::formatAndReport(level, source, false, &timeout, format, std::forward<Args>(args)...);
    }

    /// @brief Runtime-formats and synchronously reports a diagnostic.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::ReportResult report(Types::Level level, Source source, Types::RuntimeFormat format, Args &&...args)
    {
        return Detail::Core::runtimeFormatAndReport(level, source, false, nullptr, format, args...);
    }

    /// @brief Runtime-formats and synchronously reports a diagnostic under a bounded deadline.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    Types::ReportResult report(Types::Level level, Source source, std::chrono::milliseconds timeout, Types::RuntimeFormat format, Args &&...args)
    {
        return Detail::Core::runtimeFormatAndReport(level, source, false, &timeout, format, args...);
    }

    /// @brief Synchronously reports a preformatted Error with a UTF-8 string source.
    GAMEWIP_LOGGER_EXPORT Types::ReportResult reportError(std::string_view source, std::string_view message);
    /// @brief Synchronously reports a preformatted Error under a bounded deadline.
    GAMEWIP_LOGGER_EXPORT Types::ReportResult reportError(std::string_view source, std::chrono::milliseconds timeout, std::string_view message);
    /// @brief Synchronously reports a preformatted Error with a registered source.
    GAMEWIP_LOGGER_EXPORT Types::ReportResult reportError(Types::SourceId source, std::string_view message);
    /// @brief Synchronously reports a registered-source Error under a bounded deadline.
    GAMEWIP_LOGGER_EXPORT Types::ReportResult reportError(Types::SourceId source, std::chrono::milliseconds timeout, std::string_view message);

    /// @brief Synchronously reports a preformatted Fatal diagnostic and optional popup.
    GAMEWIP_LOGGER_EXPORT Types::ReportResult reportFatal(std::string_view source, std::string_view message);
    /// @brief Synchronously reports a preformatted Fatal diagnostic under a bounded deadline.
    GAMEWIP_LOGGER_EXPORT Types::ReportResult reportFatal(std::string_view source, std::chrono::milliseconds timeout, std::string_view message);
    /// @brief Synchronously reports a registered-source Fatal diagnostic and optional popup.
    GAMEWIP_LOGGER_EXPORT Types::ReportResult reportFatal(Types::SourceId source, std::string_view message);
    /// @brief Synchronously reports a registered-source Fatal diagnostic under a bounded deadline.
    GAMEWIP_LOGGER_EXPORT Types::ReportResult reportFatal(Types::SourceId source, std::chrono::milliseconds timeout, std::string_view message);

    /// @brief Synchronously reports a preformatted Error with an enum source.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    Types::ReportResult reportError(Source source, std::string_view message)
    {
        return reportError(Detail::Core::sourceId(source), message);
    }
    /// @brief Synchronously reports an enum-source Error under a bounded deadline.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    Types::ReportResult reportError(Source source, std::chrono::milliseconds timeout, std::string_view message)
    {
        return reportError(Detail::Core::sourceId(source), timeout, message);
    }
    /// @brief Synchronously reports a preformatted Fatal diagnostic with an enum source.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    Types::ReportResult reportFatal(Source source, std::string_view message)
    {
        return reportFatal(Detail::Core::sourceId(source), message);
    }
    /// @brief Synchronously reports an enum-source Fatal diagnostic under a bounded deadline.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    Types::ReportResult reportFatal(Source source, std::chrono::milliseconds timeout, std::string_view message)
    {
        return reportFatal(Detail::Core::sourceId(source), timeout, message);
    }

    /// @brief Declares formatted fixed-severity report overloads.
#define GAMEWIP_LOGGER_DECLARE_FORMATTED_REPORT(name, levelValue, popupValue) \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    Types::ReportResult name(Source source, std::format_string<Args...> format, Args &&...args) \
    { \
        return Detail::Core::formatAndReport(levelValue, source, popupValue, nullptr, format, std::forward<Args>(args)...); \
    } \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    Types::ReportResult name(Source source, std::chrono::milliseconds timeout, std::format_string<Args...> format, Args &&...args) \
    { \
        return Detail::Core::formatAndReport(levelValue, source, popupValue, &timeout, format, std::forward<Args>(args)...); \
    } \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    Types::ReportResult name(Source source, Types::RuntimeFormat format, Args &&...args) \
    { \
        return Detail::Core::runtimeFormatAndReport(levelValue, source, popupValue, nullptr, format, args...); \
    } \
    template <typename Source, typename... Args> \
        requires(sizeof...(Args) > 0) \
    Types::ReportResult name(Source source, std::chrono::milliseconds timeout, Types::RuntimeFormat format, Args &&...args) \
    { \
        return Detail::Core::runtimeFormatAndReport(levelValue, source, popupValue, &timeout, format, args...); \
    }

    GAMEWIP_LOGGER_DECLARE_FORMATTED_REPORT(reportError, Types::Level::Error, false)
    GAMEWIP_LOGGER_DECLARE_FORMATTED_REPORT(reportFatal, Types::Level::Fatal, true)
#undef GAMEWIP_LOGGER_DECLARE_FORMATTED_REPORT

    /// @brief Reports a Fatal diagnostic and then calls std::terminate().
    [[noreturn]] GAMEWIP_LOGGER_EXPORT void fatalTerminate(std::string_view source, std::string_view message);
    /// @brief Reports a registered-source Fatal diagnostic and then calls std::terminate().
    [[noreturn]] GAMEWIP_LOGGER_EXPORT void fatalTerminate(Types::SourceId source, std::string_view message);
    /// @brief Reports a Fatal diagnostic under a bounded deadline and then calls std::terminate().
    [[noreturn]] GAMEWIP_LOGGER_EXPORT void fatalTerminate(std::string_view source, std::chrono::milliseconds timeout, std::string_view message);
    /// @brief Reports a registered-source Fatal diagnostic under a bounded deadline and then calls std::terminate().
    [[noreturn]] GAMEWIP_LOGGER_EXPORT void fatalTerminate(Types::SourceId source, std::chrono::milliseconds timeout, std::string_view message);

    /// @brief Reports an enum-source Fatal diagnostic and then calls std::terminate().
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    [[noreturn]] void fatalTerminate(Source source, std::string_view message)
    {
        fatalTerminate(Detail::Core::sourceId(source), message);
    }
    /// @brief Reports an enum-source Fatal diagnostic under a deadline and then terminates.
    template <typename Source>
        requires(Detail::Core::isSourceEnum<Source>)
    [[noreturn]] void fatalTerminate(Source source, std::chrono::milliseconds timeout, std::string_view message)
    {
        fatalTerminate(Detail::Core::sourceId(source), timeout, message);
    }

    /// @brief Formats a Fatal diagnostic, reports it, and then calls std::terminate().
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    [[noreturn]] void fatalTerminate(Source source, std::format_string<Args...> format, Args &&...args)
    {
        static_cast<void>(Detail::Core::formatAndReport(Types::Level::Fatal, source, true, nullptr, format, std::forward<Args>(args)...));
        std::terminate();
    }
    /// @brief Formats and reports a Fatal diagnostic under a deadline, then terminates.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    [[noreturn]] void fatalTerminate(Source source, std::chrono::milliseconds timeout, std::format_string<Args...> format, Args &&...args)
    {
        static_cast<void>(Detail::Core::formatAndReport(Types::Level::Fatal, source, true, &timeout, format, std::forward<Args>(args)...));
        std::terminate();
    }
    /// @brief Runtime-formats a Fatal diagnostic, reports it, and then terminates.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    [[noreturn]] void fatalTerminate(Source source, Types::RuntimeFormat format, Args &&...args)
    {
        static_cast<void>(Detail::Core::runtimeFormatAndReport(Types::Level::Fatal, source, true, nullptr, format, args...));
        std::terminate();
    }
    /// @brief Runtime-formats and reports a Fatal diagnostic under a deadline, then terminates.
    template <typename Source, typename... Args>
        requires(sizeof...(Args) > 0)
    [[noreturn]] void fatalTerminate(Source source, std::chrono::milliseconds timeout, Types::RuntimeFormat format, Args &&...args)
    {
        static_cast<void>(Detail::Core::runtimeFormatAndReport(Types::Level::Fatal, source, true, &timeout, format, args...));
        std::terminate();
    }

    /// @brief Writes one validated UTF-8 diagnostic directly to the debugger channel.
    GAMEWIP_LOGGER_EXPORT IO::Types::Status writeDebugOutput(Types::Level level, std::string_view source, std::string_view message);
} // namespace GameWIP::Logger
