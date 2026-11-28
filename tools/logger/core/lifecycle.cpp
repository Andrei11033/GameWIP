/// @file lifecycle.cpp
/// @brief Logger initialization wrappers, flushing, and shutdown.

#include "logger/internal/core.h"
#include "logger/logging.h"

#include <chrono>
#include <mutex>
#include <new>
#include <optional>
#include <utility>

namespace GameWIP::Logger::Internal::Core
{
    namespace
    {
        [[nodiscard]] Types::Init::Result failedInitAfterException(const Types::Config &config, ErrorCode code) noexcept
        {
            Types::Init::Info info;
            info.requestedOutput = config.output;
            try
            {
                if(GameWIP::Logger::running())
                {
                    info.outcome = Types::Init::Outcome::Started;
                    info.effectiveOutput = GameWIP::Logger::getOutput();
                }
                else
                {
                    static_cast<void>(GameWIP::Logger::shutdown());
                }
            }
            catch(...)
            {
                info.outcome = Types::Init::Outcome::Disabled;
                info.effectiveOutput = OutputMode::None;
            }

            return Types::Init::Result::failure(makeStatus(code), std::move(info));
        }
    } // namespace

    /// @brief Callback registered with atexit that delegates to the idempotent public shutdown path.
    void shutdownLoggerAtExit()
    {
        static_cast<void>(GameWIP::Logger::shutdown());
    }
} // namespace GameWIP::Logger::Internal::Core

using namespace GameWIP::Logger::Internal::Core;

/// ------------------------------------------------------------
/// Lifecycle
/// ------------------------------------------------------------

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

GameWIP::Logger::Types::Init::Result GameWIP::Logger::init(const Types::Config &config) noexcept
{
    try
    {
        return initImpl(config);
    }
    catch(const std::bad_alloc &)
    {
        return failedInitAfterException(config, ErrorCode::OutOfMemory);
    }
    catch(...)
    {
        return failedInitAfterException(config, ErrorCode::Unknown);
    }
}

GameWIP::Logger::Types::FlushResult GameWIP::Logger::flush(std::optional<std::chrono::milliseconds> timeout) noexcept
{
    try
    {
        // Reject invalid timeouts before taking the lifecycle lock.
        if(timeout && timeout->count() < 0)
        {
            return Types::FlushResult::failure(makeStatus(ErrorCode::InvalidArgument));
        }

        if(!timeout)
        {
            std::lock_guard<std::mutex> lifecycleLock(loggerState().lifecycleMutex);
            return flushInternal(nullptr);
        }

        // Use one deadline for lifecycle-lock acquisition and sink flushing.
        const FlushDeadline deadline = makeFlushDeadline(*timeout);
        std::unique_lock<std::mutex> lifecycleLock(loggerState().lifecycleMutex, std::defer_lock);

        if(!lockBefore(lifecycleLock, deadline))
        {
            return Types::FlushResult::success(Types::FlushInfo{.outcome = Types::FlushOutcome::TimedOut});
        }
        return flushInternal(&deadline);
    }
    catch(const std::bad_alloc &)
    {
        return Types::FlushResult::failure(makeStatus(ErrorCode::OutOfMemory));
    }
    catch(...)
    {
        return Types::FlushResult::failure(makeStatus(ErrorCode::Unknown));
    }
}

GameWIP::Status::Types::Status GameWIP::Logger::Internal::Core::shutdownImpl()
{
    std::lock_guard<std::mutex> lifecycleLock(loggerState().lifecycleMutex);
    Status status;
    try
    {
        // Stop accepting work and join the worker after it drains the queue.
        {
            std::lock_guard<std::mutex> lock(loggerState().logMutex);
            loggerState().workerRunning = false;
            publishRuntimeStateUnlocked();
        }
        loggerState().logCondition.notify_all();

        if(loggerState().loggingThread.joinable())
        {
            loggerState().loggingThread.join();
        }

        // Flush accepted output and close the file sink.
        const Types::FlushResult flushResult = flushInternal(nullptr);
        status = firstFailure(std::move(status), flushResult.status());

        {
            std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);

            if(loggerState().logFile.isOpen())
            {
                const Status closeStatus = loggerState().logFile.close().status();
                status = firstFailure(std::move(status), closeStatus);
            }
            loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
        }
    }
    catch(const std::bad_alloc &)
    {
        status = firstFailure(std::move(status), makeStatus(ErrorCode::OutOfMemory));
    }
    catch(...)
    {
        status = firstFailure(std::move(status), makeStatus(ErrorCode::Unknown));
    }

    // Clear published state and release configured runtime storage.
    {
        std::lock_guard<std::mutex> lock(loggerState().logMutex);
        loggerState().logFilePath.clear();
        loggerState().mode = OutputMode::None;
        loggerState().workerRunning = false;
        loggerState().workerBusy = false;
        loggerState().sourceRegistry.store({}, std::memory_order_release);
        clearQueueUnlocked();

        if(loggerState().releaseStorageOnShutdown)
        {
            releaseRuntimeStorageUnlocked();
        }

        markHealthDisabledUnlocked();
        publishRuntimeStateUnlocked();
    }

    return status;
}

GameWIP::Status::Types::Result<void> GameWIP::Logger::shutdown() noexcept
{
    try
    {
        const GameWIP::Status::Types::Status status = shutdownImpl();
        return resultFromStatus(status);
    }
    catch(const std::bad_alloc &)
    {
        return Status::Types::Result<void>::failure(makeStatus(ErrorCode::OutOfMemory));
    }
    catch(...)
    {
        return Status::Types::Result<void>::failure(makeStatus(ErrorCode::Unknown));
    }
}
