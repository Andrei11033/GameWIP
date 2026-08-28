/// @file logger_state.cpp
/// @brief Process-wide Logger state, configuration, lifecycle transitions, and health.

#include "logger/internal/logger_core.h"

namespace GameWIP::Logger::Detail::Core
{
    namespace
    {
        constexpr std::string_view kDefaultLogDirectory = "logs";

        [[nodiscard]] bool validUtf8(std::string_view text) noexcept
        {
            return Unicode::Utf8::validate(text).outcome == Unicode::Types::ValidationOutcome::Valid;
        }

        [[nodiscard]] Types::Init::Result failedInitAfterException(const Types::Config &config, ErrorCode code) noexcept
        {
            Types::Init::Result result;
            result.status = IO::makeStatus(code);
            result.requestedOutput = config.output;
            try
            {
                if (GameWIP::Logger::running())
                {
                    result.outcome = Types::Init::Outcome::Started;
                    result.effectiveOutput = GameWIP::Logger::getOutput();
                }
                else
                {
                    static_cast<void>(GameWIP::Logger::shutdown());
                }
            }
            catch (...)
            {
                result.outcome = Types::Init::Outcome::Disabled;
                result.effectiveOutput = OutputMode::None;
            }
            return result;
        }
    } // namespace

    LoggerState &loggerState()
    {
        static LoggerState state;
        return state;
    }

    std::uint8_t toLevelValue(LogLevel level)
    {
        return static_cast<std::uint8_t>(level);
    }
    std::uint8_t toOutputModeValue(OutputMode mode)
    {
        return static_cast<std::uint8_t>(mode);
    }

    std::uint8_t toFormatPolicyValue(FormatPolicy policy)
    {
        return policy == FormatPolicy::FastNormal ? 1u : 0u;
    }

    FormatPolicy formatPolicyFromValue(std::uint8_t value)
    {
        return value == 1u ? FormatPolicy::FastNormal : FormatPolicy::StrictBounded;
    }

    bool isValidFormatPolicy(FormatPolicy policy)
    {
        return policy == FormatPolicy::StrictBounded || policy == FormatPolicy::FastNormal;
    }

    bool isValidLevel(LogLevel level)
    {
        return toLevelValue(level) < kLevelCount;
    }

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

    std::uint8_t levelBit(LogLevel level)
    {
        return isValidLevel(level) ? static_cast<std::uint8_t>(1u << toLevelValue(level)) : 0u;
    }

    OutputMode outputModeFromValue(std::uint8_t value)
    {
        const auto mode = static_cast<OutputMode>(value);
        return isValidOutputMode(mode) ? mode : OutputMode::None;
    }

    std::uint32_t packRuntimeState(bool running, OutputMode mode, LogLevel minLevel, std::uint8_t levelMask)
    {
        std::uint32_t packed = running ? kRuntimeStateRunningBit : 0u;
        packed |= (static_cast<std::uint32_t>(toOutputModeValue(mode)) & kRuntimeStateEnumMask) << kRuntimeStateOutputShift;
        packed |= (static_cast<std::uint32_t>(toLevelValue(minLevel)) & kRuntimeStateEnumMask) << kRuntimeStateMinLevelShift;
        packed |= (static_cast<std::uint32_t>(levelMask) & kRuntimeStateLevelMaskMask) << kRuntimeStateLevelMaskShift;
        return packed;
    }

    bool runtimeStateRunning(std::uint32_t packed)
    {
        return (packed & kRuntimeStateRunningBit) != 0;
    }
    OutputMode runtimeStateOutput(std::uint32_t packed)
    {
        return outputModeFromValue(static_cast<std::uint8_t>((packed >> kRuntimeStateOutputShift) & kRuntimeStateEnumMask));
    }
    LogLevel runtimeStateMinLevel(std::uint32_t packed)
    {
        return static_cast<LogLevel>((packed >> kRuntimeStateMinLevelShift) & kRuntimeStateEnumMask);
    }
    std::uint8_t runtimeStateLevelMask(std::uint32_t packed)
    {
        return static_cast<std::uint8_t>((packed >> kRuntimeStateLevelMaskShift) & kRuntimeStateLevelMaskMask);
    }

