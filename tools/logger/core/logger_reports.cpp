/// @file logger_reports.cpp
/// @brief Normal sink writes, synchronous reports, bounded drains, fatal popup, and platform debugger mirroring.

#include "logger/internal/logger_core.h"

namespace GameWIP::Logger::Detail::Core
{
    FlushDeadline makeFlushDeadline(std::chrono::milliseconds timeout) noexcept
    {
        const FlushDeadline now = std::chrono::steady_clock::now();
        if (timeout.count() <= 0)
        {
            return now;
        }
        const auto available = FlushDeadline::max() - now;
        if (timeout >= std::chrono::duration_cast<std::chrono::milliseconds>(available))
        {
            return FlushDeadline::max();
        }
        return now + timeout;
    }

    bool lockBefore(std::unique_lock<std::mutex> &lock, FlushDeadline deadline) noexcept
    {
        while (!lock.try_lock())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }
            std::this_thread::yield();
        }
        return true;
    }

    /// @brief Writes one complete Logger console record through the shared Terminal runtime.
    /// @param style Severity style and stdout/stderr route.
    /// @param line Complete log line without its line ending.
    /// @return Terminal write status.
    GameWIP::IO::Types::Status writeConsoleLine(const LogStyle &style, std::string_view line)
    {
        const std::array<GameWIP::Terminal::Types::WriteSegment, 1> segments{GameWIP::Terminal::styledTextSegment(line, style.terminalStyle)};

        GameWIP::Terminal::Types::SegmentWriteOptions options;
        options.styleMode = loggerState().consoleColorEnabledAtomic.load(std::memory_order_acquire) ? GameWIP::Terminal::Types::StyleMode::Auto
                                                                                                    : GameWIP::Terminal::Types::StyleMode::Never;
        options.appendLineEnding = true;
        options.lineEnding = GameWIP::Terminal::Types::LineEnding::Native;
        options.flushMode = loggerState().flushConsoleEveryWriteAtomic.load(std::memory_order_acquire) ? GameWIP::IO::Types::FlushMode::Data
                                                                                                       : GameWIP::IO::Types::FlushMode::None;

        const GameWIP::Terminal::Types::OutputStream stream =
            style.useStderr ? GameWIP::Terminal::Types::OutputStream::Stderr : GameWIP::Terminal::Types::OutputStream::Stdout;
        return GameWIP::Terminal::writeSegments(stream, segments, options);
    }

    /// @brief Writes one report directly to configured sinks without using the async queue.
    /// @param level Severity for the report line.
    /// @param source Source text to write.
    /// @param message Message text to write, truncated to the active message limit if needed.
    /// @param unknownSource True when source came from an unregistered SourceId.
    /// @param alreadyTruncated True when the caller already bounded the message and appended the truncation suffix.
    /// @return True when at least one configured normal sink accepted the line.
    bool writeReportSynchronously(
        LogLevel level,
        std::string_view source,
        std::string_view message,
        bool unknownSource,
        bool alreadyTruncated,
        const FlushDeadline *deadline)
    {
        try
        {
            const std::uint32_t runtimeState = loggerState().runtimeStateBits.load(std::memory_order_acquire);
            const OutputMode mode = runtimeStateOutput(runtimeState);
            if (mode == OutputMode::None || !isValidLevel(level))
            {
                return false;
            }

            const bool consoleOutput = hasConsoleOutput(mode);
            const bool wantsFileOutput = hasFileOutput(mode);
            if (!consoleOutput && !wantsFileOutput)
            {
                return false;
            }

            std::string boundedMessageScratch;
            bool truncatedNow = false;
            const std::string_view messageText = boundedMessageView(message, alreadyTruncated, boundedMessageScratch, truncatedNow);
            const bool truncated = alreadyTruncated || truncatedNow;

            TimestampCache timestampCache;
            std::string line;
            const LogStyle style = getLogStyle(level);
            buildLogLine(line, getTimestampText(timestampCache), style.text, source, messageText);

            bool accepted = false;
            bool fileWriteFailed = false;
            PlatformError fileErrorDetail;
            {
                std::unique_lock<std::mutex> outputLock(loggerState().outputMutex, std::defer_lock);
                if (deadline == nullptr)
                {
                    outputLock.lock();
                }
                else if (!lockBefore(outputLock, *deadline))
                {
                    return false;
                }
                if (consoleOutput)
                {
                    accepted = accepted || writeConsoleLine(style, line).ok();
                }

                if (wantsFileOutput)
                {
                    if (loggerState().fileOutputAvailableAtomic.load(std::memory_order_acquire) && loggerState().logFile.isOpen())
                    {
                        std::string fileLine(line);
                        fileLine.push_back('\n');
                        fileErrorDetail = writeFileForLogger(loggerState().logFile, fileLine);
                        fileWriteFailed = hasPlatformError(fileErrorDetail);
                        accepted = accepted || !fileWriteFailed;
                    }
                    else
                    {
                        fileErrorDetail = PlatformError{PlatformErrorSource::File, 0};
                        fileWriteFailed = true;
                    }
                }
            }

            if (fileWriteFailed)
            {
                recordFileWriteFailure(fileErrorDetail);
            }

            if (accepted)
            {
                loggerState().stats.written.fetch_add(1, std::memory_order_relaxed);
                if (truncated)
                {
                    loggerState().stats.truncated.fetch_add(1, std::memory_order_relaxed);
                }
                if (unknownSource)
                {
                    recordUnknownSourceUse();
                }
            }

            return accepted;
        }
        catch (...)
        {
            return false;
        }
    }

