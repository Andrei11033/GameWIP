/// @file core.h
/// @brief Private Logger contracts shared by core implementation files.

#pragma once

#include "logger/internal/format_scratch.h"
#include "logger/internal/state.h"
#include "logger/platform/backend.h"
#include "terminal/output.h"
#include "unicode/unicode.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if LOGGER_INTERNAL_TEST_HOOKS
#include "logger/internal/test_hooks.h"
#endif

namespace GameWIP::Logger::Detail::Core
{
    /// ------------------------------------------------------------
    /// Runtime value encoding and validation
    /// ------------------------------------------------------------

    std::uint8_t toLevelValue(LogLevel level);
    std::uint8_t toOutputModeValue(OutputMode mode);
    std::uint8_t toFormatPolicyValue(FormatPolicy policy);
    FormatPolicy formatPolicyFromValue(std::uint8_t value);
    bool isValidFormatPolicy(FormatPolicy policy);
    bool isValidLevel(LogLevel level);
    bool isValidOutputMode(OutputMode mode);
    std::uint8_t levelBit(LogLevel level);
    OutputMode outputModeFromValue(std::uint8_t value);
    std::uint32_t packRuntimeState(bool running, OutputMode mode, LogLevel minLevel, std::uint8_t levelMask);
    bool runtimeStateRunning(std::uint32_t packed);
    OutputMode runtimeStateOutput(std::uint32_t packed);
    LogLevel runtimeStateMinLevel(std::uint32_t packed);
    std::uint8_t runtimeStateLevelMask(std::uint32_t packed);

    /// ------------------------------------------------------------
    /// Queue coordination and counters
    /// ------------------------------------------------------------

    void cpuRelax() noexcept;
    void waitForQueueSlot(QueueSlot &slot, std::size_t ticket);
    void loggerWorker();
    void recordQueueDropCounter(std::atomic<std::size_t> &counter);
    void recordDiagnosticFailureCounter(std::atomic<std::size_t> &counter);
    void updateAtomicMax(std::atomic<std::size_t> &target, std::size_t value);
    LoggerStats snapshotStats() noexcept;

    /// ------------------------------------------------------------
    /// Storage accounting and health
    /// ------------------------------------------------------------

    std::size_t messageArenaBytesUnlocked();
    std::size_t queueStorageBytesUnlocked();
    std::size_t sourceRegistryBytes(const SourceRegistry *registry);
    std::size_t publishedSourceRegistryBytes();
    std::size_t entryTextHeapCapacityBytes(const QueuedLogEntry &entry);
    bool entryTextHeapCapacityAvailableUnlocked();
    std::size_t entryTextHeapCapacityBytesUnlocked();
    void resetAtomicStats(std::size_t peakQueueDepth = 0);
    void waitForActiveProducersToLeave();
    void countAllocationFailure();
    void countFormatFailure();
    void publishRuntimeStateUnlocked();

    void resetHealthUnlocked(Types::Health::State state, OutputMode effectiveOutput);
    void recordHealthFailure(Types::Health::FailureSource source, const Status &status, bool disableChannel);
    void markHealthDisabledUnlocked();
    [[nodiscard]] Status firstFailure(Status current, const Status &candidate);

    /// ------------------------------------------------------------
    /// Source registration and filtering
    /// ------------------------------------------------------------

    std::shared_ptr<SourceRegistry> loadSourceRegistry();
    RegisteredSource *findSource(SourceRegistry &registry, SourceId source);
    const RegisteredSource *findSource(const SourceRegistry &registry, SourceId source);
    void rebuildSourceLookup(SourceRegistry &registry);
    bool sourceEnabledRuntime(SourceId source);
    bool sourceEnabledRuntime(const SourceRegistry *registry, SourceId source);
    bool shouldLogRuntime(LogLevel level);
    bool shouldLogRuntime(LogLevel level, SourceId source);
    FilterDecision checkPendingEntryAcceptedUnlocked(const PendingLogEntry &entry);
    LogStyle getLogStyle(LogLevel level);
    bool isLowPriority(LogLevel level);
    bool hasConsoleOutput(OutputMode mode);
    bool hasFileOutput(OutputMode mode);

    /// ------------------------------------------------------------
    /// Configuration and queue storage
    /// ------------------------------------------------------------

    std::size_t computeHardQueueLimit(std::size_t softQueueSize, double multiplier);
    std::size_t effectiveHardQueueLimit(double &multiplier, std::size_t softQueueSize, bool &adjusted);
    std::size_t effectiveWorkerBatchSize(std::size_t requested, std::size_t hardLimit);
    bool allocateMessageArena(std::size_t entryCount, std::size_t inlineMessageCapacity, std::unique_ptr<char[]> &arena);
    bool prepareQueueStorage(
        std::size_t hardLimit,
        std::size_t workerBatchSize,
        std::size_t inlineMessageCapacity,
        std::unique_ptr<QueueSlot[]> &ring,
        std::size_t &ringSize,
        std::vector<QueuedLogEntry> &batch,
        std::unique_ptr<char[]> &ringArena,
        std::unique_ptr<char[]> &batchArena);
    bool prepareSources(
        std::span<const SourceDefinition> definitions,
        std::span<const SourceFilter> filters,
        std::shared_ptr<SourceRegistry> &registry,
        Status &outStatus);
    bool prepareLevelMask(std::span<const LevelFilter> filters, std::uint8_t &outMask);
    void clearQueueUnlocked();
    void releaseRuntimeStorageUnlocked();
    void clearLogEntry(QueuedLogEntry &entry);
    void assignRetainedMessage(QueuedLogEntry &entry, std::string_view message, std::size_t maxMessageLength, bool &outTruncated);
    void copyPendingEntryToQueueSlot(QueuedLogEntry &destination, const PendingLogEntry &source, bool &outTruncated);
    void moveQueuedEntry(QueuedLogEntry &destination, QueuedLogEntry &source);
    EnqueueStatus reserveQueueDepth(const PendingLogEntry &entry, std::size_t &outPreviousDepth);
    void publishQueueSlot(QueueSlot &slot, std::size_t ticket, bool &outNotifyWorker);
    EnqueueStatus publishReservedQueueEntry(const PendingLogEntry &entry, bool &outTruncated, bool &outNotifyWorker);
    std::size_t drainQueueBatch(std::vector<QueuedLogEntry> &batch);

