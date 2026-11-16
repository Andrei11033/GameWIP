/// @file sinks.cpp
/// @brief Logger console/file sink delivery and flushing.

#include "logger/internal/logger_core.h"

namespace GameWIP::Logger::Detail::Core
{
    namespace
    {
        [[nodiscard]] bool markFlushTimedOutIfExpired(const FlushDeadline *deadline, Types::FlushResult &result) noexcept
        {
            if(!deadlineExpired(deadline))
            {
                return false;
            }

            result.outcome = Types::FlushOutcome::TimedOut;
            return true;
        }

        [[nodiscard]] bool markSinkTimedOutIfExpired(const FlushDeadline *deadline, ReportSinkProgress &progress) noexcept
        {
            if(!deadlineExpired(deadline))
            {
                return false;
            }

            progress.timedOut = true;
            return true;
        }
    } // namespace

    bool deadlineExpired(const FlushDeadline *deadline) noexcept
    {
        return deadline != nullptr && std::chrono::steady_clock::now() >= *deadline;
    }

    FlushDeadline makeFlushDeadline(std::chrono::milliseconds timeout) noexcept
    {
        const FlushDeadline now = std::chrono::steady_clock::now();

        if(timeout.count() <= 0)
        {
            return now;
        }

        const auto available = FlushDeadline::max() - now;
        if(timeout >= std::chrono::duration_cast<std::chrono::milliseconds>(available))
        {
            return FlushDeadline::max();
        }
        return now + timeout;
    }