#if INTERNAL_LOGGER_TEST_HOOKS
    /// @brief Returns a synthetic platform file error used by test hooks.
    PlatformError forcedFileError() noexcept
    {
        return PlatformError{PlatformErrorSource::File, 1};
    }

    /// @brief Returns a synthetic fatal-popup error used by test hooks.
    PlatformError forcedFatalPopupError() noexcept
    {
        return PlatformError{PlatformErrorSource::FatalPopup, 1};
    }
#endif

    /// @brief Opens a file, optionally consuming a test hook that forces failure.
    PlatformError openFileExclusiveForLogger(const FilePath &path, FileWriter &outWriter)
    {
#if INTERNAL_LOGGER_TEST_HOOKS
        if (consumeTestHook(loggerTestHookState.nextFileOpenFailure))
        {
            return forcedFileError();
        }
#endif
        const GameWIP::FileSystem::Types::FileWriterOpenOptions options{
            .mode = GameWIP::FileSystem::Types::FileWriterMode::CreateNew,
            .share = GameWIP::FileSystem::Types::FileShare::Read,
            .symlinkPolicy = GameWIP::FileSystem::Types::SymlinkPolicy::FollowAll,
            .createParentDirectories = false,
            .flushOnClose = GameWIP::IO::Types::FlushMode::None};
        return filePlatformError(outWriter.open(path, options));
    }

    /// @brief Writes file text, optionally consuming a test hook that forces failure.
    PlatformError writeFileForLogger(FileWriter &writer, std::string_view text)
    {
#if INTERNAL_LOGGER_TEST_HOOKS
        if (consumeTestHook(loggerTestHookState.nextFileWriteFailure))
        {
            return forcedFileError();
        }
#endif
        const GameWIP::IO::Types::WriteResult result = GameWIP::IO::writeAllText(writer, text);
        return filePlatformError(result.status);
    }

    /// @brief Flushes a file, optionally consuming a test hook that forces failure.
    PlatformError flushFileForLogger(FileWriter &writer)
    {
#if INTERNAL_LOGGER_TEST_HOOKS
        if (consumeTestHook(loggerTestHookState.nextFileFlushFailure))
        {
            return forcedFileError();
        }
#endif
        return filePlatformError(writer.flush(GameWIP::IO::Types::FlushMode::Data));
    }

    /// @brief Resolves a SourceId and writes one report directly to configured sinks.
    /// @param level Severity for the report line.
    /// @param source SourceId to resolve.
    /// @param message Message text to write.
    /// @param alreadyTruncated True when the caller already bounded the message and appended the truncation suffix.
    /// @return True when at least one configured normal sink accepted the line.
    bool writeReportSynchronously(LogLevel level, SourceId source, std::string_view message, bool alreadyTruncated, const FlushDeadline *deadline)
    {
        const std::shared_ptr<SourceRegistry> registry = loadSourceRegistry();
        bool unknownSource = false;
        const std::string_view sourceText = findSourceName(registry.get(), source, unknownSource);
        return writeReportSynchronously(level, sourceText, message, unknownSource, alreadyTruncated, deadline);
    }

    /// @brief Writes one entry to console immediately and appends file text to the batch buffer.
    /// @details Filters are deliberately rechecked on the worker so a concurrent filter update can suppress work accepted earlier.
    /// @param entry Entry to write.
    /// @param timestampCache Worker timestamp cache.
    /// @param lineScratch Reusable line scratch buffer.
    /// @param fileBatchScratch Reusable file batch buffer.
    /// @return Sink acceptance details for stats accounting.
    /// @note Runtime filters are rechecked here so queued entries can still be suppressed after a filter change.
    SinkWriteResult writeLogEntry(
        const QueuedLogEntry &entry,
        TimestampCache &timestampCache,
        std::string &lineScratch,
        std::string &fileBatchScratch)
    {
        SinkWriteResult result;
        const std::uint32_t runtimeState = loggerState().runtimeStateBits.load(std::memory_order_acquire);
        const OutputMode mode = runtimeStateOutput(runtimeState);
        if (mode == OutputMode::None || !isValidLevel(entry.level))
        {
            return result;
        }

        if (!entry.bypassFilters && toLevelValue(entry.level) < toLevelValue(runtimeStateMinLevel(runtimeState)))
        {
            return result;
        }

        const std::shared_ptr<SourceRegistry> registry = entry.usesRegisteredSource ? loadSourceRegistry() : std::shared_ptr<SourceRegistry>{};
        const std::uint8_t levelMaskBit = levelBit(entry.level);
        if (!entry.bypassFilters && (levelMaskBit == 0 || (runtimeStateLevelMask(runtimeState) & levelMaskBit) == 0 ||
                                     (entry.usesRegisteredSource && !sourceEnabledRuntime(registry.get(), entry.sourceId))))
        {
            return result;
        }

        const bool consoleOutput = hasConsoleOutput(mode);
        const bool wantsFileOutput = hasFileOutput(mode);
        if (!consoleOutput && !wantsFileOutput)
        {
            return result;
        }

        const LogStyle style = getLogStyle(entry.level);
        bool unknownSource = false;
        const std::string_view source = resolveSourceText(entry, registry.get(), unknownSource);
        buildLogLine(lineScratch, getTimestampText(timestampCache), style.text, source, entry.message.view());

        if (consoleOutput)
        {
            std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
            result.acceptedImmediateSink = writeConsoleLine(style, lineScratch).ok();
        }

        if (wantsFileOutput)
        {
            if (loggerState().fileOutputAvailableAtomic.load(std::memory_order_acquire))
            {
                fileBatchScratch.append(lineScratch);
                fileBatchScratch.push_back('\n');
                result.queuedFile = true;
            }
            else
            {
                recordFileWriteFailure(PlatformError{PlatformErrorSource::File, 0});
            }
        }

        if (unknownSource)
        {
            recordUnknownSourceUse();
        }

        return result;
    }

    /// @brief Writes the worker file batch to disk and optionally flushes it.
    /// @param fileBatchScratch Batched file text to write; cleared before return.
    /// @param forceFlush True when shutdown/flush path requires an immediate file flush.
    /// @return True when the batch was empty or written successfully.
    bool flushFileBatch(std::string &fileBatchScratch, bool forceFlush)
    {
        if (fileBatchScratch.empty())
        {
            return true;
        }

        std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
        bool success = false;
        PlatformError fileError = PlatformError{PlatformErrorSource::File, 0};
        if (loggerState().logFile.isOpen())
        {
            fileError = writeFileForLogger(loggerState().logFile, fileBatchScratch);
            if (forceFlush || loggerState().flushFileEveryBatchAtomic.load(std::memory_order_acquire))
            {
                if (!hasPlatformError(fileError))
                {
                    fileError = flushFileForLogger(loggerState().logFile);
                }
            }
            success = !hasPlatformError(fileError);
        }

        fileBatchScratch.clear();
        if (!success)
        {
            recordFileWriteFailure(fileError);
        }

        return success;
    }

    /// @brief Shows the fatal popup when enabled.
    /// @param message Fatal popup message text.
    /// @note This remains internal; public fatal popup behavior goes through reportFatal().
    void showFatalPopupIfEnabled(std::string_view message)
    {
        if (!loggerState().fatalPopupEnabledAtomic.load(std::memory_order_acquire))
        {
            return;
        }

        try
        {
#if INTERNAL_LOGGER_TEST_HOOKS
            if (consumeTestHook(loggerTestHookState.nextFatalPopupFailure))
            {
                recordPlatformErrorIfAny(forcedFatalPopupError());
                return;
            }
#endif
            recordPlatformErrorIfAny(GameWIP::Logger::Detail::Platform::showFatalPopup(message));
        }
        catch (...)
        {
            countAllocationFailure();
        }
    }

    /// @brief Flushes active sinks without waiting for async queued work.
    /// @details Caller must serialize lifecycle if sink lifetime may change concurrently.
    /// @return True when sink flushing did not report a platform file error.
    bool flushSinksInternal()
    {
        bool fileFlushFailed = false;
        PlatformError fileError;
        {
            std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
            static_cast<void>(GameWIP::Terminal::flush(GameWIP::Terminal::Types::OutputStream::Stdout, GameWIP::IO::Types::FlushMode::Data));
            static_cast<void>(GameWIP::Terminal::flush(GameWIP::Terminal::Types::OutputStream::Stderr, GameWIP::IO::Types::FlushMode::Data));

            if (loggerState().logFile.isOpen())
            {
                fileError = flushFileForLogger(loggerState().logFile);
                fileFlushFailed = hasPlatformError(fileError);
            }
        }

        if (fileFlushFailed)
        {
            recordFileWriteFailure(fileError);
            return false;
        }
        return true;
    }

    bool flushSinksInternal(FlushDeadline deadline)
    {
        bool fileFlushFailed = false;
        PlatformError fileError;
        {
            std::unique_lock<std::mutex> outputLock(loggerState().outputMutex, std::defer_lock);
            if (!lockBefore(outputLock, deadline))
            {
                return false;
            }
            if (std::chrono::steady_clock::now() >= deadline)
            {
                return false;
            }

            // Native console and filesystem flushes are synchronous and not reliably cancellable
            // after entry. The deadline bounds all Logger-owned waits before those calls.
            static_cast<void>(GameWIP::Terminal::flush(GameWIP::Terminal::Types::OutputStream::Stdout, GameWIP::IO::Types::FlushMode::Data));
            static_cast<void>(GameWIP::Terminal::flush(GameWIP::Terminal::Types::OutputStream::Stderr, GameWIP::IO::Types::FlushMode::Data));

            if (loggerState().logFile.isOpen())
            {
                fileError = flushFileForLogger(loggerState().logFile);
                fileFlushFailed = hasPlatformError(fileError);
            }
        }

        if (fileFlushFailed)
        {
            recordFileWriteFailure(fileError);
            return false;
        }
        return std::chrono::steady_clock::now() <= deadline;
    }

    /// @brief Waits for accepted queued work to drain, then flushes active sinks.
    /// @details Caller must serialize lifecycle if sink lifetime may change concurrently.
    void flushInternal()
    {
        {
            std::unique_lock<std::mutex> lock(loggerState().logMutex);
            loggerState().logCondition.wait(
                lock,
                []
                {
                    return (!loggerState().workerRunning && !loggerState().workerBusy) ||
                           (loggerState().queueDepth.load(std::memory_order_acquire) == 0 && !loggerState().workerBusy);
                });
        }

        (void)flushSinksInternal();
    }

    /// @brief Waits for accepted queued work to drain until timeout, then flushes active sinks.
    /// @details Caller must serialize lifecycle if sink lifetime may change concurrently.
    /// @return True when the queue drained and sink flushing succeeded before timeout expired.
    bool flushInternal(FlushDeadline deadline)
    {
#if INTERNAL_LOGGER_TEST_HOOKS
        if (consumeTestHook(loggerTestHookState.nextTimedFlushTimeout))
        {
            return false;
        }
#endif
        const bool drained = [&]
        {
            std::unique_lock<std::mutex> lock(loggerState().logMutex, std::defer_lock);
            if (!lockBefore(lock, deadline))
            {
                return false;
            }
            return loggerState().logCondition.wait_until(
                lock,
                deadline,
                []
                {
                    return (!loggerState().workerRunning && !loggerState().workerBusy) ||
                           (loggerState().queueDepth.load(std::memory_order_acquire) == 0 && !loggerState().workerBusy);
                });
        }();

        if (!drained)
        {
            return false;
        }

        return flushSinksInternal(deadline);
    }
} // namespace GameWIP::Logger::Detail::Core