    void cpuRelax() noexcept
    {
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
        _mm_pause();
#endif
    }

    void waitForQueueSlot(QueueSlot &slot, std::size_t ticket)
    {
        std::uint32_t spins = 0;
        while (slot.sequence.load(std::memory_order_acquire) != ticket)
        {
            if (spins++ < kQueueSlotSpinBeforeYield)
                cpuRelax();
            else
                std::this_thread::yield();
        }
    }

    void recordQueueDropCounter(std::atomic<std::size_t> &counter)
    {
        loggerState().droppedLogs.fetch_add(1, std::memory_order_relaxed);
        counter.fetch_add(1, std::memory_order_relaxed);
    }
    void recordDiagnosticFailureCounter(std::atomic<std::size_t> &counter)
    {
        counter.fetch_add(1, std::memory_order_relaxed);
    }
    void updateAtomicMax(std::atomic<std::size_t> &target, std::size_t value)
    {
        std::size_t current = target.load(std::memory_order_relaxed);
        while (current < value && !target.compare_exchange_weak(current, value, std::memory_order_relaxed, std::memory_order_relaxed))
        {
        }
    }

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

    std::size_t messageArenaBytesUnlocked()
    {
        if (loggerState().inlineMessageCapacity == 0)
            return 0;
        std::size_t bytes = 0;
        if (loggerState().ringMessageArena)
            bytes += loggerState().logRingSize * loggerState().inlineMessageCapacity;
        if (loggerState().batchMessageArena)
            bytes += loggerState().workerBatch.size() * loggerState().inlineMessageCapacity;
        return bytes;
    }

    std::size_t queueStorageBytesUnlocked()
    {
        return loggerState().logRingSize * sizeof(QueueSlot) + loggerState().workerBatch.capacity() * sizeof(QueuedLogEntry);
    }

    std::size_t sourceRegistryBytes(const SourceRegistry *registry)
    {
        if (!registry)
            return 0;
        std::size_t bytes = sizeof(SourceRegistry) + registry->sources.capacity() * sizeof(RegisteredSource) +
                            registry->directSourceLookup.capacity() * sizeof(std::size_t);
        const std::size_t emptyCapacity = std::string{}.capacity();
        for (const auto &source : registry->sources)
            if (source.name.capacity() > emptyCapacity)
                bytes += source.name.capacity();
        return bytes;
    }

    std::size_t publishedSourceRegistryBytes()
    {
        const auto registry = loggerState().sourceRegistry.load(std::memory_order_acquire);
        return sourceRegistryBytes(registry.get());
    }
    std::size_t entryTextHeapCapacityBytes(const QueuedLogEntry &entry)
    {
        return entry.sourceText.capacityBytes() + entry.message.capacityBytes();
    }
    bool entryTextHeapCapacityAvailableUnlocked()
    {
        return !loggerState().workerBusy && loggerState().queueDepth.load(std::memory_order_acquire) == 0 &&
               loggerState().publishedQueueDepth.load(std::memory_order_acquire) == 0 &&
               loggerState().activeProducers.load(std::memory_order_acquire) == 0;
    }
    std::size_t entryTextHeapCapacityBytesUnlocked()
    {
        if (!entryTextHeapCapacityAvailableUnlocked())
            return 0;
        std::size_t bytes = 0;
        for (std::size_t i = 0; i < loggerState().logRingSize; ++i)
            bytes += entryTextHeapCapacityBytes(loggerState().logRingView[i].entry);
        if (!loggerState().workerBusy)
            for (const auto &entry : loggerState().workerBatch)
                bytes += entryTextHeapCapacityBytes(entry);
        return bytes;
    }

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
    void waitForActiveProducersToLeave()
    {
        while (loggerState().activeProducers.load(std::memory_order_acquire) != 0)
            std::this_thread::yield();
    }
    void countAllocationFailure()
    {
        recordDiagnosticFailureCounter(loggerState().stats.allocationFailures);
    }
    void countFormatFailure()
    {
        recordDiagnosticFailureCounter(loggerState().stats.formatFailures);
    }

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

