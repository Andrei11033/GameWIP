/// @file logger_state.cpp
/// @brief Logger shared state, configuration factories, stats snapshots, and lifecycle operations.

#include "logger/internal/logger_core.h"

namespace GameWIP::LoggerDetail::Core
{
    LoggerState &loggerState()
    {
        static LoggerState state;
        return state;
    }

    //-------------------------------------------------------------------------------------------------
    // Enum and result helpers
    //-------------------------------------------------------------------------------------------------

    /// @brief Converts a public level enum into its compact numeric representation.
    /// @param level Level to convert.
    /// @return Numeric level value used by atomics and bitmasks.
    std::uint8_t toLevelValue(LogLevel level)
    {
        return static_cast<std::uint8_t>(level);
    }

    /// @brief Converts a public output enum into its compact numeric representation.
    /// @param mode Output mode to convert.
    /// @return Numeric output mode value used by atomics.
    std::uint8_t toOutputModeValue(OutputMode mode)
    {
        return static_cast<std::uint8_t>(mode);
    }

    /// @brief Converts a public format policy into compact atomic storage.
    std::uint8_t toFormatPolicyValue(FormatPolicy policy)
    {
        switch (policy)
        {
        case FormatPolicy::StrictBounded:
            return 0;
        case FormatPolicy::FastNormal:
            return 1;
        }

        return 0;
    }

    /// @brief Converts compact atomic storage back to a public format policy.
    FormatPolicy formatPolicyFromValue(std::uint8_t value)
    {
        return value == 1 ? FormatPolicy::FastNormal : FormatPolicy::StrictBounded;
    }

    /// @brief Sanitizes invalid format policy enum values to the compatibility default.
    FormatPolicy sanitizeFormatPolicy(FormatPolicy policy)
    {
        switch (policy)
        {
        case FormatPolicy::StrictBounded:
        case FormatPolicy::FastNormal:
            return policy;
        }

        return FormatPolicy::StrictBounded;
    }

    /// @brief Checks whether a level value is one of the defined Logger::Types::Level values.
    /// @param level Level to validate.
    /// @return True when the value is in range.
    bool isValidLevel(LogLevel level)
    {
        return toLevelValue(level) < kLevelCount;
    }

    /// @brief Checks whether an output mode is one of the defined Logger::Types::Output values.
    /// @param mode Output mode to validate.
    /// @return True when the value is in range.
    bool isValidOutputMode(OutputMode mode)
    {
        switch (mode)
        {
        case OutputMode::None:
        case OutputMode::Console:
        case OutputMode::File:
        case OutputMode::Both:
            return true;
        }

        return false;
    }

    /// @brief Converts a valid level into a single-bit mask.
    /// @param level Level to convert.
    /// @return Bit for the level, or zero when the level is invalid.
    std::uint8_t levelBit(LogLevel level)
    {
        if (!isValidLevel(level))
        {
            return 0;
        }

        return static_cast<std::uint8_t>(1u << toLevelValue(level));
    }

    /// @brief Converts an atomic output-mode value back to the public enum.
    /// @param mode Numeric output mode value.
    /// @return Public Output value, clamped to None if the stored value is invalid.
    OutputMode outputModeFromValue(std::uint8_t mode)
    {
        switch (static_cast<OutputMode>(mode))
        {
        case OutputMode::None:
            return OutputMode::None;
        case OutputMode::Console:
            return OutputMode::Console;
        case OutputMode::File:
            return OutputMode::File;
        case OutputMode::Both:
            return OutputMode::Both;
        }

        return OutputMode::None;
    }

    /// @brief Packs hot-path logger state into one atomic word.
    /// @param running True when normal producer logs may be accepted.
    /// @param mode Current output mode.
    /// @param minLevel Startup minimum severity.
    /// @param levelMask Runtime enabled-level mask.
    /// @return Packed runtime-state word.
    std::uint32_t packRuntimeState(bool running, OutputMode mode, LogLevel minLevel, std::uint8_t levelMask)
    {
        std::uint32_t packed = running ? kRuntimeStateRunningBit : 0u;
        packed |= (static_cast<std::uint32_t>(toOutputModeValue(mode)) & kRuntimeStateEnumMask) << kRuntimeStateOutputShift;
        packed |= (static_cast<std::uint32_t>(toLevelValue(minLevel)) & kRuntimeStateEnumMask) << kRuntimeStateMinLevelShift;
        packed |= (static_cast<std::uint32_t>(levelMask) & kRuntimeStateLevelMaskMask) << kRuntimeStateLevelMaskShift;
        return packed;
    }

    /// @brief Extracts output mode from a packed runtime-state word.
    /// @param packed Packed runtime-state word.
    /// @return Current output mode.
    OutputMode runtimeStateOutput(std::uint32_t packed)
    {
        return outputModeFromValue(static_cast<std::uint8_t>((packed >> kRuntimeStateOutputShift) & kRuntimeStateEnumMask));
    }

    /// @brief Extracts min level from a packed runtime-state word.
    /// @param packed Packed runtime-state word.
    /// @return Packed min level converted to public enum.
    LogLevel runtimeStateMinLevel(std::uint32_t packed)
    {
        return static_cast<LogLevel>((packed >> kRuntimeStateMinLevelShift) & kRuntimeStateEnumMask);
    }

    /// @brief Extracts runtime level mask from a packed runtime-state word.
    /// @param packed Packed runtime-state word.
    /// @return Runtime enabled-level bitmask.
    std::uint8_t runtimeStateLevelMask(std::uint32_t packed)
    {
        return static_cast<std::uint8_t>((packed >> kRuntimeStateLevelMaskShift) & kRuntimeStateLevelMaskMask);
    }

    /// @brief Checks whether a platform error carries a real failure.
    /// @param error Platform error to inspect.
    /// @return True when the error source is not None.
    bool hasPlatformError(const PlatformError &error)
    {
        return error.source != PlatformErrorSource::None;
    }

    /// @brief Issues a cheap processor relax hint while waiting for a ring slot.
    void cpuRelax() noexcept
    {
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
        _mm_pause();
#endif
    }