    /// ------------------------------------------------------------
    /// Message and source formatting
    /// ------------------------------------------------------------

    std::string formatTimeOrFallback(std::time_t time, std::string_view timeFormat, Status &outStatus);
    std::string getCurrentTimeText(std::string_view timeFormat, Status &outStatus);
    std::string_view getTimestampText(TimestampCache &cache);
    std::string getDebugTimestampText(Status *outStatus = nullptr);
    std::string_view findSourceName(const SourceRegistry *registry, SourceId source, bool &outUnknownSource);
    std::string_view resolveSourceText(const QueuedLogEntry &entry, const SourceRegistry *registry, bool &outUnknownSource);
    void recordUnknownSourceUse();
    void buildTruncatedMessage(std::string &outMessage, std::string_view message, std::size_t maxMessageLength);
    std::string_view boundedMessageView(std::string_view message, bool alreadyTruncated, std::string &scratch, bool &outTruncated);
    void buildLogLine(
        std::string &outMessage,
        std::string_view timestamp,
        std::string_view levelText,
        std::string_view source,
        std::string_view message);

    /// ------------------------------------------------------------
    /// Sink delivery and flushing
    /// ------------------------------------------------------------

    Status openFileExclusiveForLogger(const FilePath &path, FileWriter &outWriter);
    Status writeFileForLogger(FileWriter &writer, std::string_view text);
    Status flushFileForLogger(FileWriter &writer);
    [[nodiscard]] Color levelColor(LogLevel level);
    [[nodiscard]] std::string plainOutputText(std::string_view text);
    Status writeConsoleLine(const LogStyle &logStyle, std::string_view line, Color severityColor, Color sourceColor);
    [[nodiscard]] bool deadlineExpired(const FlushDeadline *deadline) noexcept;
    ReportSinkProgress writeReportSynchronously(
        LogLevel level,
        std::string_view source,
        std::string_view message,
        bool unknownSource = false,
        bool alreadyTruncated = false,
        const FlushDeadline *deadline = nullptr,
        Color sourceColor = Color::White);
    ReportSinkProgress writeReportSynchronously(
        LogLevel level,
        SourceId source,
        std::string_view message,
        bool alreadyTruncated = false,
        const FlushDeadline *deadline = nullptr);
    Status writeDebugOutputInternal(LogLevel level, std::string_view source, std::string_view message) noexcept;
    SinkWriteResult writeLogEntry(
        const QueuedLogEntry &entry,
        TimestampCache &timestampCache,
        std::string &lineScratch,
        std::string &fileBatchScratch);
    bool flushFileBatch(std::string &fileBatchScratch, bool forceFlush);

    /// ------------------------------------------------------------
    /// Logger lifecycle and public calls
    /// ------------------------------------------------------------

    void resetRuntimeStateUnlocked(
        const Types::Config &config,
        std::size_t softQueueSize,
        std::size_t hardQueueSize,
        double hardQueueMultiplier,
        std::size_t messageLength,
        std::size_t inlineMessageCapacity,
        std::size_t workerBatchSize,
        std::uint8_t levelMask,
        std::shared_ptr<const std::string> &&layoutTemplate,
        std::shared_ptr<SourceRegistry> sourceRegistry,
        std::unique_ptr<QueueSlot[]> &&ring,
        std::size_t ringSize,
        std::vector<QueuedLogEntry> &&batch,
        std::unique_ptr<char[]> &&ringArena,
        std::unique_ptr<char[]> &&batchArena);
    Types::Init::Result initImpl(const Types::Config &config);
    Status shutdownImpl();
    void setOutputMode(OutputMode mode);
    OutputMode outputModeAfterFileSetupFailure(OutputMode requested, bool fallbackToConsole);
    EnqueueOutcome enqueuePendingLogEntry(const PendingLogEntry &entry, bool countDrops = true);
    PendingLogEntry makePendingEntry(LogLevel level, std::string_view source, std::string_view message, bool alreadyTruncated = false);
    PendingLogEntry makePendingEntry(LogLevel level, SourceId source, std::string_view message, bool alreadyTruncated = false);
    EnqueueOutcome enqueueAndWakeWorker(const PendingLogEntry &entry, bool countDrops = true);
    void shutdownLoggerAtExit();

    /// ------------------------------------------------------------
    /// Flush operations
    /// ------------------------------------------------------------

    Types::FlushResult flushSinksInternal(const FlushDeadline *deadline = nullptr);
    Types::FlushResult flushInternal(const FlushDeadline *deadline = nullptr);
    [[nodiscard]] FlushDeadline makeFlushDeadline(std::chrono::milliseconds timeout) noexcept;
    [[nodiscard]] bool lockBefore(std::unique_lock<std::mutex> &lock, FlushDeadline deadline);
} // namespace GameWIP::Logger::Detail::Core