    Status firstFailure(Status current, const Status &candidate)
    {
        if (current.ok() && !candidate.ok())
            return candidate;
        return current;
    }

    void resetHealthUnlocked(Types::Health::State state, OutputMode effectiveOutput)
    {
        loggerState().healthState = state;
        loggerState().mode = effectiveOutput;
        loggerState().lastFailureSource = Types::Health::FailureSource::None;
        loggerState().lastHealthError = ErrorCode::Success;
        loggerState().lastHealthNativeCode = 0;
        loggerState().healthFailureCount = 0;
    }

    void markHealthDisabledUnlocked()
    {
        loggerState().healthState = Types::Health::State::Disabled;
    }

    void recordHealthFailure(Types::Health::FailureSource source, const Status &status, bool disableChannel)
    {
        if (status.ok())
            return;
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        loggerState().lastFailureSource = source;
        loggerState().lastHealthError = status.code;
        loggerState().lastHealthNativeCode = status.nativeCode;
        ++loggerState().healthFailureCount;

        if (disableChannel)
        {
            switch (source)
            {
            case Types::Health::FailureSource::File:
                loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
                if (loggerState().mode == OutputMode::Both)
                    loggerState().mode = OutputMode::Console;
                else if (loggerState().mode == OutputMode::File)
                    loggerState().mode = OutputMode::None;
                break;
            case Types::Health::FailureSource::Console:
                if (loggerState().mode == OutputMode::Both)
                    loggerState().mode = OutputMode::File;
                else if (loggerState().mode == OutputMode::Console)
                    loggerState().mode = OutputMode::None;
                break;
            case Types::Health::FailureSource::DebugOutput:
                loggerState().debugOutputEnabled = false;
                loggerState().debugOutputEnabledAtomic.store(false, std::memory_order_release);
                break;
            case Types::Health::FailureSource::FatalPopup:
                loggerState().fatalPopupEnabled = false;
                loggerState().fatalPopupEnabledAtomic.store(false, std::memory_order_release);
                break;
            case Types::Health::FailureSource::None:
            case Types::Health::FailureSource::TimeConversion:
                break;
            }
        }

        loggerState().healthState = loggerState().mode == OutputMode::None ? Types::Health::State::Disabled : Types::Health::State::Degraded;
        publishRuntimeStateUnlocked();
    }

    LogStyle getLogStyle(LogLevel level)
    {
        const auto colored = [](const char *text, Terminal::Types::Style::BasicColor color, bool stderrStream)
        {
            Terminal::Types::Style::Request style;
            style.foreground = Terminal::basicColor(color);
            return LogStyle{text, style, stderrStream};
        };
        switch (level)
        {
        case LogLevel::Trace:
            return colored("TRACE", Terminal::Types::Style::BasicColor::BrightBlack, false);
        case LogLevel::Debug:
            return colored("DEBUG", Terminal::Types::Style::BasicColor::Cyan, false);
        case LogLevel::Info:
            return {"INFO", {}, false};
        case LogLevel::Warn:
            return colored("WARN", Terminal::Types::Style::BasicColor::Yellow, false);
        case LogLevel::Error:
            return colored("ERROR", Terminal::Types::Style::BasicColor::Red, true);
        case LogLevel::Fatal:
            return colored("FATAL", Terminal::Types::Style::BasicColor::Red, true);
        }
        return {};
    }
    bool isLowPriority(LogLevel level)
    {
        return level <= LogLevel::Warn;
    }
    bool hasConsoleOutput(OutputMode mode)
    {
        return mode == OutputMode::Console || mode == OutputMode::Both;
    }
    bool hasFileOutput(OutputMode mode)
    {
        return mode == OutputMode::File || mode == OutputMode::Both;
    }