using namespace GameWIP::Logger::Detail::Core;

//-------------------------------------------------------------------------------------------------
// Private Logger bridge enqueue helpers used by header-only formatting overloads
//-------------------------------------------------------------------------------------------------

/// @brief Enqueues a preformatted message after the caller's fast-path filter check.
/// @param level Entry severity.
/// @param source Source text to copy.
/// @param message Formatted message copied before this call returns.
void GameWIP::Logger::Detail::Core::enqueuePreformattedMessage(LogLevel level, std::string_view source, std::string_view message)
{
    enqueuePreformattedMessage(level, source, message, false);
}

/// @brief Enqueues a preformatted message after bounded formatting has already truncated it.
/// @param level Entry severity.
/// @param source Source text to copy.
/// @param message Formatted message copied before this call returns.
/// @param alreadyTruncated True when message already includes the truncation suffix.
void GameWIP::Logger::Detail::Core::enqueuePreformattedMessage(
    LogLevel level,
    std::string_view source,
    std::string_view message,
    bool alreadyTruncated)
{
    try
    {
        enqueueAndWakeWorker(makePendingEntry(level, source, message, false, alreadyTruncated));
    }
    catch (...)
    {
        countAllocationFailure();
    }
}

/// @brief Enqueues a preformatted message after the caller's fast-path filter check.
/// @param level Entry severity.
/// @param source Registered SourceId to store.
/// @param message Formatted message copied before this call returns.
void GameWIP::Logger::Detail::Core::enqueuePreformattedMessage(LogLevel level, SourceId source, std::string_view message)
{
    enqueuePreformattedMessage(level, source, message, false);
}

