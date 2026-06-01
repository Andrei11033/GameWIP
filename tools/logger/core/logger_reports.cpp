/// @file logger_reports.cpp
/// @brief Logger synchronous reporting, sink writes, fatal popup, and debug-output mirroring.

#include "logger/internal/logger_core.h"

namespace GameWIP::LoggerDetail::Core
{
    PlatformError openFileExclusiveForLogger(std::string_view path, FileHandle &outHandle);
    PlatformError writeFileForLogger(FileHandle handle, std::string_view text);
    PlatformError flushFileForLogger(FileHandle handle);

    /// @brief Writes one report directly to configured sinks without using the async queue.
    /// @param level Severity for the report line.
    /// @param source Source text to write.
    /// @param message Message text to write, truncated to the active message limit if needed.
    /// @param unknownSource True when source came from an unregistered SourceId.
    /// @param alreadyTruncated True when the caller already bounded the message and appended the truncation suffix.
    /// @return True when at least one configured normal sink accepted the line.
    bool writeReportSynchronously(LogLevel level, std::string_view source, std::string_view message, bool unknownSource, bool alreadyTruncated)
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

            std::size_t maxMessageLength = 0;
            {
                std::lock_guard<std::mutex> lock(loggerState().logMutex);
                maxMessageLength = loggerState().maxMessageLength;
            }

            constexpr std::string_view suffix = "... [truncated]";
            std::string truncatedMessage;
            std::string_view messageText = message;
            const bool needsTruncation = message.size() > maxMessageLength;
            const bool truncated = alreadyTruncated || needsTruncation;
            if (needsTruncation)
            {
                if (maxMessageLength <= suffix.size())
                {
                    truncatedMessage.assign(suffix.substr(0, maxMessageLength));
                }
                else
                {
                    truncatedMessage.reserve(maxMessageLength);
                    truncatedMessage.append(message.substr(0, maxMessageLength - suffix.size()));
                    truncatedMessage.append(suffix);
                }
                messageText = truncatedMessage;
            }

            TimestampCache timestampCache;
            std::string line;
            const LogStyle style = getLogStyle(level);
            buildLogLine(line, getTimestampText(timestampCache), style.text, source, messageText);