    std::size_t computeHardQueueLimit(std::size_t softQueueSize, double multiplier)
    {
        const long double precise = static_cast<long double>(softQueueSize) * static_cast<long double>(multiplier);
        const long double maximum = static_cast<long double>(std::numeric_limits<std::size_t>::max());
        if (precise >= maximum)
            return std::numeric_limits<std::size_t>::max();
        return std::max(softQueueSize, static_cast<std::size_t>(std::ceil(precise)));
    }

    std::size_t effectiveHardQueueLimit(double &multiplier, std::size_t softQueueSize, bool &adjusted)
    {
        adjusted = false;
        if (!std::isfinite(multiplier) || multiplier < 1.0)
        {
            adjusted = true;
            multiplier = 1.0;
            return softQueueSize;
        }
        return computeHardQueueLimit(softQueueSize, multiplier);
    }

    std::size_t effectiveWorkerBatchSize(std::size_t requested, std::size_t hardLimit)
    {
        const std::size_t wanted = requested == 0 ? kDefaultWorkerBatchSize : requested;
        return std::clamp(wanted, std::size_t{1}, hardLimit);
    }

    void resetRuntimeStateUnlocked(
        const Types::Config &config,
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
        loggerState().formatPolicy = config.formatPolicy;
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
        // logRingSize is the allocation-time element count retained beside the owning array.
#if defined(__clang__)
#pragma clang unsafe_buffer_usage begin
#endif
        loggerState().logRingView = {loggerState().logRing.get(), loggerState().logRingSize};
#if defined(__clang__)
#pragma clang unsafe_buffer_usage end
#endif
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
        loggerState().formatPolicyAtomic.store(toFormatPolicyValue(config.formatPolicy), std::memory_order_release);
        loggerState().releaseMessageMemoryAfterWriteAtomic.store(config.releaseMessageMemoryAfterWrite, std::memory_order_release);
        loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
        publishRuntimeStateUnlocked();
    }

    void setOutputMode(OutputMode mode)
    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        loggerState().mode = mode;
        if (!hasFileOutput(mode))
            loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
        publishRuntimeStateUnlocked();
    }

    OutputMode outputModeAfterFileSetupFailure(OutputMode requested, bool fallbackToConsole)
    {
        return hasConsoleOutput(requested) || fallbackToConsole ? OutputMode::Console : OutputMode::None;
    }
} // namespace GameWIP::Logger::Detail::Core

using namespace GameWIP::Logger::Detail::Core;

// ------------------------------------------------------------
// State queries and statistics
// ------------------------------------------------------------

bool GameWIP::Logger::running() noexcept
{
    return runtimeStateRunning(loggerState().runtimeStateBits.load(std::memory_order_acquire));
}

GameWIP::Logger::Types::Level GameWIP::Logger::getMinLevel()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    return loggerState().minLevel;
}

GameWIP::Logger::Types::OutputMode GameWIP::Logger::getOutput()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    return loggerState().mode;
}

std::string GameWIP::Logger::getLogFilePath()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    const auto result = FileSystem::pathToUtf8(loggerState().logFilePath);
    return result.status.ok() ? result.utf8 : std::string{};
}

GameWIP::Logger::Types::QueueLimits GameWIP::Logger::getQueueLimits()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    return {
        loggerState().softQueueSize,
        loggerState().hardQueueSize,
        loggerState().hardQueueMultiplier,
        loggerState().maxMessageLength,
        loggerState().inlineMessageCapacity,
        loggerState().workerBatchSize};
}

std::size_t GameWIP::Logger::getLifetimeDroppedLogCount() noexcept
{
    return loggerState().droppedLogs.load(std::memory_order_relaxed);
}

