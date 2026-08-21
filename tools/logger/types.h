/// @file types.h
/// @brief Shared passive public types for the Logger library.

#pragma once

#include "io/io.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace GameWIP::Logger::Types
{
    /// @brief Severity assigned to a log record or report.
    enum class Level : std::uint8_t
    {
        Trace, ///< Fine-grained execution detail normally disabled outside focused diagnosis.
        Debug, ///< Developer-facing state useful during debugging.
        Info,  ///< Normal lifecycle or progress information.
        Warn,  ///< Unexpected condition from which normal work can continue.
        Error, ///< Operation or subsystem failure that does not itself terminate the process.
        Fatal  ///< Failure associated with an unrecoverable process path.
    };

    /// @brief Enabled normal-output sink combination.
    enum class OutputMode : std::uint8_t
    {
        None,    ///< Disable normal asynchronous sinks.
        Console, ///< Write normal records to the configured terminal stream.
        File,    ///< Write normal records to the configured log file.
        Both     ///< Enable both Console and File sinks.
    };

    /// @brief Stable numeric identifier for a registered source.
    using SourceId = std::uint32_t;

    /// @brief Explicit wrapper for a runtime-provided format string.
    struct RuntimeFormat
    {
        std::string_view text = {}; ///< Runtime format text.
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

    namespace Init
    {
        /// @brief Final lifecycle state produced by init().
        enum class Outcome : std::uint8_t
        {
            Started, ///< A Logger runtime with at least one normal sink is active.
            Disabled ///< Initialization completed without leaving a normal sink active.
        };

        /// @brief Recoverable configuration or storage adjustments made by init().
        enum class Adjustment : std::uint32_t
        {
            None = 0,                          ///< Requested configuration was used without recoverable adjustment.
            QueueLimitsAdjusted = 1u << 0u,    ///< Soft or hard queue limits were clamped to a usable relationship.
            MessageLengthAdjusted = 1u << 1u,  ///< Maximum retained message length was clamped.
            InlineCapacityAdjusted = 1u << 2u, ///< Inline message storage was clamped relative to message limits.
            WorkerBatchAdjusted = 1u << 3u,    ///< Worker batch size was clamped relative to queue limits.
            QueueStorageFallback = 1u << 4u    ///< Requested queue storage failed and a smaller usable allocation was selected.
        };

        /// @brief Combines initialization-adjustment flags.
        [[nodiscard]] constexpr Adjustment operator|(Adjustment left, Adjustment right) noexcept
        {
            return static_cast<Adjustment>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
        }

        /// @brief Adds an initialization-adjustment flag in place.
        constexpr Adjustment &operator|=(Adjustment &left, Adjustment right) noexcept
        {
            left = left | right;
            return left;
        }

        /// @brief Tests whether an initialization adjustment contains a flag.
        [[nodiscard]] constexpr bool hasAdjustment(Adjustment value, Adjustment flag) noexcept
        {
            return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
        }

        /// @brief Rich initialization result separating operation status from final lifecycle state.
        struct Result
        {
            IO::Types::Status status;                      ///< Overall operational status.
            Outcome outcome = Outcome::Disabled;           ///< Final lifecycle outcome.
            Adjustment adjustments = Adjustment::None;     ///< Recoverable changes made by init().
            OutputMode requestedOutput = OutputMode::None; ///< Caller-requested output mode.
            OutputMode effectiveOutput = OutputMode::None; ///< Output mode actually left active.
            IO::Types::Status outputSetupStatus;           ///< Direct File/output setup status.
        };
    } // namespace Init

    /// @brief Completion state for a Logger-owned flush deadline.
    enum class FlushOutcome : std::uint8_t
    {
        Completed, ///< Queue drain and active-sink flush finished before the deadline.
        TimedOut   ///< The deadline expired before complete drain and flush.
    };

    /// @brief Result of draining queued logs and flushing active sinks.
    struct FlushResult
    {
        IO::Types::Status status;                       ///< Real IO or operation failure, if any.
        FlushOutcome outcome = FlushOutcome::Completed; ///< Independent deadline outcome.
    };

    namespace Report
    {
        /// @brief Completion state for a synchronous report deadline.
        enum class Outcome : std::uint8_t
        {
            Completed, ///< Every eligible report attempt finished before the deadline.
            TimedOut   ///< The deadline expired while report delivery or flushing was still pending.
        };

        /// @brief Fraction of eligible emergency channels that accepted a report.
        enum class Delivery : std::uint8_t
        {
            None,    ///< No eligible emergency channel accepted the report.
            Partial, ///< At least one but not every eligible channel accepted the report.
            Complete ///< Every eligible emergency channel accepted the report.
        };

        /// @brief Result of a synchronous emergency report attempt.
        struct Result
        {
            IO::Types::Status status;             ///< First real operation failure, if any.
            Outcome outcome = Outcome::Completed; ///< Independent deadline outcome.
            Delivery delivery = Delivery::None;   ///< Delivery across eligible channels.
        };
    } // namespace Report

    namespace Health
    {
        /// @brief Current aggregate Logger health for the active initialization epoch.
        enum class State : std::uint8_t
        {
            Healthy,  ///< Active configured sinks have no retained asynchronous failure.
            Degraded, ///< A sink or platform channel failed during the current initialization epoch.
            Disabled  ///< No Logger runtime is active.
        };

        /// @brief Channel associated with the most recent health failure.
        enum class FailureSource : std::uint8_t
        {
            None,          ///< No retained failure source.
            Console,       ///< Terminal-backed normal or emergency output failed.
            File,          ///< File-backed normal or emergency output failed.
            DebugOutput,   ///< Platform debugger-output delivery failed.
            FatalPopup,    ///< Platform fatal-popup presentation failed.
            TimeConversion ///< Timestamp conversion failed.
        };

        /// @brief Coherent snapshot of Logger health and failure metadata.
        struct Snapshot
        {
            State state = State::Disabled;                                  ///< Aggregate health state.
            OutputMode effectiveOutput = OutputMode::None;                  ///< Currently usable normal sinks.
            FailureSource lastFailureSource = FailureSource::None;          ///< Most recent failed channel.
            IO::Types::ErrorCode lastError = IO::Types::ErrorCode::Success; ///< Most recent portable error.
            std::int64_t lastNativeCode = 0;                                ///< Associated backend-native error.
            std::uint64_t failureCount = 0;                                 ///< Failures in the current init epoch.
        };
    } // namespace Health
} // namespace GameWIP::Logger::Types