/// @brief Enqueues a preformatted message after bounded formatting has already truncated it.
/// @param level Entry severity.
/// @param source Registered SourceId to store.
/// @param message Formatted message copied before this call returns.
/// @param alreadyTruncated True when message already includes the truncation suffix.
void GameWIP::Logger::Detail::Core::enqueuePreformattedMessage(LogLevel level, SourceId source, std::string_view message, bool alreadyTruncated)
{
    try
    {
        enqueueAndWakeWorker(makePendingEntry(level, source, message, false, alreadyTruncated));
    }
    catch (...)
    {
        countAllocationFailure();
    }
}

//-------------------------------------------------------------------------------------------------
// Public logging API
//-------------------------------------------------------------------------------------------------

/// @brief Logs a preformatted message with a string source.
/// @param level Entry severity.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::log(LogLevel level, std::string_view source, std::string_view message)
{
    if (!shouldLog(level))
    {
        return;
    }

    try
    {
        enqueueAndWakeWorker(makePendingEntry(level, source, message));
    }
    catch (...)
    {
        countAllocationFailure();
    }
}

/// @brief Logs a preformatted message with a registered SourceId.
/// @param level Entry severity.
/// @param source Registered SourceId to store.
/// @param message Message text to copy.
void GameWIP::Logger::log(LogLevel level, Types::SourceId source, std::string_view message)
{
    if (!shouldLog(level, source))
    {
        return;
    }

    try
    {
        enqueueAndWakeWorker(makePendingEntry(level, source, message));
    }
    catch (...)
    {
        countAllocationFailure();
    }
}