GameWIP::Logger::Types::Health::Snapshot GameWIP::Logger::getHealth()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    return {
        loggerState().healthState,
        loggerState().mode,
        loggerState().lastFailureSource,
        loggerState().lastHealthError,
        loggerState().lastHealthNativeCode,
        loggerState().healthFailureCount};
}

GameWIP::Logger::Types::Stats GameWIP::Logger::getStats()
{
    return snapshotStats();
}

GameWIP::Logger::Types::MemoryStats GameWIP::Logger::getMemoryStats()
{
    LoggerMemoryStats memory;
    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        memory.queueStorageBytes = queueStorageBytesUnlocked();
        memory.messageArenaBytes = messageArenaBytesUnlocked();
        memory.sourceRegistryBytes = publishedSourceRegistryBytes();
        memory.entryTextHeapCapacityAvailable = entryTextHeapCapacityAvailableUnlocked();
        memory.entryTextHeapCapacityBytes = memory.entryTextHeapCapacityAvailable ? entryTextHeapCapacityBytesUnlocked() : 0;
        memory.loggerRetainedBytes = sizeof(LoggerState) + memory.queueStorageBytes + memory.messageArenaBytes + memory.sourceRegistryBytes +
                                     memory.entryTextHeapCapacityBytes;
    }
    const auto process = GameWIP::Logger::Detail::Platform::queryProcessMemory();
    memory.processWorkingSetBytes = process.workingSetBytes;
    memory.processPrivateBytes = process.privateBytes;
    memory.processMemoryAvailable = process.available;
    return memory;
}

void GameWIP::Logger::resetStats()
{
    std::lock_guard<std::mutex> lock(loggerState().logMutex);
    resetAtomicStats(loggerState().queueDepth.load(std::memory_order_acquire));
}

// ------------------------------------------------------------
// Configuration presets
// ------------------------------------------------------------

GameWIP::Logger::Types::Config GameWIP::Logger::defaultConfig() noexcept
{
    return {};
}
GameWIP::Logger::Types::Config GameWIP::Logger::lowMemoryConfig() noexcept
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
GameWIP::Logger::Types::Config GameWIP::Logger::throughputConfig() noexcept
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

// ------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------

GameWIP::Logger::Types::Init::Result GameWIP::Logger::initDefault() noexcept
{
    return init(defaultConfig());
}
GameWIP::Logger::Types::Init::Result GameWIP::Logger::initConsole(Types::Level minLevel) noexcept
{
    Types::Config config = defaultConfig();
    config.output = OutputMode::Console;
    config.minLevel = minLevel;
    return init(config);
}
GameWIP::Logger::Types::Init::Result GameWIP::Logger::initFile(std::string_view directory, Types::Level minLevel) noexcept
{
    Types::Config config = defaultConfig();
    config.output = OutputMode::File;
    config.minLevel = minLevel;
    config.logDirectory = directory;
    return init(config);
}

