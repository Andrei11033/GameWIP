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

        // Keep init() noexcept at the API boundary while still preserving the best
        // lifecycle state we can recover after an internal exception.
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

    void waitForActiveProducersToLeave()
    {
        while (loggerState().activeProducers.load(std::memory_order_acquire) != 0)
        {
            std::this_thread::yield();
        }
    }

    namespace
    {
        // ------------------------------------------------------------
        // Init configuration preparation
        // ------------------------------------------------------------

        struct PreparedInitConfig
        {
            std::size_t softQueueSize = 0;
            std::size_t hardQueueSize = 0;
            double hardQueueMultiplier = 1.0;
            std::size_t maxMessageLength = 0;
            std::size_t inlineMessageCapacity = 0;
            std::size_t workerBatchSize = 0;
            std::uint8_t levelMask = kAllLevelMask;
            std::shared_ptr<SourceRegistry> sourceRegistry;
        };

        struct PreparedQueueStorage
        {
            std::unique_ptr<QueueSlot[]> ring;
            std::size_t ringSize = 0;
            std::vector<QueuedLogEntry> batch;
            std::unique_ptr<char[]> ringArena;
            std::unique_ptr<char[]> batchArena;
        };

        [[nodiscard]] bool normalizeInitConfig(
            const Types::Config &config,
            PreparedInitConfig &prepared,
            Types::Init::Adjustment &adjustments,
            Status &outStatus)
        {
            if (!isValidOutputMode(config.output) || !isValidLevel(config.minLevel) || !isValidFormatPolicy(config.formatPolicy))
            {
                outStatus = IO::makeStatus(ErrorCode::InvalidArgument);
                return false;
            }

            if (!config.logDirectory.empty() && !validUtf8(config.logDirectory))
            {
                outStatus = IO::makeStatus(ErrorCode::EncodingFailed);
                return false;
            }

            prepared.softQueueSize = config.maxQueueSize;
            if (prepared.softQueueSize == 0)
            {
                prepared.softQueueSize = 4;
                adjustments |= Types::Init::Adjustment::QueueLimitsAdjusted;
            }

            prepared.maxMessageLength = config.maxMessageLength;
            if (prepared.maxMessageLength == 0)
            {
                prepared.maxMessageLength = 512;
                adjustments |= Types::Init::Adjustment::MessageLengthAdjusted;
            }

            prepared.inlineMessageCapacity = std::min(config.inlineMessageCapacity, prepared.maxMessageLength);
            if (prepared.inlineMessageCapacity != config.inlineMessageCapacity)
            {
                adjustments |= Types::Init::Adjustment::InlineCapacityAdjusted;
            }

            prepared.hardQueueMultiplier = config.hardQueueMultiplier;
            bool hardQueueAdjusted = false;
            prepared.hardQueueSize = effectiveHardQueueLimit(prepared.hardQueueMultiplier, prepared.softQueueSize, hardQueueAdjusted);
            if (hardQueueAdjusted)
            {
                adjustments |= Types::Init::Adjustment::QueueLimitsAdjusted;
            }

            prepared.workerBatchSize = effectiveWorkerBatchSize(config.workerBatchSize, prepared.hardQueueSize);
            if (config.workerBatchSize != 0 && prepared.workerBatchSize != config.workerBatchSize)
            {
                adjustments |= Types::Init::Adjustment::WorkerBatchAdjusted;
            }

            prepared.levelMask = kAllLevelMask;
            if (!prepareLevelMask(config.levelFilters, prepared.levelMask))
            {
                outStatus = IO::makeStatus(ErrorCode::InvalidArgument);
                return false;
            }

            if (!prepareSources(config.sources, config.sourceFilters, prepared.sourceRegistry, outStatus))
            {
                return false;
            }

            return true;
        }

        [[nodiscard]] bool prepareInitQueueStorage(
            const Types::Config &config,
            PreparedInitConfig &prepared,
            Types::Init::Adjustment &adjustments,
            PreparedQueueStorage &storage)
        {
            if (prepareQueueStorage(
                    prepared.hardQueueSize,
                    prepared.workerBatchSize,
                    prepared.inlineMessageCapacity,
                    storage.ring,
                    storage.ringSize,
                    storage.batch,
                    storage.ringArena,
                    storage.batchArena))
            {
                return true;
            }

            // Keep Logger usable under memory pressure by falling back to the smallest
            // queue that still preserves the async worker and hard-limit behavior.
            prepared.softQueueSize = 4;
            prepared.hardQueueSize = 4;
            prepared.hardQueueMultiplier = 1.0;
            prepared.workerBatchSize = effectiveWorkerBatchSize(config.workerBatchSize, prepared.hardQueueSize);
            adjustments |= Types::Init::Adjustment::QueueLimitsAdjusted;
            adjustments |= Types::Init::Adjustment::QueueStorageFallback;

            if (config.workerBatchSize != 0 && prepared.workerBatchSize != config.workerBatchSize)
            {
                adjustments |= Types::Init::Adjustment::WorkerBatchAdjusted;
            }

            return prepareQueueStorage(
                prepared.hardQueueSize,
                prepared.workerBatchSize,
                prepared.inlineMessageCapacity,
                storage.ring,
                storage.ringSize,
                storage.batch,
                storage.ringArena,
                storage.batchArena);
        }

        // ------------------------------------------------------------
        // Runtime publication during init
        // ------------------------------------------------------------

        void publishDisabledRuntimeUnlocked(const Types::Config &config, PreparedInitConfig &&prepared)
        {
            resetRuntimeStateUnlocked(
                config,
                prepared.softQueueSize,
                prepared.hardQueueSize,
                prepared.hardQueueMultiplier,
                prepared.maxMessageLength,
                prepared.inlineMessageCapacity,
                prepared.workerBatchSize,
                prepared.levelMask,
                std::move(prepared.sourceRegistry),
                {},
                0,
                {},
                {},
                {});
            resetHealthUnlocked(Types::Health::State::Disabled, OutputMode::None);
            publishRuntimeStateUnlocked();
        }

        void publishPreparedRuntimeUnlocked(const Types::Config &config, PreparedInitConfig &&prepared, PreparedQueueStorage &&storage)
        {
            resetRuntimeStateUnlocked(
                config,
                prepared.softQueueSize,
                prepared.hardQueueSize,
                prepared.hardQueueMultiplier,
                prepared.maxMessageLength,
                prepared.inlineMessageCapacity,
                prepared.workerBatchSize,
                prepared.levelMask,
                std::move(prepared.sourceRegistry),
                std::move(storage.ring),
                storage.ringSize,
                std::move(storage.batch),
                std::move(storage.ringArena),
                std::move(storage.batchArena));
            resetHealthUnlocked(Types::Health::State::Healthy, config.output);
            publishRuntimeStateUnlocked();
        }

        // ------------------------------------------------------------
        // File output setup
        // ------------------------------------------------------------

        void closeOpenLogFileForInit()
        {
            std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
            if (!loggerState().logFile.isOpen())
            {
                return;
            }

            // Re-init is already replacing File ownership; a stale close failure should
            // not prevent the next runtime from starting.
            static_cast<void>(loggerState().logFile.close());
        }

        [[nodiscard]] bool setupFileOutputForInit(const Types::Config &config, Status &outStatus)
        {
            FilePath directoryPath;
            const std::string directoryText = config.logDirectory.empty() ? std::string(kDefaultLogDirectory) : std::string(config.logDirectory);
            auto directory = FileSystem::pathFromUtf8(directoryText);
            if (!directory.status.ok())
            {
                outStatus = directory.status;
                return false;
            }

            directoryPath = std::move(directory.path);
            Status status = FileSystem::createDirectories(
                directoryPath,
                FileSystem::Types::Directory::CreateOptions{
                    .succeedIfAlreadyExists = true,
                    .symlinkPolicy = FileSystem::Types::SymlinkPolicy::FollowAll});
            if (!status.ok())
            {
                outStatus = status;
                return false;
            }

            Status timeStatus;
            const std::string baseName = getCurrentTimeText("%Y-%m-%d_%H-%M-%S", timeStatus);
            if (!timeStatus.ok())
            {
                // Timestamp failure degrades diagnostics but does not make File unusable
                // if the backend can still create a collision-safe path from the fallback text.
                outStatus = firstFailure(outStatus, timeStatus);
                recordHealthFailure(Types::Health::FailureSource::TimeConversion, timeStatus, false);
            }

            constexpr std::size_t kMaxCollisionAttempts = 1024;
            Status lastOpenStatus = IO::makeStatus(ErrorCode::OpenFailed);
            std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
            for (std::size_t index = 0; index <= kMaxCollisionAttempts; ++index)
            {
                const std::string fileName = index == 0 ? baseName + ".log" : baseName + "_" + std::to_string(index) + ".log";
                auto fileNamePath = FileSystem::pathFromUtf8(fileName);
                if (!fileNamePath.status.ok())
                {
                    outStatus = firstFailure(outStatus, fileNamePath.status);
                    return false;
                }

                auto candidate = FileSystem::joinPath(directoryPath, fileNamePath.path);
                if (!candidate.status.ok())
                {
                    outStatus = firstFailure(outStatus, candidate.status);
                    return false;
                }

                lastOpenStatus = openFileExclusiveForLogger(candidate.path, loggerState().logFile);
                if (lastOpenStatus.ok())
                {
                    {
                        std::lock_guard<std::mutex> lock(loggerState().logMutex);
                        loggerState().logFilePath = std::move(candidate.path);
                    }
                    loggerState().fileOutputAvailableAtomic.store(true, std::memory_order_release);
                    return true;
                }
            }

            outStatus = firstFailure(outStatus, lastOpenStatus);
            return false;
        }

        // ------------------------------------------------------------
        // Worker startup
        // ------------------------------------------------------------

        [[nodiscard]] Status startWorkerThread()
        {
            {
                std::lock_guard<std::mutex> lock(loggerState().logMutex);
                loggerState().workerRunning = true;
                loggerState().workerBusy = false;
                publishRuntimeStateUnlocked();
            }

            try
            {
                loggerState().loggingThread = std::thread(loggerWorker);
                return {};
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
                closeOpenLogFileForInit();
                return IO::makeStatus(ErrorCode::NativeFailure);
            }
        }
    } // namespace

} // namespace GameWIP::Logger::Detail::Core