/// @brief Logs a Trace message with a string source.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::trace(std::string_view source, std::string_view message)
{
    log(LogLevel::Trace, source, message);
}

/// @brief Logs a Trace message with a registered SourceId.
/// @param source Registered SourceId to store.
/// @param message Message text to copy.
void GameWIP::Logger::trace(Types::SourceId source, std::string_view message)
{
    log(LogLevel::Trace, source, message);
}

/// @brief Logs a Debug message with a string source.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::debug(std::string_view source, std::string_view message)
{
    log(LogLevel::Debug, source, message);
}

/// @brief Logs a Debug message with a registered SourceId.
/// @param source Registered SourceId to store.
/// @param message Message text to copy.
void GameWIP::Logger::debug(Types::SourceId source, std::string_view message)
{
    log(LogLevel::Debug, source, message);
}

/// @brief Logs an Info message with a string source.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::info(std::string_view source, std::string_view message)
{
    log(LogLevel::Info, source, message);
}

/// @brief Logs an Info message with a registered SourceId.
/// @param source Registered SourceId to store.
/// @param message Message text to copy.
void GameWIP::Logger::info(Types::SourceId source, std::string_view message)
{
    log(LogLevel::Info, source, message);
}

/// @brief Logs a Warn message with a string source.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::warn(std::string_view source, std::string_view message)
{
    log(LogLevel::Warn, source, message);
}

/// @brief Logs a Warn message with a registered SourceId.
/// @param source Registered SourceId to store.
/// @param message Message text to copy.
void GameWIP::Logger::warn(Types::SourceId source, std::string_view message)
{
    log(LogLevel::Warn, source, message);
}

/// @brief Logs an Error message with a string source.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::error(std::string_view source, std::string_view message)
{
    log(LogLevel::Error, source, message);
}

/// @brief Logs an Error message with a registered SourceId.
/// @param source Registered SourceId to store.
/// @param message Message text to copy.
void GameWIP::Logger::error(Types::SourceId source, std::string_view message)
{
    log(LogLevel::Error, source, message);
}

/// @brief Logs a Fatal message with a string source without forcing platform debug output flush or fatal popup.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::fatal(std::string_view source, std::string_view message)
{
    log(LogLevel::Fatal, source, message);
}

/// @brief Logs a Fatal message with a registered SourceId without forcing platform debug output flush or fatal popup.
/// @param source Registered SourceId to store.
/// @param message Message text to copy.
void GameWIP::Logger::fatal(Types::SourceId source, std::string_view message)
{
    log(LogLevel::Fatal, source, message);
}