GameWIP::Logger::Types::Init::Result GameWIP::Logger::Detail::Core::initImpl(const Types::Config &config)
{
    Types::Init::Result result;
    result.requestedOutput = config.output;

    std::lock_guard<std::mutex> lifecycleLock(loggerState().lifecycleMutex);
    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        if (loggerState().workerRunning || loggerState().loggingThread.joinable())
        {
            result.status = IO::makeStatus(ErrorCode::AlreadyOpen);
            result.outcome = Types::Init::Outcome::Started;
            result.effectiveOutput = loggerState().mode;
            return result;
        }
        resetHealthUnlocked(Types::Health::State::Disabled, OutputMode::None);
    }

    if (!loggerState().shutdownRegistered)
    {
        std::atexit(shutdownLoggerAtExit);
        loggerState().shutdownRegistered = true;
    }

    const auto fail = [&](Status status)
    {
        result.status = std::move(status);
        result.outcome = Types::Init::Outcome::Disabled;
        result.effectiveOutput = OutputMode::None;
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        loggerState().workerRunning = false;
        loggerState().workerBusy = false;
        loggerState().mode = OutputMode::None;
        loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
        loggerState().sourceRegistry.store({}, std::memory_order_release);
        markHealthDisabledUnlocked();
        publishRuntimeStateUnlocked();
        return result;
    };

    if (!isValidOutputMode(config.output) || !isValidLevel(config.minLevel) || !isValidFormatPolicy(config.formatPolicy))
        return fail(IO::makeStatus(ErrorCode::InvalidArgument));
    if (!config.logDirectory.empty() && !validUtf8(config.logDirectory))
        return fail(IO::makeStatus(ErrorCode::EncodingFailed));

    std::size_t softQueueSize = config.maxQueueSize;
    if (softQueueSize == 0)
    {
        softQueueSize = 4;
        result.adjustments |= Types::Init::Adjustment::QueueLimitsAdjusted;
    }
    std::size_t maxMessageLength = config.maxMessageLength;
    if (maxMessageLength == 0)
    {
        maxMessageLength = 512;
        result.adjustments |= Types::Init::Adjustment::MessageLengthAdjusted;
    }
    std::size_t inlineMessageCapacity = std::min(config.inlineMessageCapacity, maxMessageLength);
    if (inlineMessageCapacity != config.inlineMessageCapacity)
        result.adjustments |= Types::Init::Adjustment::InlineCapacityAdjusted;

    double hardQueueMultiplier = config.hardQueueMultiplier;
    bool hardQueueAdjusted = false;
    std::size_t hardQueueSize = effectiveHardQueueLimit(hardQueueMultiplier, softQueueSize, hardQueueAdjusted);
    if (hardQueueAdjusted)
        result.adjustments |= Types::Init::Adjustment::QueueLimitsAdjusted;
    std::size_t workerBatchSize = effectiveWorkerBatchSize(config.workerBatchSize, hardQueueSize);
    if (config.workerBatchSize != 0 && workerBatchSize != config.workerBatchSize)
        result.adjustments |= Types::Init::Adjustment::WorkerBatchAdjusted;

    std::uint8_t levelMask = kAllLevelMask;
    if (!prepareLevelMask(config.levelFilters, levelMask))
        return fail(IO::makeStatus(ErrorCode::InvalidArgument));

    Status sourceStatus;
    std::shared_ptr<SourceRegistry> sourceRegistry;
    if (!prepareSources(config.sources, config.sourceFilters, sourceRegistry, sourceStatus))
        return fail(sourceStatus);

    if (config.output == OutputMode::None)
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
            {},
            0,
            {},
            {},
            {});
        resetHealthUnlocked(Types::Health::State::Disabled, OutputMode::None);
        publishRuntimeStateUnlocked();
        result.outcome = Types::Init::Outcome::Disabled;
        result.effectiveOutput = OutputMode::None;
        return result;
    }

    std::unique_ptr<QueueSlot[]> ring;
    std::size_t ringSize = 0;
    std::vector<QueuedLogEntry> batch;
    std::unique_ptr<char[]> ringArena;
    std::unique_ptr<char[]> batchArena;
    if (!prepareQueueStorage(hardQueueSize, workerBatchSize, inlineMessageCapacity, ring, ringSize, batch, ringArena, batchArena))
    {
        softQueueSize = 4;
        hardQueueSize = 4;
        hardQueueMultiplier = 1.0;
        workerBatchSize = effectiveWorkerBatchSize(config.workerBatchSize, hardQueueSize);
        result.adjustments |= Types::Init::Adjustment::QueueLimitsAdjusted;
        result.adjustments |= Types::Init::Adjustment::QueueStorageFallback;
        if (config.workerBatchSize != 0 && workerBatchSize != config.workerBatchSize)
            result.adjustments |= Types::Init::Adjustment::WorkerBatchAdjusted;
        if (!prepareQueueStorage(hardQueueSize, workerBatchSize, inlineMessageCapacity, ring, ringSize, batch, ringArena, batchArena))
            return fail(IO::makeStatus(ErrorCode::OutOfMemory));
    }

    {
        std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
        if (loggerState().logFile.isOpen())
            static_cast<void>(loggerState().logFile.close());
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
        resetHealthUnlocked(Types::Health::State::Healthy, config.output);
        publishRuntimeStateUnlocked();
    }

    if (hasFileOutput(config.output))
    {
        bool setupFailed = false;
        FilePath directoryPath;
        const std::string directoryText = config.logDirectory.empty() ? std::string(kDefaultLogDirectory) : std::string(config.logDirectory);
        auto directory = FileSystem::pathFromUtf8(directoryText);
        if (!directory.status.ok())
        {
            result.outputSetupStatus = directory.status;
            setupFailed = true;
        }
        else
            directoryPath = std::move(directory.path);

        if (!setupFailed)
        {
            Status status = FileSystem::createDirectories(
                directoryPath,
                FileSystem::Types::Directory::CreateOptions{
                    .succeedIfAlreadyExists = true,
                    .symlinkPolicy = FileSystem::Types::SymlinkPolicy::FollowAll});
            if (!status.ok())
            {
                result.outputSetupStatus = status;
                setupFailed = true;
            }
        }

        std::string baseName;
        if (!setupFailed)
        {
            Status timeStatus;
            baseName = getCurrentTimeText("%Y-%m-%d_%H-%M-%S", timeStatus);
            if (!timeStatus.ok())
            {
                result.outputSetupStatus = firstFailure(result.outputSetupStatus, timeStatus);
                recordHealthFailure(Types::Health::FailureSource::TimeConversion, timeStatus, false);
            }

            constexpr std::size_t kMaxCollisionAttempts = 1024;
            bool opened = false;
            Status lastOpenStatus = IO::makeStatus(ErrorCode::OpenFailed);
            std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
            for (std::size_t index = 0; index <= kMaxCollisionAttempts; ++index)
            {
                const std::string fileName = index == 0 ? baseName + ".log" : baseName + "_" + std::to_string(index) + ".log";
                auto fileNamePath = FileSystem::pathFromUtf8(fileName);
                if (!fileNamePath.status.ok())
                {
                    lastOpenStatus = fileNamePath.status;
                    break;
                }
                auto candidate = FileSystem::joinPath(directoryPath, fileNamePath.path);
                if (!candidate.status.ok())
                {
                    lastOpenStatus = candidate.status;
                    break;
                }
                lastOpenStatus = openFileExclusiveForLogger(candidate.path, loggerState().logFile);
                if (lastOpenStatus.ok())
                {
                    {
                        std::lock_guard<std::mutex> lock(loggerState().logMutex);
                        loggerState().logFilePath = std::move(candidate.path);
                    }
                    loggerState().fileOutputAvailableAtomic.store(true, std::memory_order_release);
                    opened = true;
                    break;
                }
            }
            if (!opened)
            {
                result.outputSetupStatus = firstFailure(result.outputSetupStatus, lastOpenStatus);
                setupFailed = true;
            }
        }

        if (setupFailed)
        {
            const OutputMode fallback = outputModeAfterFileSetupFailure(config.output, config.fallbackToConsoleOnFileFailure);
            setOutputMode(fallback);
            recordHealthFailure(Types::Health::FailureSource::File, result.outputSetupStatus, false);
        }
    }

    result.effectiveOutput = getOutput();
    if (result.effectiveOutput == OutputMode::None)
    {
        result.status = result.outputSetupStatus.ok() ? IO::makeStatus(ErrorCode::OpenFailed) : result.outputSetupStatus;
        result.outcome = Types::Init::Outcome::Disabled;
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        clearQueueUnlocked();
        if (loggerState().releaseStorageOnShutdown)
            releaseRuntimeStorageUnlocked();
        markHealthDisabledUnlocked();
        publishRuntimeStateUnlocked();
        return result;
    }

    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
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
            loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
            markHealthDisabledUnlocked();
            publishRuntimeStateUnlocked();
        }
        waitForActiveProducersToLeave();
        {
            std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
            if (loggerState().logFile.isOpen())
                static_cast<void>(loggerState().logFile.close());
        }
        return fail(IO::makeStatus(ErrorCode::NativeFailure));
    }

    result.outcome = Types::Init::Outcome::Started;
    result.effectiveOutput = getOutput();
    return result;
}