    bool lockBefore(std::unique_lock<std::mutex> &lock, FlushDeadline deadline)
    {
        while(!lock.try_lock())
        {
            if(std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            std::this_thread::yield();
        }

        return true;
    }

#if LOGGER_INTERNAL_TEST_HOOKS
    /// ------------------------------------------------------------
    /// File sink operations
    /// ------------------------------------------------------------

    Status forcedFileStatus(ErrorCode code) noexcept
    {
        return IO::makeStatus(code, 1);
    }

    Status forcedFatalPopupStatus() noexcept
    {
        return IO::makeStatus(ErrorCode::NativeFailure, 1);
    }

#endif

    Status openFileExclusiveForLogger(const FilePath &path, FileWriter &outWriter)
    {
        const FileSystem::Types::File::WriterOpenOptions options{
            .mode = FileSystem::Types::File::WriterMode::CreateNew,
            .share = FileSystem::Types::File::Share::Read,
            .symlinkPolicy = FileSystem::Types::SymlinkPolicy::FollowAll,
            .createParentDirectories = false,
            .flushOnClose = IO::Types::FlushMode::None};
        return outWriter.open(path, options);
    }

    Status writeFileForLogger(FileWriter &writer, std::string_view text)
    {
#if LOGGER_INTERNAL_TEST_HOOKS
        if(consumeTestHook(loggerTestHookState.nextFileWriteFailure))
        {
            return forcedFileStatus(ErrorCode::WriteFailed);
        }

#endif

        return IO::writeAllText(writer, text).status;
    }

    Status flushFileForLogger(FileWriter &writer)
    {
#if LOGGER_INTERNAL_TEST_HOOKS
        if(consumeTestHook(loggerTestHookState.nextFileFlushFailure))
        {
            return forcedFileStatus(ErrorCode::FlushFailed);
        }

#endif

        return writer.flush(IO::Types::FlushMode::Data);
    }

    /// ------------------------------------------------------------
    /// Synchronous report sink delivery
    /// ------------------------------------------------------------

    ReportSinkProgress writeReportSynchronously(
        LogLevel level,
        std::string_view source,
        std::string_view message,
        bool unknownSource,
        bool alreadyTruncated,
        const FlushDeadline *deadline,
        Color sourceColor)
    {
        ReportSinkProgress progress;

        if(!isValidLevel(level))
        {
            progress.status = IO::makeStatus(ErrorCode::InvalidArgument);
            return progress;
        }

        if(markSinkTimedOutIfExpired(deadline, progress))
        {
            return progress;
        }

        try
        {
            // Bound and format the message before taking the sink lock.
            const OutputMode mode = runtimeStateOutput(loggerState().runtimeStateBits.load(std::memory_order_acquire));
            const bool consoleOutput = hasConsoleOutput(mode);
            const bool fileOutput = hasFileOutput(mode);

            progress.eligible = static_cast<std::size_t>(consoleOutput) + static_cast<std::size_t>(fileOutput);

            if(progress.eligible == 0)
            {
                return progress;
            }

            std::string boundedScratch;
            bool truncatedNow = false;
            const std::string_view messageText = boundedMessageView(message, alreadyTruncated, boundedScratch, truncatedNow);
            const bool truncated = alreadyTruncated || truncatedNow;
            TimestampCache timestamp;
            std::string line;
            const LogStyle style = getLogStyle(level);
            buildLogLine(line, getTimestampText(timestamp), style.text, source, messageText);

            Status consoleStatus;
            Status fileStatus;

            // Deliver to each configured sink while output is serialized.
            {
                std::unique_lock<std::mutex> outputLock(loggerState().outputMutex, std::defer_lock);

                if(deadline == nullptr)
                {
                    outputLock.lock();
                }
                else if(!lockBefore(outputLock, *deadline))
                {
                    progress.timedOut = true;
                    return progress;
                }

                if((consoleOutput) && (!markSinkTimedOutIfExpired(deadline, progress)))
                {
                    consoleStatus = writeConsoleLine(style, line, levelColor(level), sourceColor);

                    if(consoleStatus.ok())
                    {
                        ++progress.delivered;
                    }
                    else
                    {
                        progress.status = firstFailure(std::move(progress.status), consoleStatus);
                    }
                }

                if(fileOutput && !progress.timedOut)
                {
                    if(!markSinkTimedOutIfExpired(deadline, progress) && loggerState().fileOutputAvailableAtomic.load(std::memory_order_acquire) &&
                       loggerState().logFile.isOpen())
                    {
                        std::string fileLine = plainOutputText(line);
                        fileLine.push_back('\n');
                        fileStatus = writeFileForLogger(loggerState().logFile, fileLine);

                        if(fileStatus.ok())
                        {
                            ++progress.delivered;
                        }
                        else
                        {
                            progress.status = firstFailure(std::move(progress.status), fileStatus);
                        }
                    }
                    else if(!progress.timedOut)
                    {
                        fileStatus = IO::makeStatus(ErrorCode::NotOpen);
                        progress.status = firstFailure(std::move(progress.status), fileStatus);
                    }
                }
            }

            // Record sink failures and delivery statistics after unlocking.
            if(!consoleStatus.ok())
            {
                recordHealthFailure(Types::Health::FailureSource::Console, consoleStatus, true);
            }

            if(!fileStatus.ok())
            {
                loggerState().stats.fileWriteFailures.fetch_add(1, std::memory_order_relaxed);
                recordHealthFailure(Types::Health::FailureSource::File, fileStatus, true);
            }

            if(progress.delivered > 0)
            {
                loggerState().stats.written.fetch_add(1, std::memory_order_relaxed);

                if(truncated)
                {
                    loggerState().stats.truncated.fetch_add(1, std::memory_order_relaxed);
                }

                if(unknownSource)
                {
                    recordUnknownSourceUse();
                }
            }
        }
        catch(const std::bad_alloc &)
        {
            progress.status = firstFailure(std::move(progress.status), IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch(...)
        {
            progress.status = firstFailure(std::move(progress.status), IO::makeStatus(ErrorCode::Unknown));
        }

        return progress;
    }

    ReportSinkProgress writeReportSynchronously(
        LogLevel level,
        SourceId source,
        std::string_view message,
        bool alreadyTruncated,
        const FlushDeadline *deadline)
    {
        const auto registry = loadSourceRegistry();
        bool unknown = false;
        Color sourceColor = loggerState().theme.area;
        if(const RegisteredSource *registered = registry ? findSource(*registry, source) : nullptr; registered && registered->color)
        {
            sourceColor = *registered->color;
        }
        return writeReportSynchronously(
            level,
            findSourceName(registry.get(), source, unknown),
            message,
            unknown,
            alreadyTruncated,
            deadline,
            sourceColor);
    }

    /// ------------------------------------------------------------
    /// Normal queued sink delivery
    /// ------------------------------------------------------------

    SinkWriteResult writeLogEntry(const QueuedLogEntry &entry, TimestampCache &timestamp, std::string &lineScratch, std::string &fileBatchScratch)
    {
        SinkWriteResult result;
        const std::uint32_t runtime = loggerState().runtimeStateBits.load(std::memory_order_acquire);
        const OutputMode mode = runtimeStateOutput(runtime);

        // Recheck the current runtime filters before resolving source metadata.
        if(mode == OutputMode::None || !isValidLevel(entry.level))
        {
            return result;
        }

        if(toLevelValue(entry.level) < toLevelValue(runtimeStateMinLevel(runtime)))
        {
            return result;
        }

        const auto registry = entry.usesRegisteredSource ? loadSourceRegistry() : std::shared_ptr<SourceRegistry>{};
        const std::uint8_t bit = levelBit(entry.level);

        if(bit == 0 || (runtimeStateLevelMask(runtime) & bit) == 0 ||
           (entry.usesRegisteredSource && !sourceEnabledRuntime(registry.get(), entry.sourceId)))
        {
            return result;
        }

        const LogStyle style = getLogStyle(entry.level);
        bool unknown = false;
        const std::string_view source = resolveSourceText(entry, registry.get(), unknown);
        const RegisteredSource *registeredSource = entry.usesRegisteredSource && registry ? findSource(*registry, entry.sourceId) : nullptr;
        const Color sourceColor = registeredSource && registeredSource->color ? *registeredSource->color : loggerState().theme.area;

        // Format once, then deliver to the immediate console and queued file sink.
        buildLogLine(lineScratch, getTimestampText(timestamp), style.text, source, entry.message.view());

        if(hasConsoleOutput(mode))
        {
            Status consoleStatus;
            {
                std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
                consoleStatus = writeConsoleLine(style, lineScratch, levelColor(entry.level), sourceColor);
            }

            result.acceptedImmediateSink = consoleStatus.ok();

            if(!consoleStatus.ok())
            {
                recordHealthFailure(Types::Health::FailureSource::Console, consoleStatus, true);
            }
        }

        if((hasFileOutput(mode)) && (loggerState().fileOutputAvailableAtomic.load(std::memory_order_acquire)))
        {
            fileBatchScratch.append(plainOutputText(lineScratch));
            fileBatchScratch.push_back('\n');
            result.queuedFile = true;
        }

        // Count unregistered source IDs for diagnostics.
        if(unknown)
        {
            recordUnknownSourceUse();
        }

        return result;
    }

    /// ------------------------------------------------------------
    /// Sink flushing and report orchestration
    /// ------------------------------------------------------------

    bool flushFileBatch(std::string &fileBatchScratch, bool forceFlush)
    {
        if(fileBatchScratch.empty())
        {
            return true;
        }

        Status status = IO::makeStatus(ErrorCode::NotOpen);

        // Write and optionally flush the completed file batch.
        {
            std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);

            if(loggerState().logFile.isOpen())
            {
                status = writeFileForLogger(loggerState().logFile, fileBatchScratch);

                if(status.ok() && (forceFlush || loggerState().flushFileEveryBatchAtomic.load(std::memory_order_acquire)))
                {
                    status = flushFileForLogger(loggerState().logFile);
                }
            }
        }

        fileBatchScratch.clear();

        if(!status.ok())
        {
            loggerState().stats.fileWriteFailures.fetch_add(1, std::memory_order_relaxed);
            recordHealthFailure(Types::Health::FailureSource::File, status, true);
            return false;
        }

        return true;
    }

    Types::FlushResult flushSinksInternal(const FlushDeadline *deadline)
    {
        Types::FlushResult result;
        const OutputMode mode = runtimeStateOutput(loggerState().runtimeStateBits.load(std::memory_order_acquire));
        Status consoleStatus;
        Status fileStatus;

        std::unique_lock<std::mutex> outputLock(loggerState().outputMutex, std::defer_lock);

        if(deadline == nullptr)
        {
            outputLock.lock();
        }
        else if(!lockBefore(outputLock, *deadline))
        {
            result.outcome = Types::FlushOutcome::TimedOut;
            return result;
        }

        // Flush console streams first, then the configured file sink if time remains.
        if((hasConsoleOutput(mode)) && (!markFlushTimedOutIfExpired(deadline, result)))
        {
            Status stdoutStatus = Terminal::flush(Terminal::Types::Output::Stream::Stdout, IO::Types::FlushMode::Data);
            Status stderrStatus;

            if(!deadlineExpired(deadline))
            {
                stderrStatus = Terminal::flush(Terminal::Types::Output::Stream::Stderr, IO::Types::FlushMode::Data);
            }
            else
            {
                result.outcome = Types::FlushOutcome::TimedOut;
            }

            consoleStatus = firstFailure(std::move(stdoutStatus), stderrStatus);
            result.status = firstFailure(std::move(result.status), consoleStatus);
        }

        if(hasFileOutput(mode) && result.outcome != Types::FlushOutcome::TimedOut)
        {
            if(!markFlushTimedOutIfExpired(deadline, result) && loggerState().fileOutputAvailableAtomic.load(std::memory_order_acquire) &&
               loggerState().logFile.isOpen())
            {
                fileStatus = flushFileForLogger(loggerState().logFile);
                result.status = firstFailure(std::move(result.status), fileStatus);
            }
            else if(result.outcome != Types::FlushOutcome::TimedOut)
            {
                fileStatus = IO::makeStatus(ErrorCode::NotOpen);
                result.status = firstFailure(std::move(result.status), fileStatus);
            }
        }
        outputLock.unlock();

        // Update health only after releasing the output lock.
        if(!consoleStatus.ok())
        {
            recordHealthFailure(Types::Health::FailureSource::Console, consoleStatus, true);
        }

        if(!fileStatus.ok())
        {
            loggerState().stats.fileWriteFailures.fetch_add(1, std::memory_order_relaxed);
            recordHealthFailure(Types::Health::FailureSource::File, fileStatus, true);
        }

        return result;
    }

    Types::FlushResult flushInternal(const FlushDeadline *deadline)
    {
        Types::FlushResult result;
#if LOGGER_INTERNAL_TEST_HOOKS
        if(deadline && consumeTestHook(loggerTestHookState.nextTimedFlushTimeout))
        {
            result.outcome = Types::FlushOutcome::TimedOut;
            return result;
        }

#endif
        // Wait for accepted queue work to finish before flushing the sinks.
        {
            std::unique_lock<std::mutex> lock(loggerState().logMutex, std::defer_lock);

            if(deadline == nullptr)
            {
                lock.lock();
                loggerState().logCondition.wait(
                    lock,
                    []
                    {
                        return (!loggerState().workerRunning && !loggerState().workerBusy) ||
                               (loggerState().queueDepth.load(std::memory_order_acquire) == 0 && !loggerState().workerBusy);
                    });
            }
            else
            {
                if(!lockBefore(lock, *deadline))
                {
                    result.outcome = Types::FlushOutcome::TimedOut;
                    return result;
                }

                if(!loggerState().logCondition.wait_until(
                       lock,
                       *deadline,
                       []
                       {
                           return (!loggerState().workerRunning && !loggerState().workerBusy) ||
                                  (loggerState().queueDepth.load(std::memory_order_acquire) == 0 && !loggerState().workerBusy);
                       }))
                {
                    result.outcome = Types::FlushOutcome::TimedOut;
                    return result;
                }
            }
        }

        return flushSinksInternal(deadline);
    }

} // namespace GameWIP::Logger::Detail::Core