//-------------------------------------------------------------------------------------------------
// Reporting and direct platform debug output API
//-------------------------------------------------------------------------------------------------

/// @brief Reports a preformatted message with a string source.
/// @param level Severity to log and mirror.
/// @param source Source text to copy.
/// @param message Formatted message copied before this call returns.
/// @param showPopup True to run the fatal popup path after flush.
void GameWIP::Logger::Detail::Core::reportPreformattedMessage(LogLevel level, std::string_view source, std::string_view message, bool showPopup)
{
    reportPreformattedMessage(level, source, message, showPopup, false, nullptr);
}

/// @brief Reports a preformatted message with a string source, then optionally attempts a bounded queue drain.
/// @param level Severity to log and mirror.
/// @param source Source text to copy.
/// @param message Formatted message copied before this call returns.
/// @param showPopup True to run the fatal popup path after flush.
/// @param alreadyTruncated True when message already includes the truncation suffix.
/// @param timeout Optional bounded flush duration.
/// @return True when the requested post-report drain and observable sink flush completed; not report-line delivery status.
bool GameWIP::Logger::Detail::Core::reportPreformattedMessage(
    LogLevel level,
    std::string_view source,
    std::string_view message,
    bool showPopup,
    bool alreadyTruncated,
    Types::FlushTimeout *timeout)
{
    if (!isValidLevel(level))
    {
        return false;
    }

    std::string boundedMessageScratch;
    bool truncatedNow = false;
    const std::string_view reportMessage = boundedMessageView(message, alreadyTruncated, boundedMessageScratch, truncatedNow);
    const bool storedMessageAlreadyTruncated = alreadyTruncated || truncatedNow;

    const FlushDeadline deadline = timeout == nullptr ? FlushDeadline{} : makeFlushDeadline(timeout->value);
    std::unique_lock<std::mutex> lifecycleLock(loggerState().lifecycleMutex, std::defer_lock);
    if (timeout == nullptr)
    {
        lifecycleLock.lock();
    }
    else if (!lockBefore(lifecycleLock, deadline))
    {
        return false;
    }

    (void)writeReportSynchronously(level, source, reportMessage, false, storedMessageAlreadyTruncated, timeout == nullptr ? nullptr : &deadline);

    writeDebugOutput(level, source, reportMessage);
    bool flushed = true;
    if (timeout == nullptr)
    {
        flushed = flushSinksInternal();
    }
    else
    {
        const bool initialFlush = flushSinksInternal(deadline);
        const bool drained = flushInternal(deadline);
        flushed = initialFlush && drained;
    }

    if (showPopup)
    {
        showFatalPopupIfEnabled(reportMessage);
    }
    return flushed;
}

/// @brief Reports a preformatted message with a registered SourceId.
/// @param level Severity to log and mirror.
/// @param source Registered SourceId to resolve for platform debug output.
/// @param message Formatted message copied before this call returns.
/// @param showPopup True to run the fatal popup path after flush.
void GameWIP::Logger::Detail::Core::reportPreformattedMessage(LogLevel level, SourceId source, std::string_view message, bool showPopup)
{
    reportPreformattedMessage(level, source, message, showPopup, false, nullptr);
}

/// @brief Reports a preformatted message with a registered SourceId, then optionally attempts a bounded queue drain.
/// @param level Severity to log and mirror.
/// @param source Registered SourceId to resolve for platform debug output.
/// @param message Formatted message copied before this call returns.
/// @param showPopup True to run the fatal popup path after flush.
/// @param alreadyTruncated True when message already includes the truncation suffix.
/// @param timeout Optional bounded flush duration.
/// @return True when the requested post-report drain and observable sink flush completed; not report-line delivery status.
bool GameWIP::Logger::Detail::Core::reportPreformattedMessage(
    LogLevel level,
    SourceId source,
    std::string_view message,
    bool showPopup,
    bool alreadyTruncated,
    Types::FlushTimeout *timeout)
{
    if (!isValidLevel(level))
    {
        return false;
    }

    std::string boundedMessageScratch;
    bool truncatedNow = false;
    const std::string_view reportMessage = boundedMessageView(message, alreadyTruncated, boundedMessageScratch, truncatedNow);
    const bool storedMessageAlreadyTruncated = alreadyTruncated || truncatedNow;

    const FlushDeadline deadline = timeout == nullptr ? FlushDeadline{} : makeFlushDeadline(timeout->value);
    std::unique_lock<std::mutex> lifecycleLock(loggerState().lifecycleMutex, std::defer_lock);
    if (timeout == nullptr)
    {
        lifecycleLock.lock();
    }
    else if (!lockBefore(lifecycleLock, deadline))
    {
        return false;
    }

    (void)writeReportSynchronously(level, source, reportMessage, storedMessageAlreadyTruncated, timeout == nullptr ? nullptr : &deadline);

    bool unknownSource = false;
    const std::shared_ptr<SourceRegistry> registry = loadSourceRegistry();
    const std::string_view sourceText = findSourceName(registry.get(), source, unknownSource);
    writeDebugOutput(level, sourceText, reportMessage);
    bool flushed = true;
    if (timeout == nullptr)
    {
        flushed = flushSinksInternal();
    }
    else
    {
        const bool initialFlush = flushSinksInternal(deadline);
        const bool drained = flushInternal(deadline);
        flushed = initialFlush && drained;
    }

    if (showPopup)
    {
        showFatalPopupIfEnabled(reportMessage);
    }
    return flushed;
}