using namespace GameWIP::Logger::Detail::Core;

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

    PreparedInitConfig prepared;
    if (!normalizeInitConfig(config, prepared, result.adjustments, result.outputSetupStatus))
    {
        return fail(result.outputSetupStatus);
    }

    if (config.output == OutputMode::None)
    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        publishDisabledRuntimeUnlocked(config, std::move(prepared));
        result.outcome = Types::Init::Outcome::Disabled;
        result.effectiveOutput = OutputMode::None;
        return result;
    }

    PreparedQueueStorage storage;
    if (!prepareInitQueueStorage(config, prepared, result.adjustments, storage))
    {
        return fail(IO::makeStatus(ErrorCode::OutOfMemory));
    }

    closeOpenLogFileForInit();
    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        publishPreparedRuntimeUnlocked(config, std::move(prepared), std::move(storage));
    }

    if (hasFileOutput(config.output))
    {
        const bool fileOutputReady = setupFileOutputForInit(config, result.outputSetupStatus);
        if (!fileOutputReady)
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
        {
            releaseRuntimeStorageUnlocked();
        }

        markHealthDisabledUnlocked();
        publishRuntimeStateUnlocked();
        return result;
    }

    const Status workerStatus = startWorkerThread();
    if (!workerStatus.ok())
    {
        return fail(workerStatus);
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
        {
            loggerState().loggingThread.join();
        }

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
        {
            releaseRuntimeStorageUnlocked();
        }

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