    /// @brief Waits for a claimed MPSC ring slot with bounded spinning before yielding.
    /// @param slot Slot claimed by ticket modulo capacity.
    /// @param ticket Expected sequence value for this producer.
    void waitForQueueSlot(QueueSlot &slot, std::size_t ticket)
    {
        std::uint32_t spins = 0;
        while (slot.sequence.load(std::memory_order_acquire) != ticket)
        {
            if (spins < kQueueSlotSpinBeforeYield)
            {
                ++spins;
                cpuRelax();
            }
            else
            {
                std::this_thread::yield();
            }
        }
    }

    /// @brief Counts one queue-pressure drop in both lifetime and resettable diagnostics.
    /// @param counter Resettable queue-drop counter to increment.
    void recordQueueDropCounter(std::atomic<std::size_t> &counter)
    {
        loggerState().droppedLogs.fetch_add(1, std::memory_order_relaxed);
        counter.fetch_add(1, std::memory_order_relaxed);
    }

    /// @brief Counts one diagnostic failure without affecting lifetime queue-drop reporting.
    /// @param counter Resettable diagnostic counter to increment.
    void recordDiagnosticFailureCounter(std::atomic<std::size_t> &counter)
    {
        counter.fetch_add(1, std::memory_order_relaxed);
    }

    /// @brief Updates an atomic maximum value.
    /// @param target Atomic maximum to update.
    /// @param value Candidate value.
    void updateAtomicMax(std::atomic<std::size_t> &target, std::size_t value)
    {
        std::size_t current = target.load(std::memory_order_relaxed);
        while (current < value && !target.compare_exchange_weak(current, value, std::memory_order_relaxed, std::memory_order_relaxed))
        {
        }
    }

    /// @brief Reads resettable atomic counters into the public Stats type.
    /// @return Public stats snapshot.
    LoggerStats snapshotStats()
    {
        return LoggerStats{
            loggerState().stats.queued.load(std::memory_order_relaxed),
            loggerState().stats.written.load(std::memory_order_relaxed),
            loggerState().stats.queueDropsSoft.load(std::memory_order_relaxed),
            loggerState().stats.queueDropsHard.load(std::memory_order_relaxed),
            loggerState().stats.allocationFailures.load(std::memory_order_relaxed),
            loggerState().stats.fileWriteFailures.load(std::memory_order_relaxed),
            loggerState().stats.unknownSourceUses.load(std::memory_order_relaxed),
            loggerState().stats.formatFailures.load(std::memory_order_relaxed),
            loggerState().stats.truncated.load(std::memory_order_relaxed),
            loggerState().stats.peakQueueDepth.load(std::memory_order_relaxed)};
    }

    /// @brief Returns retained message arena bytes for the current queue storage.
    /// @pre loggerState().logMutex is held.
    /// @return Current ring and worker-batch message arena bytes.
    std::size_t messageArenaBytesUnlocked()
    {
        if (loggerState().inlineMessageCapacity == 0)
        {
            return 0;
        }

        std::size_t bytes = 0;
        if (loggerState().ringMessageArena)
        {
            bytes += loggerState().logRingSize * loggerState().inlineMessageCapacity;
        }
        if (loggerState().batchMessageArena)
        {
            bytes += loggerState().workerBatch.size() * loggerState().inlineMessageCapacity;
        }
        return bytes;
    }

    /// @brief Returns retained queue vector storage bytes for ring and worker batch entries.
    /// @pre loggerState().logMutex is held.
    /// @return Current queue vector capacity bytes.
    std::size_t queueStorageBytesUnlocked()
    {
        return (loggerState().logRingSize * sizeof(QueueSlot)) + (loggerState().workerBatch.capacity() * sizeof(QueuedLogEntry));
    }

    /// @brief Returns retained source registry storage bytes.
    /// @param registry Current source registry snapshot, or nullptr.
    /// @return Best-effort retained registry bytes.
    std::size_t sourceRegistryBytes(const SourceRegistry *registry)
    {
        if (!registry)
        {
            return 0;
        }

        std::size_t bytes = sizeof(SourceRegistry);
        bytes += registry->sources.capacity() * sizeof(RegisteredSource);
        bytes += registry->directSourceLookup.capacity() * sizeof(std::size_t);
        static const std::size_t emptyStringCapacity = std::string{}.capacity();
        for (const RegisteredSource &source : registry->sources)
        {
            const std::size_t nameCapacity = source.name.capacity();
            if (nameCapacity > emptyStringCapacity)
            {
                bytes += nameCapacity;
            }
        }
        return bytes;
    }

    /// @brief Returns bytes retained by the currently published shared source registry.
    /// @return Best-effort retained source registry bytes.
    std::size_t publishedSourceRegistryBytes()
    {
        const std::shared_ptr<SourceRegistry> registry = loggerState().sourceRegistry.load(std::memory_order_acquire);
        return sourceRegistryBytes(registry.get());
    }

    /// @brief Returns retained heap fallback capacity for one queued entry.
    /// @param entry Queue entry to inspect.
    /// @return Source and message heap fallback capacities.
    std::size_t entryTextHeapCapacityBytes(const QueuedLogEntry &entry)
    {
        return entry.sourceText.capacityBytes() + entry.message.capacityBytes();
    }

    /// @brief Checks whether queue-entry text capacity can be inspected without racing mutations.
    /// @pre loggerState().logMutex is held.
    /// @return True when producers are inactive, the queue is empty, and the worker is idle.
    bool entryTextHeapCapacityAvailableUnlocked()
    {
        return !loggerState().workerBusy &&
               loggerState().queueDepth.load(std::memory_order_acquire) == 0 &&
               loggerState().publishedQueueDepth.load(std::memory_order_acquire) == 0 &&
               loggerState().activeProducers.load(std::memory_order_acquire) == 0;
    }