/// @brief Synchronously reports a preformatted diagnostic with a string source and no logger-owned popup.
void GameWIP::Logger::report(LogLevel level, std::string_view source, std::string_view message)
{
    reportPreformattedMessage(level, source, message, false);
}

/// @brief Synchronously reports a preformatted diagnostic with a string source and bounded drain/flush.
bool GameWIP::Logger::report(LogLevel level, std::string_view source, Types::FlushTimeout timeout, std::string_view message)
{
    return reportPreformattedMessage(level, source, message, false, false, &timeout);
}

/// @brief Synchronously reports a preformatted diagnostic with a SourceId and no logger-owned popup.
void GameWIP::Logger::report(LogLevel level, SourceId source, std::string_view message)
{
    reportPreformattedMessage(level, source, message, false);
}

/// @brief Synchronously reports a preformatted diagnostic with a SourceId and bounded drain/flush.
bool GameWIP::Logger::report(LogLevel level, SourceId source, Types::FlushTimeout timeout, std::string_view message)
{
    return reportPreformattedMessage(level, source, message, false, false, &timeout);
}

/// @brief Synchronously reports a preformatted diagnostic with explicit popup behavior.
void GameWIP::Logger::report(LogLevel level, std::string_view source, Types::ReportPopup popup, std::string_view message)
{
    reportPreformattedMessage(level, source, message, popup == Types::ReportPopup::Fatal);
}

/// @brief Synchronously reports a preformatted diagnostic with explicit popup behavior and bounded drain/flush.
bool GameWIP::Logger::report(LogLevel level, std::string_view source, Types::FlushTimeout timeout, Types::ReportPopup popup, std::string_view message)
{
    return reportPreformattedMessage(level, source, message, popup == Types::ReportPopup::Fatal, false, &timeout);
}

/// @brief Synchronously reports a preformatted diagnostic with a SourceId and explicit popup behavior.
void GameWIP::Logger::report(LogLevel level, SourceId source, Types::ReportPopup popup, std::string_view message)
{
    reportPreformattedMessage(level, source, message, popup == Types::ReportPopup::Fatal);
}

/// @brief Synchronously reports a preformatted diagnostic with a SourceId, popup behavior, and bounded drain/flush.
bool GameWIP::Logger::report(LogLevel level, SourceId source, Types::FlushTimeout timeout, Types::ReportPopup popup, std::string_view message)
{
    return reportPreformattedMessage(level, source, message, popup == Types::ReportPopup::Fatal, false, &timeout);
}

/// @brief Reports an error, mirrors it to platform debug output, and flushes active sinks without draining older queued records.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::reportError(std::string_view source, std::string_view message)
{
    report(LogLevel::Error, source, message);
}

/// @brief Reports an error, mirrors it to platform debug output, then attempts a bounded queue drain and sink flush.
/// @param source Source text to copy.
/// @param timeout Maximum flush wait.
/// @param message Message text to copy.
/// @return True when the post-report bounded queue drain and observable sink flush completed; not report-line delivery status.
bool GameWIP::Logger::reportError(std::string_view source, Types::FlushTimeout timeout, std::string_view message)
{
    return report(LogLevel::Error, source, timeout, message);
}

/// @brief Reports an error with a SourceId, mirrors it to platform debug output, and flushes active sinks without draining older queued records.
/// @param source Registered SourceId to store and resolve.
/// @param message Message text to copy.
void GameWIP::Logger::reportError(SourceId source, std::string_view message)
{
    report(LogLevel::Error, source, message);
}

