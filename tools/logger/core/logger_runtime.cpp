/// @file logger_runtime.cpp
/// @brief Process-wide Logger runtime state, health, output policy, and configuration normalization helpers.

#include "logger/internal/logger_core.h"

namespace GameWIP::Logger::Detail::Core
{
    // ------------------------------------------------------------
    // Shared runtime state and packed hot-path configuration
    // ------------------------------------------------------------

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

    // ------------------------------------------------------------
    // Queue coordination counters
    // ------------------------------------------------------------

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
            {
                cpuRelax();
            }
            else
            {
                std::this_thread::yield();
            }
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

    // ------------------------------------------------------------
    // Health state and output-channel availability
    // ------------------------------------------------------------

    Status firstFailure(Status current, const Status &candidate)
    {
        if (current.ok() && !candidate.ok())
        {
            return candidate;
        }

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
        {
            return;
        }

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
                {
                    loggerState().mode = OutputMode::Console;
                }
                else if (loggerState().mode == OutputMode::File)
                {
                    loggerState().mode = OutputMode::None;
                }
                break;
            case Types::Health::FailureSource::Console:
                if (loggerState().mode == OutputMode::Both)
                {
                    loggerState().mode = OutputMode::File;
                }
                else if (loggerState().mode == OutputMode::Console)
                {
                    loggerState().mode = OutputMode::None;
                }
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

    // ------------------------------------------------------------
    // Output policy helpers
    // ------------------------------------------------------------

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

    // ------------------------------------------------------------
    // Configuration limit normalization
    // ------------------------------------------------------------

    std::size_t computeHardQueueLimit(std::size_t softQueueSize, double multiplier)
    {
        const long double precise = static_cast<long double>(softQueueSize) * static_cast<long double>(multiplier);
        const long double maximum = static_cast<long double>(std::numeric_limits<std::size_t>::max());
        if (precise >= maximum)
        {
            return std::numeric_limits<std::size_t>::max();
        }

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

    // ------------------------------------------------------------
    // Runtime publication and reset
    // ------------------------------------------------------------

    // Rebuild every pointer and atomic mirror together so hot-path readers never
    // observe configuration from one init epoch paired with storage from another.
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
        // Keep the span bound next to the owning allocation so later queue code can
        // index raw slots without recomputing or trusting capacity-like state.
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
        {
            loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
        }

        publishRuntimeStateUnlocked();
    }

    OutputMode outputModeAfterFileSetupFailure(OutputMode requested, bool fallbackToConsole)
    {
        return hasConsoleOutput(requested) || fallbackToConsole ? OutputMode::Console : OutputMode::None;
    }

} // namespace GameWIP::Logger::Detail::Core