    /// @brief Returns retained heap fallback capacity in queue entries.
    /// @pre loggerState().logMutex is held.
    /// @note Worker batch entries are scanned only while the worker is idle to avoid racing worker mutation.
    /// @return Best-effort retained entry text heap capacity.
    std::size_t entryTextHeapCapacityBytesUnlocked()
    {
        if (!entryTextHeapCapacityAvailableUnlocked())
        {
            return 0;
        }

        std::size_t bytes = 0;
        for (std::size_t index = 0; index < loggerState().logRingSize; ++index)
        {
            bytes += entryTextHeapCapacityBytes(loggerState().logRing[index].entry);
        }

        if (!loggerState().workerBusy)
        {
            for (const QueuedLogEntry &entry : loggerState().workerBatch)
            {
                bytes += entryTextHeapCapacityBytes(entry);
            }
        }

        return bytes;
    }

    /// @brief Resets visible atomic stats counters.
    /// @param peakQueueDepth Peak queue depth to publish after reset.
    void resetAtomicStats(std::size_t peakQueueDepth)
    {
        loggerState().stats.queued.store(0, std::memory_order_relaxed);
        loggerState().stats.written.store(0, std::memory_order_relaxed);
        loggerState().stats.queueDropsSoft.store(0, std::memory_order_relaxed);
        loggerState().stats.queueDropsHard.store(0, std::memory_order_relaxed);
        loggerState().stats.allocationFailures.store(0, std::memory_order_relaxed);
        loggerState().stats.fileWriteFailures.store(0, std::memory_order_relaxed);
        loggerState().stats.unknownSourceUses.store(0, std::memory_order_relaxed);
        loggerState().stats.formatFailures.store(0, std::memory_order_relaxed);
        loggerState().stats.truncated.store(0, std::memory_order_relaxed);
        loggerState().stats.peakQueueDepth.store(peakQueueDepth, std::memory_order_relaxed);
    }

    /// @brief Waits until producers that entered before shutdown/thread-start failure have left.
    void waitForActiveProducersToLeave()
    {
        while (loggerState().activeProducers.load(std::memory_order_acquire) != 0)
        {
            std::this_thread::yield();
        }
    }

    /// @brief Writes last-result state while logMutex is already held.
    /// @param result Result to publish.
    /// @param platformError Optional platform error to publish alongside result.
    void setResultUnlocked(LoggerResult result, PlatformError platformError)
    {
        loggerState().lastResult = result;
        loggerState().lastPlatformError = platformError;
    }