GameWIP::Logger::Types::Init::Result GameWIP::Logger::init(const Types::Config &config) noexcept
{
    try
    {
        return initImpl(config);
    }
    catch (const std::bad_alloc &)
    {
        return failedInitAfterException(config, ErrorCode::OutOfMemory);
    }
    catch (...)
    {
        return failedInitAfterException(config, ErrorCode::Unknown);
    }
}

GameWIP::Logger::Types::FlushResult GameWIP::Logger::flush(std::optional<std::chrono::milliseconds> timeout) noexcept
{
    try
    {
        Types::FlushResult invalid;
        if (timeout && timeout->count() < 0)
        {
            invalid.status = IO::makeStatus(ErrorCode::InvalidArgument);
            return invalid;
        }

        if (!timeout)
        {
            std::lock_guard<std::mutex> lifecycleLock(loggerState().lifecycleMutex);
            return flushInternal(nullptr);
        }

        const FlushDeadline deadline = makeFlushDeadline(*timeout);
        std::unique_lock<std::mutex> lifecycleLock(loggerState().lifecycleMutex, std::defer_lock);
        if (!lockBefore(lifecycleLock, deadline))
        {
            Types::FlushResult result;
            result.outcome = Types::FlushOutcome::TimedOut;
            return result;
        }
        return flushInternal(&deadline);
    }
    catch (const std::bad_alloc &)
    {
        Types::FlushResult result;
        result.status = IO::makeStatus(ErrorCode::OutOfMemory);
        return result;
    }
    catch (...)
    {
        Types::FlushResult result;
        result.status = IO::makeStatus(ErrorCode::Unknown);
        return result;
    }
}