            bool accepted = false;
            bool fileWriteFailed = false;
            PlatformError fileErrorDetail;
            {
                std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
                if (consoleOutput)
                {
                    std::ostream &consoleStream = style.useCerr ? std::cerr : std::cout;
                    if (consoleColorEnabledForStream(style.useCerr) && style.color[0] != '\0')
                    {
                        consoleStream << style.color << line << "\033[0m";
                    }
                    else
                    {
                        consoleStream << line;
                    }

                    consoleStream << '\n';
                    if (loggerState().flushConsoleEveryWriteAtomic.load(std::memory_order_acquire))
                    {
                        consoleStream.flush();
                    }
                    accepted = accepted || !consoleStream.fail();
                }

                if (wantsFileOutput)
                {
                    if (loggerState().fileOutputAvailableAtomic.load(std::memory_order_acquire) && GameWIP::LoggerDetail::Platform::isFileOpen(loggerState().logFile))
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

#if GAMEWIP_LOGGER_TEST_HOOKS
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
    PlatformError openFileExclusiveForLogger(std::string_view path, FileHandle &outHandle)
    {
#if GAMEWIP_LOGGER_TEST_HOOKS
        if (consumeTestHook(loggerTestHookState.nextFileOpenFailure))
        {
            outHandle = {};
            return forcedFileError();
        }
#endif
        return GameWIP::LoggerDetail::Platform::openFileExclusive(path, outHandle);
    }

    /// @brief Writes file text, optionally consuming a test hook that forces failure.
    PlatformError writeFileForLogger(FileHandle handle, std::string_view text)
    {
#if GAMEWIP_LOGGER_TEST_HOOKS
        if (consumeTestHook(loggerTestHookState.nextFileWriteFailure))
        {
            return forcedFileError();
        }
#endif
        return GameWIP::LoggerDetail::Platform::writeFile(handle, text);
    }

    /// @brief Flushes a file, optionally consuming a test hook that forces failure.
    PlatformError flushFileForLogger(FileHandle handle)
    {
#if GAMEWIP_LOGGER_TEST_HOOKS
        if (consumeTestHook(loggerTestHookState.nextFileFlushFailure))
        {
            return forcedFileError();
        }
#endif
        return GameWIP::LoggerDetail::Platform::flushFile(handle);
    }

    /// @brief Resolves a SourceId and writes one report directly to configured sinks.
    /// @param level Severity for the report line.
    /// @param source SourceId to resolve.
    /// @param message Message text to write.
    /// @param alreadyTruncated True when the caller already bounded the message and appended the truncation suffix.
    /// @return True when at least one configured normal sink accepted the line.
    bool writeReportSynchronously(LogLevel level, SourceId source, std::string_view message, bool alreadyTruncated)
    {
        const std::shared_ptr<SourceRegistry> registry = loadSourceRegistry();
        bool unknownSource = false;
        const std::string_view sourceText = findSourceName(registry.get(), source, unknownSource);
        return writeReportSynchronously(level, sourceText, message, unknownSource, alreadyTruncated);
    }

    /// @brief Writes one entry to console immediately and appends file text to the batch buffer.
    /// @param entry Entry to write.
    /// @param timestampCache Worker timestamp cache.
    /// @param lineScratch Reusable line scratch buffer.
    /// @param fileBatchScratch Reusable file batch buffer.
    /// @return Sink acceptance details for stats accounting.
    /// @note Runtime filters are rechecked here so queued entries can still be suppressed after a filter change.
    SinkWriteResult writeLogEntry(const QueuedLogEntry &entry, TimestampCache &timestampCache, std::string &lineScratch, std::string &fileBatchScratch)
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
        if (!entry.bypassFilters &&
            (levelMaskBit == 0 ||
             (runtimeStateLevelMask(runtimeState) & levelMaskBit) == 0 ||
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
            std::ostream &consoleStream = style.useCerr ? std::cerr : std::cout;
            if (consoleColorEnabledForStream(style.useCerr) && style.color[0] != '\0')
            {
                consoleStream << style.color << lineScratch << "\033[0m";
            }
            else
            {
                consoleStream << lineScratch;
            }

            consoleStream << '\n';
            if (loggerState().flushConsoleEveryWriteAtomic.load(std::memory_order_acquire))
            {
                consoleStream.flush();
            }
            result.acceptedImmediateSink = !consoleStream.fail();
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
        if (GameWIP::LoggerDetail::Platform::isFileOpen(loggerState().logFile))
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
            #if GAMEWIP_LOGGER_TEST_HOOKS
            if (consumeTestHook(loggerTestHookState.nextFatalPopupFailure))
            {
                recordPlatformErrorIfAny(forcedFatalPopupError());
                return;
            }
#endif
            recordPlatformErrorIfAny(GameWIP::LoggerDetail::Platform::showFatalPopup(message));
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
            std::cout.flush();
            std::cerr.flush();

            if (GameWIP::LoggerDetail::Platform::isFileOpen(loggerState().logFile))
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

    /// @brief Waits for accepted queued work to drain, then flushes active sinks.
    /// @details Caller must serialize lifecycle if sink lifetime may change concurrently.
    void flushInternal()
    {
        {
            std::unique_lock<std::mutex> lock(loggerState().logMutex);
            loggerState().logCondition.wait(lock, []
                                          { return (!loggerState().workerRunning && !loggerState().workerBusy) || (loggerState().queueDepth.load(std::memory_order_acquire) == 0 && !loggerState().workerBusy); });
        }

        (void)flushSinksInternal();
    }

    /// @brief Waits for accepted queued work to drain until timeout, then flushes active sinks.
    /// @details Caller must serialize lifecycle if sink lifetime may change concurrently.
    /// @return True when the queue drained and sink flushing succeeded before timeout expired.
    bool flushInternal(std::chrono::milliseconds timeout)
    {
#if GAMEWIP_LOGGER_TEST_HOOKS
        if (consumeTestHook(loggerTestHookState.nextTimedFlushTimeout))
        {
            return false;
        }
#endif
        const bool drained = [&]
        {
            std::unique_lock<std::mutex> lock(loggerState().logMutex);
            return loggerState().logCondition.wait_for(lock, timeout, []
                                                     { return (!loggerState().workerRunning && !loggerState().workerBusy) || (loggerState().queueDepth.load(std::memory_order_acquire) == 0 && !loggerState().workerBusy); });
        }();

        if (!drained)
        {
            return false;
        }

        return flushSinksInternal();
    }
}

using namespace GameWIP::LoggerDetail::Core;

//-------------------------------------------------------------------------------------------------
// Private Logger bridge enqueue helpers used by header-only formatting overloads
//-------------------------------------------------------------------------------------------------

/// @brief Enqueues a preformatted message after the caller's fast-path filter check.
/// @param level Entry severity.
/// @param source Source text to copy.
/// @param message Formatted message copied before this call returns.
void GameWIP::Logger::enqueuePreformattedMessage(LogLevel level, std::string_view source, std::string_view message)
{
    enqueuePreformattedMessage(level, source, message, false);
}

/// @brief Enqueues a preformatted message after bounded formatting has already truncated it.
/// @param level Entry severity.
/// @param source Source text to copy.
/// @param message Formatted message copied before this call returns.
/// @param alreadyTruncated True when message already includes the truncation suffix.
void GameWIP::Logger::enqueuePreformattedMessage(LogLevel level, std::string_view source, std::string_view message, bool alreadyTruncated)
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
void GameWIP::Logger::enqueuePreformattedMessage(LogLevel level, SourceId source, std::string_view message)
{
    enqueuePreformattedMessage(level, source, message, false);
}

/// @brief Enqueues a preformatted message after bounded formatting has already truncated it.
/// @param level Entry severity.
/// @param source Registered SourceId to store.
/// @param message Formatted message copied before this call returns.
/// @param alreadyTruncated True when message already includes the truncation suffix.
void GameWIP::Logger::enqueuePreformattedMessage(LogLevel level, SourceId source, std::string_view message, bool alreadyTruncated)
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
void GameWIP::Logger::reportPreformattedMessage(LogLevel level, std::string_view source, std::string_view message, bool showPopup)
{
    reportPreformattedMessage(level, source, message, showPopup, false, nullptr);
}

/// @brief Reports a preformatted message with a string source and optional bounded flush.
/// @param level Severity to log and mirror.
/// @param source Source text to copy.
/// @param message Formatted message copied before this call returns.
/// @param showPopup True to run the fatal popup path after flush.
/// @param alreadyTruncated True when message already includes the truncation suffix.
/// @param timeout Optional bounded flush duration.
/// @return True when the flush completed.
bool GameWIP::Logger::reportPreformattedMessage(LogLevel level, std::string_view source, std::string_view message, bool showPopup, bool alreadyTruncated, Types::FlushTimeout *timeout)
{
    if (!isValidLevel(level))
    {
        return false;
    }

    std::string boundedMessageScratch;
    bool truncatedNow = false;
    const std::string_view reportMessage = boundedMessageView(message, alreadyTruncated, boundedMessageScratch, truncatedNow);
    const bool storedMessageAlreadyTruncated = alreadyTruncated || truncatedNow;

    std::lock_guard<std::mutex> lifecycleLock(loggerState().lifecycleMutex);

    (void)writeReportSynchronously(level, source, reportMessage, false, storedMessageAlreadyTruncated);

    writeDebugOutput(level, source, reportMessage);
    const bool flushed = timeout == nullptr ? flushSinksInternal() : (flushSinksInternal() && flushInternal(timeout->value));

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
void GameWIP::Logger::reportPreformattedMessage(LogLevel level, SourceId source, std::string_view message, bool showPopup)
{
    reportPreformattedMessage(level, source, message, showPopup, false, nullptr);
}

/// @brief Reports a preformatted message with a registered SourceId and optional bounded flush.
/// @param level Severity to log and mirror.
/// @param source Registered SourceId to resolve for platform debug output.
/// @param message Formatted message copied before this call returns.
/// @param showPopup True to run the fatal popup path after flush.
/// @param alreadyTruncated True when message already includes the truncation suffix.
/// @param timeout Optional bounded flush duration.
/// @return True when the flush completed.
bool GameWIP::Logger::reportPreformattedMessage(LogLevel level, SourceId source, std::string_view message, bool showPopup, bool alreadyTruncated, Types::FlushTimeout *timeout)
{
    if (!isValidLevel(level))
    {
        return false;
    }

    std::string boundedMessageScratch;
    bool truncatedNow = false;
    const std::string_view reportMessage = boundedMessageView(message, alreadyTruncated, boundedMessageScratch, truncatedNow);
    const bool storedMessageAlreadyTruncated = alreadyTruncated || truncatedNow;

    std::lock_guard<std::mutex> lifecycleLock(loggerState().lifecycleMutex);

    (void)writeReportSynchronously(level, source, reportMessage, storedMessageAlreadyTruncated);

    bool unknownSource = false;
    const std::shared_ptr<SourceRegistry> registry = loadSourceRegistry();
    const std::string_view sourceText = findSourceName(registry.get(), source, unknownSource);
    writeDebugOutput(level, sourceText, reportMessage);
    const bool flushed = timeout == nullptr ? flushSinksInternal() : (flushSinksInternal() && flushInternal(timeout->value));

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

/// @brief Logs an error, mirrors it to platform debug output, and flushes.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::reportError(std::string_view source, std::string_view message)
{
    report(LogLevel::Error, source, message);
}

/// @brief Logs an error, mirrors it to platform debug output, and waits for a bounded flush.
/// @param source Source text to copy.
/// @param timeout Maximum flush wait.
/// @param message Message text to copy.
/// @return True when the bounded flush completed.
bool GameWIP::Logger::reportError(std::string_view source, Types::FlushTimeout timeout, std::string_view message)
{
    return report(LogLevel::Error, source, timeout, message);
}

/// @brief Logs an error with a SourceId, mirrors it to platform debug output, and flushes.
/// @param source Registered SourceId to store and resolve.
/// @param message Message text to copy.
void GameWIP::Logger::reportError(SourceId source, std::string_view message)
{
    report(LogLevel::Error, source, message);
}

/// @brief Logs an error with a SourceId, mirrors it to platform debug output, and waits for a bounded flush.
/// @param source Registered SourceId to store and resolve.
/// @param timeout Maximum flush wait.
/// @param message Message text to copy.
/// @return True when the bounded flush completed.
bool GameWIP::Logger::reportError(SourceId source, Types::FlushTimeout timeout, std::string_view message)
{
    return report(LogLevel::Error, source, timeout, message);
}

/// @brief Logs fatal, mirrors it to platform debug output, flushes, and shows the fatal popup when enabled.
/// @param source Source text to copy.
/// @param message Message text to copy.
void GameWIP::Logger::reportFatal(std::string_view source, std::string_view message)
{
    report(LogLevel::Fatal, source, Types::ReportPopup::Fatal, message);
}

/// @brief Logs fatal, mirrors it to platform debug output, waits for a bounded flush, and shows the fatal popup when enabled.
/// @param source Source text to copy.
/// @param timeout Maximum flush wait.
/// @param message Message text to copy.
/// @return True when the bounded flush completed.
bool GameWIP::Logger::reportFatal(std::string_view source, Types::FlushTimeout timeout, std::string_view message)
{
    return report(LogLevel::Fatal, source, timeout, Types::ReportPopup::Fatal, message);
}

/// @brief Logs fatal with a SourceId, mirrors it to platform debug output, flushes, and shows the fatal popup when enabled.
/// @param source Registered SourceId to store and resolve.
/// @param message Message text to copy.
void GameWIP::Logger::reportFatal(SourceId source, std::string_view message)
{
    report(LogLevel::Fatal, source, Types::ReportPopup::Fatal, message);
}

/// @brief Logs fatal with a SourceId, mirrors it to platform debug output, waits for a bounded flush, and shows the fatal popup when enabled.
/// @param source Registered SourceId to store and resolve.
/// @param timeout Maximum flush wait.
/// @param message Message text to copy.
/// @return True when the bounded flush completed.
bool GameWIP::Logger::reportFatal(SourceId source, Types::FlushTimeout timeout, std::string_view message)
{
    return report(LogLevel::Fatal, source, timeout, Types::ReportPopup::Fatal, message);
}

/// @brief Logs fatal, flushes, optionally shows fatal popup, then terminates.
[[noreturn]] void GameWIP::Logger::fatalTerminate(std::string_view source, std::string_view message)
{
    report(LogLevel::Fatal, source, Types::ReportPopup::Fatal, message);
    std::terminate();
}

/// @brief Logs fatal with a SourceId, flushes, optionally shows fatal popup, then terminates.
[[noreturn]] void GameWIP::Logger::fatalTerminate(Types::SourceId source, std::string_view message)
{
    report(LogLevel::Fatal, source, Types::ReportPopup::Fatal, message);
    std::terminate();
}

/// @brief Logs fatal, waits for a bounded flush, optionally shows fatal popup, then terminates.
[[noreturn]] void GameWIP::Logger::fatalTerminate(std::string_view source, Types::FlushTimeout timeout, std::string_view message)
{
    report(LogLevel::Fatal, source, timeout, Types::ReportPopup::Fatal, message);
    std::terminate();
}

/// @brief Logs fatal with a SourceId, waits for a bounded flush, optionally shows fatal popup, then terminates.
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
        recordPlatformErrorIfAny(GameWIP::LoggerDetail::Platform::writeDebugOutput(line));
    }
    catch (...)
    {
        countAllocationFailure();
    }
}