    /// @brief Writes last-result state from any thread.
    /// @param result Result to publish.
    /// @param platformError Optional platform error to publish alongside result.
    void recordResult(LoggerResult result, PlatformError platformError)
    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        setResultUnlocked(result, platformError);
    }

    /// @brief Converts a structured platform failure into last-result state when needed.
    /// @param platformError Platform error returned by the platform bridge.
    void recordPlatformErrorIfAny(const PlatformError &platformError)
    {
        if (hasPlatformError(platformError))
        {
            recordResult(LoggerResult::PlatformCallFailed, platformError);
        }
    }

    /// @brief Preserves the first recoverable init warning while later setup keeps running.
    /// @param inOutResult Current init result.
    /// @param candidateResult New recoverable result to preserve if this is the first warning.
    /// @param inOutPlatformError Current platform error paired with inOutResult.
    /// @param candidatePlatformError Optional platform error paired with candidateResult.
    void preserveFirstInitResult(
        LoggerResult &inOutResult,
        LoggerResult candidateResult,
        PlatformError &inOutPlatformError,
        PlatformError candidatePlatformError)
    {
        if (inOutResult == LoggerResult::Success)
        {
            inOutResult = candidateResult;
            inOutPlatformError = candidatePlatformError;
        }
    }

    /// @brief Counts a file write/flush failure without stopping console/debug sinks.
    /// @param platformError Native failure details returned by the platform file bridge, when available.
    void recordFileWriteFailure(PlatformError platformError)
    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        loggerState().stats.fileWriteFailures.fetch_add(1, std::memory_order_relaxed);
        setResultUnlocked(LoggerResult::FileWriteFailed, platformError);
    }

    /// @brief Counts an allocation/internal-format failure with relaxed atomic accounting.
    void countAllocationFailure()
    {
        recordDiagnosticFailureCounter(loggerState().stats.allocationFailures);
    }

    /// @brief Counts a runtime format failure with relaxed atomic accounting.
    void countFormatFailure()
    {
        recordDiagnosticFailureCounter(loggerState().stats.formatFailures);
    }

    /// @brief Publishes locked configuration state into atomics used by hot paths.
    void publishRuntimeStateUnlocked()
    {
        loggerState().runtimeStateBits.store(
            packRuntimeState(loggerState().workerRunning, loggerState().mode, loggerState().minLevel, loggerState().enabledLevelMask),
            std::memory_order_release);
        loggerState().consoleColorEnabledAtomic.store(loggerState().consoleColorEnabled, std::memory_order_release);
        loggerState().debugOutputEnabledAtomic.store(loggerState().debugOutputEnabled, std::memory_order_release);
        loggerState().fatalPopupEnabledAtomic.store(loggerState().fatalPopupEnabled, std::memory_order_release);
        loggerState().flushConsoleEveryWriteAtomic.store(loggerState().flushConsoleEveryWrite, std::memory_order_release);
        loggerState().flushFileEveryBatchAtomic.store(loggerState().flushFileEveryBatch, std::memory_order_release);
    }

    /// @brief Publishes ANSI color support for stdout/stderr outside the hot write path.
    void publishConsoleColorSupport()
    {
        const bool allowColor = loggerState().consoleColorEnabled;
        loggerState().stdoutColorEnabledAtomic.store(
            allowColor && GameWIP::LoggerDetail::Platform::supportsAnsiColor(GameWIP::LoggerDetail::Platform::ConsoleStream::Stdout),
            std::memory_order_release);
        loggerState().stderrColorEnabledAtomic.store(
            allowColor && GameWIP::LoggerDetail::Platform::supportsAnsiColor(GameWIP::LoggerDetail::Platform::ConsoleStream::Stderr),
            std::memory_order_release);
    }

    /// @brief Returns cached ANSI-color availability for the selected console stream.
    bool consoleColorEnabledForStream(bool useCerr)
    {
        return useCerr
                   ? loggerState().stderrColorEnabledAtomic.load(std::memory_order_acquire)
                   : loggerState().stdoutColorEnabledAtomic.load(std::memory_order_acquire);
    }

    //-------------------------------------------------------------------------------------------------
    // Output style and sink selection helpers
    //-------------------------------------------------------------------------------------------------

    /// @brief Returns output label/color/stream routing for a level.
    /// @param level Level to style.
    /// @return Style data for the requested level.
    LogStyle getLogStyle(LogLevel level)
    {
        switch (level)
        {
        case LogLevel::Trace:
            return {"TRACE", "\033[90m", false};
        case LogLevel::Debug:
            return {"DEBUG", "\033[36m", false};
        case LogLevel::Info:
            return {"INFO", "", false};
        case LogLevel::Warn:
            return {"WARN", "\033[33m", false};
        case LogLevel::Error:
            return {"ERROR", "\033[31m", true};
        case LogLevel::Fatal:
            return {"FATAL", "\033[31m", true};
        }

        return {};
    }

    /// @brief Checks whether a level is allowed to drop at the soft queue limit.
    /// @param level Level to test.
    /// @return True for Trace, Debug, Info, and Warn.
    bool isLowPriority(LogLevel level)
    {
        return level <= LogLevel::Warn;
    }

    /// @brief Checks whether an output mode includes console output.
    /// @param mode Output mode to inspect.
    /// @return True when stdout/stderr should receive normal logs.
    bool hasConsoleOutput(OutputMode mode)
    {
        return mode == OutputMode::Console || mode == OutputMode::Both;
    }

    /// @brief Checks whether an output mode includes file output.
    /// @param mode Output mode to inspect.
    /// @return True when the file sink should receive normal logs.
    bool hasFileOutput(OutputMode mode)
    {
        return mode == OutputMode::File || mode == OutputMode::Both;
    }

    //-------------------------------------------------------------------------------------------------
    // Init validation and storage helpers
    //-------------------------------------------------------------------------------------------------

    /// @brief Computes ceil(softQueueSize * multiplier) while avoiding size_t overflow.
    /// @param softQueueSize Configured soft queue limit.
    /// @param multiplier Hard queue capacity multiplier.
    /// @return Hard queue limit.
    std::size_t computeHardQueueLimit(std::size_t softQueueSize, double multiplier)
    {
        const long double preciseLimit = static_cast<long double>(softQueueSize) * static_cast<long double>(multiplier);
        const long double maxLimit = static_cast<long double>(std::numeric_limits<std::size_t>::max());
        if (preciseLimit >= maxLimit)
        {
            return std::numeric_limits<std::size_t>::max();
        }

        const std::size_t hardLimit = static_cast<std::size_t>(std::ceil(preciseLimit));
        return std::max(softQueueSize, hardLimit);
    }

    /// @brief Sanitizes the hard queue multiplier.
    /// @param inOutHardQueueMultiplier Configured multiplier, overwritten with the sanitized multiplier.
    /// @param softQueueSize Sanitized soft queue limit.
    /// @param inOutResult Receives InvalidQueueSize if the requested multiplier is invalid.
    /// @return Effective hard queue limit.
    std::size_t effectiveHardQueueLimit(double &inOutHardQueueMultiplier, std::size_t softQueueSize, LoggerResult &inOutResult)
    {
        if (!std::isfinite(inOutHardQueueMultiplier) || inOutHardQueueMultiplier < 1.0)
        {
            if (inOutResult == LoggerResult::Success)
            {
                inOutResult = LoggerResult::InvalidQueueSize;
            }
            inOutHardQueueMultiplier = 1.0;
            return softQueueSize;
        }

        return computeHardQueueLimit(softQueueSize, inOutHardQueueMultiplier);
    }

    /// @brief Sanitizes and clamps worker batch size against the hard queue limit.
    /// @param requested Configured worker batch size, where zero means default.
    /// @param hardLimit Effective hard queue limit.
    /// @return Effective worker batch size in [1, hardLimit].
    std::size_t effectiveWorkerBatchSize(std::size_t requested, std::size_t hardLimit)
    {
        const std::size_t wanted = requested == 0 ? kDefaultWorkerBatchSize : requested;
        return std::clamp(wanted, std::size_t{1}, hardLimit);
    }

    //-------------------------------------------------------------------------------------------------
    // Init and lifecycle state helpers
    //-------------------------------------------------------------------------------------------------

    /// @brief Resets init-scoped runtime state while preserving allocated inputs from init().
    /// @param config Startup configuration.
    /// @param softQueueSize Sanitized soft queue size.
    /// @param hardQueueSize Sanitized hard queue size.
    /// @param hardQueueMultiplier Sanitized hard queue multiplier.
    /// @param messageLength Sanitized maximum message length.
    /// @param inlineMessageCapacity Per-slot reserved message bytes.
    /// @param workerBatchSize Effective worker batch capacity.
    /// @param levelMask Initial runtime level-filter mask.
    /// @param sourceRegistry Source registry snapshot to publish.
    /// @param ring Allocated ring storage.
    /// @param ringSize Number of slots in ring.
    /// @param batch Allocated worker batch storage.
    /// @param ringArena Ring message arena.
    /// @param batchArena Worker batch message arena.
    /// @pre loggerState().logMutex is held.
    void resetRuntimeStateUnlocked(
        const GameWIP::Logger::Types::Config &config,
        std::size_t softQueueSize,
        std::size_t hardQueueSize,
        double hardQueueMultiplier,
        std::size_t messageLength,
        std::size_t inlineMessageCapacity,
        std::size_t workerBatchSize,
        std::uint8_t levelMask,
        std::shared_ptr<SourceRegistry> sourceRegistry,
        std::unique_ptr<QueueSlot[]> &&ring,
        std::size_t ringSize,
        std::vector<QueuedLogEntry> &&batch,
        std::unique_ptr<char[]> &&ringArena,
        std::unique_ptr<char[]> &&batchArena)
    {
        loggerState().logFilePath.clear();
        loggerState().mode = config.output;
        loggerState().fallbackToConsoleOnFileFailure = config.fallbackToConsoleOnFileFailure;
        loggerState().minLevel = config.minLevel;
        loggerState().softQueueSize = softQueueSize;
        loggerState().hardQueueSize = hardQueueSize;
        loggerState().hardQueueMultiplier = hardQueueMultiplier;
        loggerState().maxMessageLength = messageLength;
        loggerState().formatPolicy = sanitizeFormatPolicy(config.formatPolicy);
        loggerState().inlineMessageCapacity = inlineMessageCapacity;
        loggerState().workerBatchSize = workerBatchSize;
        loggerState().consoleColorEnabled = config.enableConsoleColor;
        loggerState().debugOutputEnabled = config.enableDebugOutput;
        loggerState().fatalPopupEnabled = config.enableFatalPopup;
        loggerState().flushFileEveryBatch = config.flushFileEveryBatch;
        loggerState().flushConsoleEveryWrite = config.flushConsoleEveryWrite;
        loggerState().releaseMessageMemoryAfterWrite = config.releaseMessageMemoryAfterWrite;
        loggerState().releaseStorageOnShutdown = config.releaseStorageOnShutdown;
        loggerState().enabledLevelMask = levelMask;
        loggerState().sourceRegistry.store(std::move(sourceRegistry), std::memory_order_release);
        loggerState().ringMessageArena = std::move(ringArena);
        loggerState().batchMessageArena = std::move(batchArena);
        loggerState().logRing = std::move(ring);
        loggerState().logRingSize = ringSize;
        loggerState().workerBatch = std::move(batch);
        loggerState().droppedLogs.store(0, std::memory_order_relaxed);
        resetAtomicStats();
        loggerState().workerRunning = false;
        loggerState().workerBusy = false;
        loggerState().enqueueTicket.store(0, std::memory_order_relaxed);
        loggerState().dequeueTicket.store(0, std::memory_order_relaxed);
        loggerState().queueDepth.store(0, std::memory_order_relaxed);
        loggerState().publishedQueueDepth.store(0, std::memory_order_relaxed);
        loggerState().maxMessageLengthAtomic.store(messageLength, std::memory_order_release);
        loggerState().formatPolicyAtomic.store(toFormatPolicyValue(loggerState().formatPolicy), std::memory_order_release);
        loggerState().releaseMessageMemoryAfterWriteAtomic.store(config.releaseMessageMemoryAfterWrite, std::memory_order_release);
        publishRuntimeStateUnlocked();
        publishConsoleColorSupport();
        loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
    }

    /// @brief Changes output mode after file setup fallback.
    /// @param mode New output mode.
    /// @note Used during init before worker start; it also updates runtime output atomics.
    void setOutputMode(OutputMode mode)
    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        loggerState().mode = mode;
        publishRuntimeStateUnlocked();
        if (!hasFileOutput(mode))
        {
            loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
        }
    }

    /// @brief Chooses the remaining output mode when the requested file sink cannot be opened.
    /// @param requested Startup output requested by Config.
    /// @param fallbackToConsole True to use Console for file-only failures.
    /// @return Console when explicitly requested or allowed as fallback; otherwise None.
    OutputMode outputModeAfterFileSetupFailure(OutputMode requested, bool fallbackToConsole)
    {
        if (hasConsoleOutput(requested) || fallbackToConsole)
        {
            return OutputMode::Console;
        }

        return OutputMode::None;
    }
}