GameWIP::IO::Types::Status GameWIP::Logger::Detail::Core::shutdownImpl()
{
    std::lock_guard<std::mutex> lifecycleLock(loggerState().lifecycleMutex);
    Status status;
    try
    {
        {
            std::lock_guard<std::mutex> lock(loggerState().logMutex);
            loggerState().workerRunning = false;
            publishRuntimeStateUnlocked();
        }
        loggerState().logCondition.notify_all();
        if (loggerState().loggingThread.joinable())
            loggerState().loggingThread.join();

        const Types::FlushResult flushResult = flushInternal(nullptr);
        status = firstFailure(std::move(status), flushResult.status);

        {
            std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
            if (loggerState().logFile.isOpen())
            {
                const Status closeStatus = loggerState().logFile.close();
                status = firstFailure(std::move(status), closeStatus);
            }
            loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
        }
    }
    catch (const std::bad_alloc &)
    {
        status = firstFailure(std::move(status), IO::makeStatus(ErrorCode::OutOfMemory));
    }
    catch (...)
    {
        status = firstFailure(std::move(status), IO::makeStatus(ErrorCode::Unknown));
    }

    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        loggerState().logFilePath.clear();
        loggerState().mode = OutputMode::None;
        loggerState().workerRunning = false;
        loggerState().workerBusy = false;
        loggerState().sourceRegistry.store({}, std::memory_order_release);
        clearQueueUnlocked();
        if (loggerState().releaseStorageOnShutdown)
            releaseRuntimeStorageUnlocked();
        markHealthDisabledUnlocked();
        publishRuntimeStateUnlocked();
    }
    return status;
}

GameWIP::IO::Types::Status GameWIP::Logger::shutdown() noexcept
{
    try
    {
        return shutdownImpl();
    }
    catch (const std::bad_alloc &)
    {
        return IO::makeStatus(ErrorCode::OutOfMemory);
    }
    catch (...)
    {
        return IO::makeStatus(ErrorCode::Unknown);
    }
}