/// @brief Reports an error with a SourceId, mirrors it to platform debug output, then attempts a bounded queue drain and sink flush.
/// @param source Registered SourceId to store and resolve.
/// @param timeout Maximum flush wait.
/// @param message Message text to copy.
/// @return True when the post-report bounded queue drain and observable sink flush completed; not report-line delivery status.
bool GameWIP::Logger::reportError(SourceId source, Types::FlushTimeout timeout, std::string_view message)
{
    return report(LogLevel::Error, source, timeout, message);
}

/// @brief Reports fatal, mirrors it to platform debug output, flushes active sinks, and shows the fatal popup when enabled.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::reportFatal(std::string_view source, std::string_view message)
{
    report(LogLevel::Fatal, source, Types::ReportPopup::Fatal, message);
}

/// @brief Reports fatal, mirrors it to platform debug output, attempts a bounded queue drain/sink flush, and shows the popup when enabled.
/// @param source Source text to copy.
/// @param timeout Maximum flush wait.
/// @param message Message text to copy.
/// @return True when the post-report bounded queue drain and observable sink flush completed; not report-line delivery status.
bool GameWIP::Logger::reportFatal(std::string_view source, Types::FlushTimeout timeout, std::string_view message)
{
    return report(LogLevel::Fatal, source, timeout, Types::ReportPopup::Fatal, message);
}

/// @brief Reports fatal with a SourceId, mirrors it to platform debug output, flushes active sinks, and shows the popup when enabled.
/// @param source Registered SourceId to store and resolve.
/// @param message Message text to copy.
void GameWIP::Logger::reportFatal(SourceId source, std::string_view message)
{
    report(LogLevel::Fatal, source, Types::ReportPopup::Fatal, message);
}

/// @brief Reports fatal with a SourceId, mirrors it to platform debug output, attempts a bounded queue
/// drain/sink flush, and shows the popup when enabled.
/// @param source Registered SourceId to store and resolve.
/// @param timeout Maximum flush wait.
/// @param message Message text to copy.
/// @return True when the post-report bounded queue drain and observable sink flush completed; not report-line delivery status.
bool GameWIP::Logger::reportFatal(SourceId source, Types::FlushTimeout timeout, std::string_view message)
{
    return report(LogLevel::Fatal, source, timeout, Types::ReportPopup::Fatal, message);
}

/// @brief Performs the untimed fatal report path, then calls std::terminate() without normal stack unwinding.
[[noreturn]] void GameWIP::Logger::fatalTerminate(std::string_view source, std::string_view message)
{
    report(LogLevel::Fatal, source, Types::ReportPopup::Fatal, message);
    std::terminate();
}

/// @brief Performs the untimed fatal report path for a SourceId, then calls std::terminate() without normal stack unwinding.
[[noreturn]] void GameWIP::Logger::fatalTerminate(Types::SourceId source, std::string_view message)
{
    report(LogLevel::Fatal, source, Types::ReportPopup::Fatal, message);
    std::terminate();
}

/// @brief Performs the timed fatal report path, then calls std::terminate() without normal stack unwinding.
[[noreturn]] void GameWIP::Logger::fatalTerminate(std::string_view source, Types::FlushTimeout timeout, std::string_view message)
{
    report(LogLevel::Fatal, source, timeout, Types::ReportPopup::Fatal, message);
    std::terminate();
}

/// @brief Performs the timed fatal report path for a SourceId, then calls std::terminate() without normal stack unwinding.
[[noreturn]] void GameWIP::Logger::fatalTerminate(Types::SourceId source, Types::FlushTimeout timeout, std::string_view message)
{
    report(LogLevel::Fatal, source, timeout, Types::ReportPopup::Fatal, message);
    std::terminate();
}

/// @brief Writes a line directly to the platform debug output when enabled.
/// @param level Severity used for the platform debug output line label.
/// @param source Source text written into the line.
/// @param message Message text written into the line.
void GameWIP::Logger::writeDebugOutput(LogLevel level, std::string_view source, std::string_view message)
{
    if (!loggerState().debugOutputEnabledAtomic.load(std::memory_order_acquire))
    {
        return;
    }

    try
    {
        std::string boundedMessageScratch;
        bool truncated = false;
        const std::string_view messageText = boundedMessageView(message, false, boundedMessageScratch, truncated);
        const LogStyle style = getLogStyle(level);
        std::string line;
        buildLogLine(line, getDebugTimestampText(), style.text, source, messageText);
        line.push_back('\n');
        recordPlatformErrorIfAny(GameWIP::Logger::Detail::Platform::writeDebugOutput(line));
    }
    catch (...)
    {
        countAllocationFailure();
    }
}