using namespace GameWIP::LoggerDetail::Core;

//-------------------------------------------------------------------------------------------------
// Public query and lifecycle API
//-------------------------------------------------------------------------------------------------

/// @brief Returns whether the async worker is accepting normal log work.
/// @return True while the worker-running runtime flag is set.
bool GameWIP::Logger::isRunning()
{
    return (loggerState().runtimeStateBits.load(std::memory_order_acquire) & kRuntimeStateRunningBit) != 0;
}

/// @brief Returns the configured startup severity floor.
/// @return Minimum level copied during init().
LogLevel GameWIP::Logger::getMinLevel()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    return loggerState().minLevel;
}

/// @brief Returns the active output mode.
/// @return Output mode, including file setup fallback to Console.
OutputMode GameWIP::Logger::getOutput()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    return loggerState().mode;
}

/// @brief Returns the active log file path.
/// @return Current file path, or empty string when no file sink is active.
std::string GameWIP::Logger::getLogFilePath()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    return loggerState().logFilePath.string();
}

/// @brief Returns the effective queue and message limits selected during init.
/// @return Queue and message limit snapshot.
QueueLimits GameWIP::Logger::getQueueLimits()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    return QueueLimits{
        loggerState().softQueueSize,
        loggerState().hardQueueSize,
        loggerState().hardQueueMultiplier,
        loggerState().maxMessageLength,
        loggerState().inlineMessageCapacity,
        loggerState().workerBatchSize};
}

/// @brief Returns lifetime queue-pressure drop count.
/// @return Queue-drop count preserved for diagnostics.
std::size_t GameWIP::Logger::getLifetimeDroppedLogCount()
{
    return loggerState().droppedLogs.load(std::memory_order_relaxed);
}

/// @brief Returns the last logger result.
/// @return Last result recorded by init, filters, or sink failure handling.
LoggerResult GameWIP::Logger::getLastResult()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    return loggerState().lastResult;
}

/// @brief Returns the last platform error details.
/// @return Last platform error, or source None when no platform error is recorded.
GameWIP::Logger::Types::PlatformError GameWIP::Logger::getLastPlatformError()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    return loggerState().lastPlatformError;
}

/// @brief Returns a snapshot of resettable stats counters.
/// @return Relaxed atomic stats snapshot for diagnostics.
LoggerStats GameWIP::Logger::getStats()
{
    return snapshotStats();
}

