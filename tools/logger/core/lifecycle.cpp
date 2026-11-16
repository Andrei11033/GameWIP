/// @file lifecycle.cpp
/// @brief Logger initialization wrappers, flushing, and shutdown.

#include "logger/internal/logger_core.h"

#include <chrono>
#include <mutex>
#include <new>
#include <optional>
#include <utility>

namespace GameWIP::Logger::Detail::Core
{
    namespace
    {
        [[nodiscard]] Types::Init::Result failedInitAfterException(const Types::Config &config, ErrorCode code) noexcept
        {
            Types::Init::Result result;
            result.status = IO::makeStatus(code);
            result.requestedOutput = config.output;
            try
            {
                if(GameWIP::Logger::running())
                {
                    result.outcome = Types::Init::Outcome::Started;
                    result.effectiveOutput = GameWIP::Logger::getOutput();
                }
                else
                {
                    static_cast<void>(GameWIP::Logger::shutdown());
                }
            }
            catch(...)
            {
                result.outcome = Types::Init::Outcome::Disabled;
                result.effectiveOutput = OutputMode::None;
            }

            return result;
        }
    } // namespace

    /// @brief Callback registered with atexit that delegates to the idempotent public shutdown path.
    void shutdownLoggerAtExit()
    {
        GameWIP::Logger::shutdown();
    }
} // namespace GameWIP::Logger::Detail::Core

using namespace GameWIP::Logger::Detail::Core;

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
        Types::FlushResult invalid;

        // Reject invalid timeouts before taking the lifecycle lock.
        if(timeout && timeout->count() < 0)
        {
            invalid.status = IO::makeStatus(ErrorCode::InvalidArgument);
            return invalid;
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
            Types::FlushResult result;
            result.outcome = Types::FlushOutcome::TimedOut;
            return result;
        }
        return flushInternal(&deadline);
    }
    catch(const std::bad_alloc &)
    {
        Types::FlushResult result;
        result.status = IO::makeStatus(ErrorCode::OutOfMemory);
        return result;
    }
    catch(...)
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
        status = firstFailure(std::move(status), flushResult.status);

        {
            std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);

            if(loggerState().logFile.isOpen())
            {
                const Status closeStatus = loggerState().logFile.close();
                status = firstFailure(std::move(status), closeStatus);
            }
            loggerState().fileOutputAvailableAtomic.store(false, std::memory_order_release);
        }
    }
    catch(const std::bad_alloc &)
    {
        status = firstFailure(std::move(status), IO::makeStatus(ErrorCode::OutOfMemory));
    }
    catch(...)
    {
        status = firstFailure(std::move(status), IO::makeStatus(ErrorCode::Unknown));
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

GameWIP::IO::Types::Status GameWIP::Logger::shutdown() noexcept
{
    try
    {
        return shutdownImpl();
    }
    catch(const std::bad_alloc &)
    {
        return IO::makeStatus(ErrorCode::OutOfMemory);
    }
    catch(...)
    {
        return IO::makeStatus(ErrorCode::Unknown);
    }
}
