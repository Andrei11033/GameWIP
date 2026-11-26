/// @file initialization.cpp
/// @brief Logger initialization, configuration preparation, and startup.

#include "logger/internal/core.h"
#include "logger/logging.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace GameWIP::Logger::Internal::Core
{
    namespace
    {
        constexpr std::string_view kDefaultLogDirectory = "logs";

        [[nodiscard]] bool validUtf8(std::string_view text) noexcept
        {
            return Unicode::Utf8::validate(text).outcome == Unicode::Types::ValidationOutcome::Valid;
        }

    } // namespace

    void waitForActiveProducersToLeave()
    {
        while(loggerState().activeProducers.load(std::memory_order_acquire) != 0)
        {
            std::this_thread::yield();
        }
    }

    namespace
    {
        /// ------------------------------------------------------------
        /// Init configuration preparation
        /// ------------------------------------------------------------

        struct PreparedInitConfig
        {
            // Effective queue and message limits
            std::size_t softQueueSize = 0;
            std::size_t hardQueueSize = 0;
            double hardQueueMultiplier = 1.0;
            std::size_t maxMessageLength = 0;
            std::size_t inlineMessageCapacity = 0;
            std::size_t workerBatchSize = 0;

            // Compiled filters and source metadata
            std::shared_ptr<const std::string> layoutTemplate;
            std::uint8_t levelMask = kAllLevelMask;
            std::shared_ptr<SourceRegistry> sourceRegistry;
        };

        struct PreparedQueueStorage
        {
            // Queue slots and worker batch
            std::unique_ptr<QueueSlot[]> ring;
            std::size_t ringSize = 0;
            std::vector<QueuedLogEntry> batch;

            // Message arenas owned by those storage regions
            std::unique_ptr<char[]> ringArena;
            std::unique_ptr<char[]> batchArena;
        };

        [[nodiscard]] bool normalizeInitConfig(
            const Types::Config &config,
            PreparedInitConfig &prepared,
            Types::Init::Adjustment &adjustments,
            Status &outStatus)
        {
            // Reject invalid enum values, themes, layouts, and timestamp formats.
            if(!isValidOutputMode(config.output) || !isValidLevel(config.minLevel) || !isValidFormatPolicy(config.formatPolicy) ||
               !Internal::isValidFieldLayout(config.fieldLayout) || !Internal::isValidTheme(config.theme) ||
               !Internal::isValidTimestampFormat(config.timestampFormat) || !Internal::isValidLayoutTemplate(config.layout.text))
            {
                outStatus = makeStatus(ErrorCode::InvalidArgument);
                return false;
            }

            // Validate retained strings before compiling or copying them.
            if(Unicode::Utf8::validate(config.layout.text).outcome != Unicode::Types::ValidationOutcome::Valid)
            {
                outStatus = makeStatus(ErrorCode::EncodingFailed);
                return false;
            }

            if(Unicode::Utf8::validate(config.timestampFormat).outcome != Unicode::Types::ValidationOutcome::Valid)
            {
                outStatus = makeStatus(ErrorCode::EncodingFailed);
                return false;
            }

            prepared.layoutTemplate = std::make_shared<const std::string>(Internal::compileLayoutTemplate(config.layout.text));

            if(!config.logDirectory.empty() && !validUtf8(config.logDirectory))
            {
                outStatus = makeStatus(ErrorCode::EncodingFailed);
                return false;
            }

            // Normalize capacities and prepare queue policy.
            prepared.softQueueSize = config.maxQueueSize;
            if(prepared.softQueueSize == 0)
            {
                prepared.softQueueSize = 4;
                adjustments |= Types::Init::Adjustment::QueueLimitsAdjusted;
            }

            prepared.maxMessageLength = config.maxMessageLength;
            if(prepared.maxMessageLength == 0)
            {
                prepared.maxMessageLength = 512;
                adjustments |= Types::Init::Adjustment::MessageLengthAdjusted;
            }

            prepared.inlineMessageCapacity = std::min(config.inlineMessageCapacity, prepared.maxMessageLength);
            if(prepared.inlineMessageCapacity != config.inlineMessageCapacity)
            {
                adjustments |= Types::Init::Adjustment::InlineCapacityAdjusted;
            }

            prepared.hardQueueMultiplier = config.hardQueueMultiplier;
            bool hardQueueAdjusted = false;
            prepared.hardQueueSize = effectiveHardQueueLimit(prepared.hardQueueMultiplier, prepared.softQueueSize, hardQueueAdjusted);
            if(hardQueueAdjusted)
            {
                adjustments |= Types::Init::Adjustment::QueueLimitsAdjusted;
            }

            prepared.workerBatchSize = effectiveWorkerBatchSize(config.workerBatchSize, prepared.hardQueueSize);
            if(config.workerBatchSize != 0 && prepared.workerBatchSize != config.workerBatchSize)
            {
                adjustments |= Types::Init::Adjustment::WorkerBatchAdjusted;
            }

            // Prepare source and level filters before publishing the runtime state.
            prepared.levelMask = kAllLevelMask;
            if(!prepareLevelMask(config.levelFilters, prepared.levelMask))
            {
                outStatus = makeStatus(ErrorCode::InvalidArgument);
                return false;
            }

            if(!prepareSources(config.sources, config.sourceFilters, prepared.sourceRegistry, outStatus))
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
            if(prepareQueueStorage(
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

            if(config.workerBatchSize != 0 && prepared.workerBatchSize != config.workerBatchSize)
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

        /// ------------------------------------------------------------
        /// Runtime publication during init
        /// ------------------------------------------------------------

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
                std::move(prepared.layoutTemplate),
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
                std::move(prepared.layoutTemplate),
                std::move(prepared.sourceRegistry),
                std::move(storage.ring),
                storage.ringSize,
                std::move(storage.batch),
                std::move(storage.ringArena),
                std::move(storage.batchArena));
            resetHealthUnlocked(Types::Health::State::Healthy, config.output);
            publishRuntimeStateUnlocked();
        }

        /// ------------------------------------------------------------
        /// File output setup
        /// ------------------------------------------------------------

        void closeOpenLogFileForInit()
        {
            std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);

            if(!loggerState().logFile.isOpen())
            {
                return;
            }

            // Re-init is already replacing File ownership; a stale close failure should
            // not prevent the next runtime from starting.
            static_cast<void>(loggerState().logFile.close());
        }

        [[nodiscard]] bool setupFileOutputForInit(const Types::Config &config, Status &outStatus)
        {
            // Resolve the configured UTF-8 directory and create it if needed.
            FilePath directoryPath;
            const std::string directoryText = config.logDirectory.empty() ? std::string(kDefaultLogDirectory) : std::string(config.logDirectory);
            auto directory = FileSystem::pathFromUtf8(directoryText);
            if(!directory.ok())
            {
                outStatus = directory.status();
                return false;
            }
            directoryPath = std::move(directory.value());
            const auto directoryResult = FileSystem::createDirectories(
                directoryPath,
                FileSystem::Types::Directory::CreateOptions{
                    .succeedIfAlreadyExists = true,
                    .symlinkPolicy = FileSystem::Types::SymlinkPolicy::FollowAll});
            if(!directoryResult.ok())
            {
                outStatus = directoryResult.status();
                return false;
            }

            Status timeStatus;
            const std::string baseName = getCurrentTimeText("%Y-%m-%d_%H-%M-%S", timeStatus);
            if(!timeStatus.ok())
            {
                // Timestamp failure degrades diagnostics but does not make File unusable
                // if the backend can still create a collision-safe path from the fallback text.
                outStatus = firstFailure(outStatus, timeStatus);
                recordHealthFailure(Types::Health::FailureSource::TimeConversion, timeStatus, false);
            }

            // Create a unique log file while holding exclusive file ownership.
            constexpr std::size_t kMaxCollisionAttempts = 1024;
            Status lastOpenStatus = makeStatus(ErrorCode::OpenFailed);
            std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);

            for(std::size_t index = 0; index <= kMaxCollisionAttempts; ++index)
            {
                const std::string fileName = index == 0 ? baseName + ".log" : baseName + "_" + std::to_string(index) + ".log";
                auto fileNamePath = FileSystem::pathFromUtf8(fileName);
                if(!fileNamePath.ok())
                {
                    outStatus = firstFailure(outStatus, fileNamePath.status());
                    return false;
                }
                auto candidate = FileSystem::joinPath(directoryPath, fileNamePath.value());
                if(!candidate.ok())
                {
                    outStatus = firstFailure(outStatus, candidate.status());
                    return false;
                }
                lastOpenStatus = openFileExclusiveForLogger(candidate.value(), loggerState().logFile);
                if(lastOpenStatus.ok())
                {
                    {
                        std::lock_guard<std::mutex> lock(loggerState().logMutex);
                        loggerState().logFilePath = std::move(candidate.value());
                    }
                    loggerState().fileOutputAvailableAtomic.store(true, std::memory_order_release);
                    return true;
                }
            }

            outStatus = firstFailure(outStatus, lastOpenStatus);
            return false;
        }

        /// ------------------------------------------------------------
        /// Worker startup
        /// ------------------------------------------------------------

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
            catch(...)
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
                return makeStatus(ErrorCode::NativeFailure);
            }
        }
    } // namespace
    /// ------------------------------------------------------------
    /// Logger initialization
    /// ------------------------------------------------------------

    Types::Init::Result initImpl(const Types::Config &config)
    {
        Types::Init::Info info;
        info.requestedOutput = config.output;

        // Serialize lifecycle changes and reject a duplicate active initialization.
        std::lock_guard<std::mutex> lifecycleLock(loggerState().lifecycleMutex);
        {
            std::lock_guard<std::mutex> lock(loggerState().logMutex);

            if(loggerState().workerRunning || loggerState().loggingThread.joinable())
            {
                info.outcome = Types::Init::Outcome::Started;
                info.effectiveOutput = loggerState().mode;
                return Types::Init::Result::failure(makeStatus(ErrorCode::AlreadyOpen), std::move(info));
            }

            resetHealthUnlocked(Types::Health::State::Disabled, OutputMode::None);
        }

        if(!loggerState().shutdownRegistered)
        {
            std::atexit(shutdownLoggerAtExit);
            loggerState().shutdownRegistered = true;
        }

        // Keep startup rollback consistent across every failure path.
        const auto fail = [&](Status status)
        {
            info.outcome = Types::Init::Outcome::Disabled;
            info.effectiveOutput = OutputMode::None;

            std::lock_guard<std::mutex> lock(loggerState().logMutex);
            loggerState().workerRunning = false;
            loggerState().workerBusy = false;
            loggerState().mode = OutputMode::None;
            loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
            loggerState().sourceRegistry.store({}, std::memory_order_release);
            markHealthDisabledUnlocked();
            publishRuntimeStateUnlocked();
            return Types::Init::Result::failure(std::move(status), std::move(info));
        };

        // Validate options and compute the effective queue configuration.
        PreparedInitConfig prepared;
        Status preparationStatus;
        if(!normalizeInitConfig(config, prepared, info.adjustments, preparationStatus))
        {
            return fail(std::move(preparationStatus));
        }

        if(config.output == OutputMode::None)
        {
            std::lock_guard<std::mutex> lock(loggerState().logMutex);
            publishDisabledRuntimeUnlocked(config, std::move(prepared));
            info.outcome = Types::Init::Outcome::Disabled;
            info.effectiveOutput = OutputMode::None;
            return Types::Init::Result::success(std::move(info));
        }

        // Prepare storage before replacing the current runtime state.
        PreparedQueueStorage storage;
        if(!prepareInitQueueStorage(config, prepared, info.adjustments, storage))
        {
            return fail(makeStatus(ErrorCode::OutOfMemory));
        }

        closeOpenLogFileForInit();
        {
            std::lock_guard<std::mutex> lock(loggerState().logMutex);
            publishPreparedRuntimeUnlocked(config, std::move(prepared), std::move(storage));
        }

        if(hasFileOutput(config.output))
        {
            // Apply the configured fallback if file setup fails.
            Status outputSetupStatus;
            const bool fileOutputReady = setupFileOutputForInit(config, outputSetupStatus);
            if(!fileOutputReady)
            {
                info.outputSetupStatus = outputSetupStatus;
                const OutputMode fallback = outputModeAfterFileSetupFailure(config.output, config.fallbackToConsoleOnFileFailure);
                setOutputMode(fallback);
                recordHealthFailure(Types::Health::FailureSource::File, outputSetupStatus, false);
            }
        }

        {
            std::lock_guard<std::mutex> lock(loggerState().logMutex);
            info.effectiveOutput = loggerState().mode;
        }

        if(info.effectiveOutput == OutputMode::None)
        {
            const Status failureStatus = info.outputSetupStatus.value_or(makeStatus(ErrorCode::OpenFailed));
            info.outcome = Types::Init::Outcome::Disabled;

            std::lock_guard<std::mutex> lock(loggerState().logMutex);
            clearQueueUnlocked();

            if(loggerState().releaseStorageOnShutdown)
            {
                releaseRuntimeStorageUnlocked();
            }

            markHealthDisabledUnlocked();
            publishRuntimeStateUnlocked();
            return Types::Init::Result::failure(failureStatus, std::move(info));
        }

        // Start asynchronous delivery only after sinks and state are ready.
        const Status workerStatus = startWorkerThread();
        if(!workerStatus.ok())
        {
            return fail(workerStatus);
        }

        info.outcome = Types::Init::Outcome::Started;
        info.effectiveOutput = getOutput();
        return Types::Init::Result::success(std::move(info));
    }

} // namespace GameWIP::Logger::Internal::Core