/// @brief Returns a cold memory diagnostic snapshot.
/// @return Best-effort retained logger memory plus process memory when available.
LoggerMemoryStats GameWIP::Logger::getMemoryStats()
{
    LoggerMemoryStats memory;
    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        memory.queueStorageBytes = queueStorageBytesUnlocked();
        memory.messageArenaBytes = messageArenaBytesUnlocked();
        memory.sourceRegistryBytes = publishedSourceRegistryBytes();
        memory.entryTextHeapCapacityAvailable = entryTextHeapCapacityAvailableUnlocked();
        memory.entryTextHeapCapacityBytes = memory.entryTextHeapCapacityAvailable ? entryTextHeapCapacityBytesUnlocked() : 0;
        memory.loggerRetainedBytes =
            sizeof(LoggerState) +
            memory.queueStorageBytes +
            memory.messageArenaBytes +
            memory.sourceRegistryBytes +
            memory.entryTextHeapCapacityBytes;
    }

    const GameWIP::LoggerDetail::Platform::ProcessMemory processMemory = GameWIP::LoggerDetail::Platform::queryProcessMemory();
    memory.processWorkingSetBytes = processMemory.workingSetBytes;
    memory.processPrivateBytes = processMemory.privateBytes;
    memory.processMemoryAvailable = processMemory.available;
    return memory;
}

/// @brief Resets visible stats counters while preserving lifetime queue-drop state.
/// @note Shutdown reporting uses droppedLogs, so resetStats() must not clear the lifetime queue-drop value.
void GameWIP::Logger::resetStats()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    resetAtomicStats(loggerState().queueDepth.load(std::memory_order_acquire));
}

/// @brief Builds the default startup configuration.
GameWIP::Logger::Types::Config GameWIP::Logger::defaultConfig()
{
    return Types::Config{};
}

/// @brief Builds a lower-retained-memory startup configuration.
GameWIP::Logger::Types::Config GameWIP::Logger::lowMemoryConfig()
{
    Types::Config config;
    config.maxQueueSize = 256;
    config.hardQueueMultiplier = 1.0;
    config.maxMessageLength = 1024;
    config.inlineMessageCapacity = 128;
    config.workerBatchSize = 64;
    config.releaseMessageMemoryAfterWrite = true;
    config.releaseStorageOnShutdown = true;
    return config;
}

/// @brief Builds a higher-throughput startup configuration.
GameWIP::Logger::Types::Config GameWIP::Logger::throughputConfig()
{
    Types::Config config;
    config.maxQueueSize = 4096;
    config.hardQueueMultiplier = 1.25;
    config.maxMessageLength = 4096;
    config.inlineMessageCapacity = 256;
    config.workerBatchSize = 512;
    config.releaseMessageMemoryAfterWrite = false;
    config.releaseStorageOnShutdown = false;
    return config;
}

/// @brief Starts the logger using defaultConfig().
LoggerResult GameWIP::Logger::initDefault()
{
    return init(defaultConfig());
}

/// @brief Starts a console-only logger.
LoggerResult GameWIP::Logger::initConsole(Types::Level minLevel)
{
    Types::Config config = defaultConfig();
    config.output = OutputMode::Console;
    config.minLevel = minLevel;
    return init(config);
}

/// @brief Starts a file-only logger.
LoggerResult GameWIP::Logger::initFile(std::string_view directory, Types::Level minLevel)
{
    Types::Config config = defaultConfig();
    config.output = OutputMode::File;
    config.minLevel = minLevel;
    config.logDirectory = directory;
    return init(config);
}

/// @brief Initializes the async logger and starts its worker thread.
/// @param config Startup configuration.
/// @return Success or the first non-fatal setup/configuration result.
LoggerResult GameWIP::Logger::init(const Types::Config &config)
{
    std::lock_guard<std::mutex> lifecycleLock(loggerState().lifecycleMutex);

    bool alreadyRunning = false;
    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        alreadyRunning = loggerState().workerRunning || loggerState().loggingThread.joinable();
        if (alreadyRunning)
        {
            setResultUnlocked(LoggerResult::AlreadyRunning);
        }
    }

    if (alreadyRunning)
    {
        return LoggerResult::AlreadyRunning;
    }

    if (!loggerState().shutdownRegistered)
    {
        std::atexit(shutdownLoggerAtExit);
        loggerState().shutdownRegistered = true;
    }

    LoggerResult initResult = LoggerResult::Success;
    PlatformError initPlatformError;

    if (!isValidOutputMode(config.output))
    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        loggerState().mode = OutputMode::None;
        loggerState().workerRunning = false;
        loggerState().sourceRegistry.store(std::shared_ptr<SourceRegistry>{}, std::memory_order_release);
        publishRuntimeStateUnlocked();
        loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
        setResultUnlocked(LoggerResult::InvalidOutputMode);
        return LoggerResult::InvalidOutputMode;
    }

    std::size_t softQueueSize = config.maxQueueSize;
    if (softQueueSize == 0)
    {
        softQueueSize = 4;
        preserveFirstInitResult(initResult, LoggerResult::InvalidQueueSize, initPlatformError);
    }

    std::size_t maxMessageLength = config.maxMessageLength;
    if (maxMessageLength == 0)
    {
        maxMessageLength = 512;
        preserveFirstInitResult(initResult, LoggerResult::InvalidMessageLength, initPlatformError);
    }

    const std::size_t inlineMessageCapacity = std::min(config.inlineMessageCapacity, maxMessageLength);
    double hardQueueMultiplier = config.hardQueueMultiplier;
    std::size_t hardQueueSize = effectiveHardQueueLimit(hardQueueMultiplier, softQueueSize, initResult);
    std::size_t workerBatchSize = effectiveWorkerBatchSize(config.workerBatchSize, hardQueueSize);

    if (!isValidLevel(config.minLevel))
    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        loggerState().mode = OutputMode::None;
        loggerState().workerRunning = false;
        loggerState().sourceRegistry.store(std::shared_ptr<SourceRegistry>{}, std::memory_order_release);
        publishRuntimeStateUnlocked();
        loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
        setResultUnlocked(LoggerResult::InvalidLevelFilter);
        return LoggerResult::InvalidLevelFilter;
    }

    std::uint8_t levelMask = kAllLevelMask;
    if (!prepareLevelMask(config.levelFilters, levelMask))
    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        loggerState().mode = OutputMode::None;
        loggerState().workerRunning = false;
        loggerState().sourceRegistry.store(std::shared_ptr<SourceRegistry>{}, std::memory_order_release);
        publishRuntimeStateUnlocked();
        loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
        setResultUnlocked(LoggerResult::InvalidLevelFilter);
        return LoggerResult::InvalidLevelFilter;
    }

    LoggerResult sourcePrepareResult = LoggerResult::Success;
    std::shared_ptr<SourceRegistry> sourceRegistry;
    if (!prepareSources(config.sources, config.sourceFilters, sourceRegistry, sourcePrepareResult))
    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        loggerState().mode = OutputMode::None;
        loggerState().workerRunning = false;
        loggerState().sourceRegistry.store(std::shared_ptr<SourceRegistry>{}, std::memory_order_release);
        publishRuntimeStateUnlocked();
        loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
        setResultUnlocked(sourcePrepareResult);
        return sourcePrepareResult;
    }

    if (config.output == OutputMode::None)
    {
        Types::Config disabledConfig = config;
        disabledConfig.output = OutputMode::None;

        {
            std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
            if (GameWIP::LoggerDetail::Platform::isFileOpen(loggerState().logFile))
            {
                GameWIP::LoggerDetail::Platform::closeFile(loggerState().logFile);
                loggerState().logFile = {};
            }
            loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
        }

        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        resetRuntimeStateUnlocked(
            disabledConfig,
            softQueueSize,
            hardQueueSize,
            hardQueueMultiplier,
            maxMessageLength,
            inlineMessageCapacity,
            workerBatchSize,
            levelMask,
            std::move(sourceRegistry),
            std::unique_ptr<QueueSlot[]>{},
            0,
            std::vector<QueuedLogEntry>{},
            std::unique_ptr<char[]>{},
            std::unique_ptr<char[]>{});
        setResultUnlocked(initResult, initPlatformError);
        return initResult;
    }

    {
        std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
        if (GameWIP::LoggerDetail::Platform::isFileOpen(loggerState().logFile))
        {
            GameWIP::LoggerDetail::Platform::closeFile(loggerState().logFile);
            loggerState().logFile = {};
        }
        loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
        loggerState().stdoutColorEnabledAtomic.store(false, std::memory_order_release);
        loggerState().stderrColorEnabledAtomic.store(false, std::memory_order_release);
    }

    std::unique_ptr<QueueSlot[]> ring;
    std::size_t ringSize = 0;
    std::vector<QueuedLogEntry> batch;
    std::unique_ptr<char[]> ringArena;
    std::unique_ptr<char[]> batchArena;
    if (!prepareQueueStorage(hardQueueSize, workerBatchSize, inlineMessageCapacity, ring, ringSize, batch, ringArena, batchArena))
    {
        softQueueSize = 4;
        hardQueueSize = softQueueSize;
        hardQueueMultiplier = 1.0;
        workerBatchSize = effectiveWorkerBatchSize(config.workerBatchSize, hardQueueSize);
        if (!prepareQueueStorage(hardQueueSize, workerBatchSize, inlineMessageCapacity, ring, ringSize, batch, ringArena, batchArena))
        {
            std::lock_guard<std::mutex> lock(loggerState().logMutex);
            loggerState().mode = OutputMode::None;
            loggerState().workerRunning = false;
            loggerState().sourceRegistry.store(std::shared_ptr<SourceRegistry>{}, std::memory_order_release);
            publishRuntimeStateUnlocked();
            loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
            setResultUnlocked(LoggerResult::InvalidQueueSize);
            return LoggerResult::InvalidQueueSize;
        }

        preserveFirstInitResult(initResult, LoggerResult::InvalidQueueSize, initPlatformError);
    }

    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        resetRuntimeStateUnlocked(
            config,
            softQueueSize,
            hardQueueSize,
            hardQueueMultiplier,
            maxMessageLength,
            inlineMessageCapacity,
            workerBatchSize,
            levelMask,
            std::move(sourceRegistry),
            std::move(ring),
            ringSize,
            std::move(batch),
            std::move(ringArena),
            std::move(batchArena));
        setResultUnlocked(initResult);
    }

    const bool wantsFile = hasFileOutput(config.output);
    bool fileSetupFailed = false;

    if (wantsFile)
    {
        try
        {
            const std::string logDirectoryText = config.logDirectory.empty() ? std::string(LOGGER_DEFAULT_DIRECTORY) : std::string(config.logDirectory);

            if (logDirectoryText.empty())
            {
                setOutputMode(outputModeAfterFileSetupFailure(config.output, config.fallbackToConsoleOnFileFailure));
                preserveFirstInitResult(initResult, LoggerResult::InvalidLogDirectory, initPlatformError);
                fileSetupFailed = true;
            }
            else
            {
                const PlatformError directoryError = GameWIP::LoggerDetail::Platform::createDirectories(logDirectoryText);
                if (hasPlatformError(directoryError))
                {
                    setOutputMode(outputModeAfterFileSetupFailure(config.output, config.fallbackToConsoleOnFileFailure));
                    preserveFirstInitResult(initResult, LoggerResult::FileSetupFailed, initPlatformError, directoryError);
                    fileSetupFailed = true;
                }
            }

            if (!fileSetupFailed)
            {
                PlatformError timeError;
                const std::string logFileBaseName = getCurrentTimeText("%Y-%m-%d_%H-%M-%S", timeError);
                if (hasPlatformError(timeError))
                {
                    preserveFirstInitResult(initResult, LoggerResult::PlatformCallFailed, initPlatformError, timeError);
                }

                constexpr std::size_t kMaxCollisionAttempts = 1024;
                bool opened = false;
                PlatformError lastOpenError;
                {
                    std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
                    for (std::size_t index = 0; index <= kMaxCollisionAttempts; ++index)
                    {
                        const std::string fileName = index == 0
                                                         ? std::string(logFileBaseName) + ".log"
                                                         : std::string(logFileBaseName) + "_" + std::to_string(index) + ".log";
                        std::string nativeCandidatePath = logDirectoryText;
                        if (!nativeCandidatePath.empty() && nativeCandidatePath.back() != '/' && nativeCandidatePath.back() != '\\')
                        {
                            nativeCandidatePath.push_back('\\');
                        }
                        nativeCandidatePath.append(fileName);
                        FileHandle candidateHandle;
                        const PlatformError openError = openFileExclusiveForLogger(nativeCandidatePath, candidateHandle);
                        lastOpenError = openError;
                        if (!hasPlatformError(openError))
                        {
                            loggerState().logFile = candidateHandle;
                            {
                                std::lock_guard<std::mutex> lock(loggerState().logMutex);
                                loggerState().logFilePath = nativeCandidatePath;
                            }
                            loggerState().fileOutputAvailableAtomic.store(true, std::memory_order_release);
                            opened = true;
                            break;
                        }
                    }
                }

                if (!opened)
                {
                    setOutputMode(outputModeAfterFileSetupFailure(config.output, config.fallbackToConsoleOnFileFailure));
                    preserveFirstInitResult(initResult, LoggerResult::FileOpenFailed, initPlatformError, lastOpenError);
                    fileSetupFailed = true;
                }
            }
        }
        catch (const std::exception &)
        {
            setOutputMode(outputModeAfterFileSetupFailure(config.output, config.fallbackToConsoleOnFileFailure));
            preserveFirstInitResult(initResult, LoggerResult::FileSetupFailed, initPlatformError);
            fileSetupFailed = true;
        }
        catch (...)
        {
            setOutputMode(outputModeAfterFileSetupFailure(config.output, config.fallbackToConsoleOnFileFailure));
            preserveFirstInitResult(initResult, LoggerResult::FileSetupFailed, initPlatformError);
            fileSetupFailed = true;
        }
    }

    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        if (loggerState().mode == OutputMode::None)
        {
            clearQueueUnlocked();
            if (loggerState().releaseStorageOnShutdown)
            {
                releaseRuntimeStorageUnlocked();
            }
            publishRuntimeStateUnlocked();
            setResultUnlocked(initResult, initPlatformError);
            return initResult;
        }

        loggerState().workerRunning = true;
        loggerState().workerBusy = false;
        publishRuntimeStateUnlocked();
    }

    try
    {
        loggerState().loggingThread = std::thread(loggerWorker);
    }
    catch (...)
    {
        {
            std::lock_guard<std::mutex> lock(loggerState().logMutex);
            loggerState().workerRunning = false;
            loggerState().workerBusy = false;
            loggerState().mode = OutputMode::None;
            loggerState().sourceRegistry.store(std::shared_ptr<SourceRegistry>{}, std::memory_order_release);
            publishRuntimeStateUnlocked();
            loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
            setResultUnlocked(LoggerResult::ThreadStartFailed);
        }
        waitForActiveProducersToLeave();

        {
            std::lock_guard<std::mutex> lock(loggerState().logMutex);
            clearQueueUnlocked();
            if (loggerState().releaseStorageOnShutdown)
            {
                releaseRuntimeStorageUnlocked();
            }
        }

        {
            std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
            if (GameWIP::LoggerDetail::Platform::isFileOpen(loggerState().logFile))
            {
                GameWIP::LoggerDetail::Platform::closeFile(loggerState().logFile);
                loggerState().logFile = {};
            }
        }

        {
            std::lock_guard<std::mutex> lock(loggerState().logMutex);
            loggerState().logFilePath.clear();
        }

        return LoggerResult::ThreadStartFailed;
    }

    recordResult(initResult, initPlatformError);

    return initResult;
}

//-------------------------------------------------------------------------------------------------
// Public flush and shutdown API
//-------------------------------------------------------------------------------------------------

/// @brief Waits until the queue drains, then flushes console and file sinks.
void GameWIP::Logger::flush()
{
    std::lock_guard<std::mutex> lifecycleLock(loggerState().lifecycleMutex);
    flushInternal();
}

/// @brief Waits until the queue drains or timeout expires, then flushes console and file sinks.
/// @param timeout Maximum wait duration.
/// @return True when queued work drained and sink flushing succeeded before timeout expired.
bool GameWIP::Logger::flush(std::chrono::milliseconds timeout)
{
    std::lock_guard<std::mutex> lifecycleLock(loggerState().lifecycleMutex);
    return flushInternal(timeout);
}

/// @brief Stops the worker, drains queued logs, and closes the file sink.
/// @note Safe to call repeatedly and safe before init().
void GameWIP::Logger::shutdown()
{
    std::lock_guard<std::mutex> lifecycleLock(loggerState().lifecycleMutex);

    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        loggerState().workerRunning = false;
        publishRuntimeStateUnlocked();
    }

    loggerState().logCondition.notify_all();

    if (loggerState().loggingThread.joinable())
    {
        loggerState().loggingThread.join();
    }

    flushInternal();

    {
        std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
        if (GameWIP::LoggerDetail::Platform::isFileOpen(loggerState().logFile))
        {
            GameWIP::LoggerDetail::Platform::closeFile(loggerState().logFile);
            loggerState().logFile = {};
        }
        loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
        loggerState().stdoutColorEnabledAtomic.store(false, std::memory_order_release);
        loggerState().stderrColorEnabledAtomic.store(false, std::memory_order_release);
    }

    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        loggerState().logFilePath.clear();
        loggerState().mode = OutputMode::None;
        loggerState().workerBusy = false;
        loggerState().sourceRegistry.store(std::shared_ptr<SourceRegistry>{}, std::memory_order_release);
        clearQueueUnlocked();
        if (loggerState().releaseStorageOnShutdown)
        {
            releaseRuntimeStorageUnlocked();
        }
        publishRuntimeStateUnlocked();
    }
}
