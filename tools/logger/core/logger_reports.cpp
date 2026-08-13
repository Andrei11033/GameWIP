/// @file logger_reports.cpp
/// @brief Sink writes, synchronous emergency reports, flushing, and direct debug output.

#include "logger/internal/logger_core.h"

namespace GameWIP::Logger::Detail::Core
{
    namespace
    {
        [[nodiscard]] bool deadlineExpired(const FlushDeadline *deadline) noexcept
        {
            return deadline != nullptr && std::chrono::steady_clock::now() >= *deadline;
        }

        [[nodiscard]] Types::Report::Delivery deliveryFrom(std::size_t eligible, std::size_t delivered) noexcept
        {
            if (delivered == 0)
                return Types::Report::Delivery::None;
            if (delivered >= eligible)
                return Types::Report::Delivery::Complete;
            return Types::Report::Delivery::Partial;
        }

        template <typename Function> [[nodiscard]] Types::Report::Result reportBoundary(Function &&function) noexcept
        {
            try
            {
                return std::forward<Function>(function)();
            }
            catch (const std::bad_alloc &)
            {
                return reportFailure(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return reportFailure(ErrorCode::Unknown);
            }
        }
    } // namespace

    FlushDeadline makeFlushDeadline(std::chrono::milliseconds timeout) noexcept
    {
        const FlushDeadline now = std::chrono::steady_clock::now();
        if (timeout.count() <= 0)
            return now;
        const auto available = FlushDeadline::max() - now;
        if (timeout >= std::chrono::duration_cast<std::chrono::milliseconds>(available))
            return FlushDeadline::max();
        return now + timeout;
    }

    bool lockBefore(std::unique_lock<std::mutex> &lock, FlushDeadline deadline)
    {
        while (!lock.try_lock())
        {
            if (std::chrono::steady_clock::now() >= deadline)
                return false;
            std::this_thread::yield();
        }
        return true;
    }

    static Status writeConsoleLine(const LogStyle &style, std::string_view line)
    {
        const std::array<Terminal::Types::Output::Segment, 1> segments{Terminal::styledTextSegment(line, style.terminalStyle)};
        Terminal::Types::Output::SegmentOptions options;
        options.styleMode = loggerState().consoleColorEnabledAtomic.load(std::memory_order_acquire) ? Terminal::Types::Style::Mode::Auto
                                                                                                    : Terminal::Types::Style::Mode::Never;
        options.appendLineEnding = true;
        options.lineEnding = Terminal::Types::Output::LineEnding::Native;
        options.flushMode =
            loggerState().flushConsoleEveryWriteAtomic.load(std::memory_order_acquire) ? IO::Types::FlushMode::Data : IO::Types::FlushMode::None;
        const auto stream = style.useStderr ? Terminal::Types::Output::Stream::Stderr : Terminal::Types::Output::Stream::Stdout;
        return Terminal::writeSegments(stream, segments, options);
    }

#if LOGGER_INTERNAL_TEST_HOOKS
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
#if LOGGER_INTERNAL_TEST_HOOKS
        if (consumeTestHook(loggerTestHookState.nextFileOpenFailure))
            return forcedFileStatus(ErrorCode::OpenFailed);
#endif
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
        if (consumeTestHook(loggerTestHookState.nextFileWriteFailure))
            return forcedFileStatus(ErrorCode::WriteFailed);
#endif
        return IO::writeAllText(writer, text).status;
    }

    Status flushFileForLogger(FileWriter &writer)
    {
#if LOGGER_INTERNAL_TEST_HOOKS
        if (consumeTestHook(loggerTestHookState.nextFileFlushFailure))
            return forcedFileStatus(ErrorCode::FlushFailed);
#endif
        return writer.flush(IO::Types::FlushMode::Data);
    }

    ReportSinkProgress writeReportSynchronously(
        LogLevel level,
        std::string_view source,
        std::string_view message,
        bool unknownSource,
        bool alreadyTruncated,
        const FlushDeadline *deadline)
    {
        ReportSinkProgress progress;
        if (!isValidLevel(level))
        {
            progress.status = IO::makeStatus(ErrorCode::InvalidArgument);
            return progress;
        }
        if (deadlineExpired(deadline))
        {
            progress.timedOut = true;
            return progress;
        }

        try
        {
            const OutputMode mode = runtimeStateOutput(loggerState().runtimeStateBits.load(std::memory_order_acquire));
            const bool consoleOutput = hasConsoleOutput(mode);
            const bool fileOutput = hasFileOutput(mode);
            progress.eligible = static_cast<std::size_t>(consoleOutput) + static_cast<std::size_t>(fileOutput);
            if (progress.eligible == 0)
                return progress;

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
            {
                std::unique_lock<std::mutex> outputLock(loggerState().outputMutex, std::defer_lock);
                if (deadline == nullptr)
                    outputLock.lock();
                else if (!lockBefore(outputLock, *deadline))
                {
                    progress.timedOut = true;
                    return progress;
                }

                if (consoleOutput)
                {
                    if (deadlineExpired(deadline))
                        progress.timedOut = true;
                    else
                    {
                        consoleStatus = writeConsoleLine(style, line);
                        if (consoleStatus.ok())
                            ++progress.delivered;
                        else
                            progress.status = firstFailure(std::move(progress.status), consoleStatus);
                    }
                }

                if (fileOutput && !progress.timedOut)
                {
                    if (deadlineExpired(deadline))
                        progress.timedOut = true;
                    else if (loggerState().fileOutputAvailableAtomic.load(std::memory_order_acquire) && loggerState().logFile.isOpen())
                    {
                        std::string fileLine(line);
                        fileLine.push_back('\n');
                        fileStatus = writeFileForLogger(loggerState().logFile, fileLine);
                        if (fileStatus.ok())
                            ++progress.delivered;
                        else
                            progress.status = firstFailure(std::move(progress.status), fileStatus);
                    }
                    else
                    {
                        fileStatus = IO::makeStatus(ErrorCode::NotOpen);
                        progress.status = firstFailure(std::move(progress.status), fileStatus);
                    }
                }
            }

            if (!consoleStatus.ok())
                recordHealthFailure(Types::Health::FailureSource::Console, consoleStatus, true);
            if (!fileStatus.ok())
            {
                loggerState().stats.fileWriteFailures.fetch_add(1, std::memory_order_relaxed);
                recordHealthFailure(Types::Health::FailureSource::File, fileStatus, true);
            }
            if (progress.delivered > 0)
            {
                loggerState().stats.written.fetch_add(1, std::memory_order_relaxed);
                if (truncated)
                    loggerState().stats.truncated.fetch_add(1, std::memory_order_relaxed);
                if (unknownSource)
                    recordUnknownSourceUse();
            }
        }
        catch (const std::bad_alloc &)
        {
            progress.status = firstFailure(std::move(progress.status), IO::makeStatus(ErrorCode::OutOfMemory));
        }
        catch (...)
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
        return writeReportSynchronously(level, findSourceName(registry.get(), source, unknown), message, unknown, alreadyTruncated, deadline);
    }

    SinkWriteResult writeLogEntry(const QueuedLogEntry &entry, TimestampCache &timestamp, std::string &lineScratch, std::string &fileBatchScratch)
    {
        SinkWriteResult result;
        const std::uint32_t runtime = loggerState().runtimeStateBits.load(std::memory_order_acquire);
        const OutputMode mode = runtimeStateOutput(runtime);
        if (mode == OutputMode::None || !isValidLevel(entry.level))
            return result;
        if (toLevelValue(entry.level) < toLevelValue(runtimeStateMinLevel(runtime)))
            return result;
        const auto registry = entry.usesRegisteredSource ? loadSourceRegistry() : std::shared_ptr<SourceRegistry>{};
        const std::uint8_t bit = levelBit(entry.level);
        if (bit == 0 || (runtimeStateLevelMask(runtime) & bit) == 0 ||
            (entry.usesRegisteredSource && !sourceEnabledRuntime(registry.get(), entry.sourceId)))
            return result;

        const LogStyle style = getLogStyle(entry.level);
        bool unknown = false;
        const std::string_view source = resolveSourceText(entry, registry.get(), unknown);
        buildLogLine(lineScratch, getTimestampText(timestamp), style.text, source, entry.message.view());

        if (hasConsoleOutput(mode))
        {
            Status consoleStatus;
            {
                std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
                consoleStatus = writeConsoleLine(style, lineScratch);
            }
            result.acceptedImmediateSink = consoleStatus.ok();
            if (!consoleStatus.ok())
                recordHealthFailure(Types::Health::FailureSource::Console, consoleStatus, true);
        }

        if (hasFileOutput(mode))
        {
            if (loggerState().fileOutputAvailableAtomic.load(std::memory_order_acquire))
            {
                fileBatchScratch.append(lineScratch);
                fileBatchScratch.push_back('\n');
                result.queuedFile = true;
            }
        }
        if (unknown)
            recordUnknownSourceUse();
        return result;
    }

    bool flushFileBatch(std::string &fileBatchScratch, bool forceFlush)
    {
        if (fileBatchScratch.empty())
            return true;
        Status status = IO::makeStatus(ErrorCode::NotOpen);
        {
            std::lock_guard<std::mutex> outputLock(loggerState().outputMutex);
            if (loggerState().logFile.isOpen())
            {
                status = writeFileForLogger(loggerState().logFile, fileBatchScratch);
                if (status.ok() && (forceFlush || loggerState().flushFileEveryBatchAtomic.load(std::memory_order_acquire)))
                    status = flushFileForLogger(loggerState().logFile);
            }
        }
        fileBatchScratch.clear();
        if (!status.ok())
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
        if (deadline == nullptr)
            outputLock.lock();
        else if (!lockBefore(outputLock, *deadline))
        {
            result.outcome = Types::FlushOutcome::TimedOut;
            return result;
        }

        if (hasConsoleOutput(mode))
        {
            if (deadlineExpired(deadline))
                result.outcome = Types::FlushOutcome::TimedOut;
            else
            {
                Status stdoutStatus = Terminal::flush(Terminal::Types::Output::Stream::Stdout, IO::Types::FlushMode::Data);
                Status stderrStatus;
                if (!deadlineExpired(deadline))
                    stderrStatus = Terminal::flush(Terminal::Types::Output::Stream::Stderr, IO::Types::FlushMode::Data);
                else
                    result.outcome = Types::FlushOutcome::TimedOut;
                consoleStatus = firstFailure(std::move(stdoutStatus), stderrStatus);
                result.status = firstFailure(std::move(result.status), consoleStatus);
            }
        }

        if (hasFileOutput(mode) && result.outcome != Types::FlushOutcome::TimedOut)
        {
            if (deadlineExpired(deadline))
                result.outcome = Types::FlushOutcome::TimedOut;
            else if (loggerState().fileOutputAvailableAtomic.load(std::memory_order_acquire) && loggerState().logFile.isOpen())
            {
                fileStatus = flushFileForLogger(loggerState().logFile);
                result.status = firstFailure(std::move(result.status), fileStatus);
            }
            else
            {
                fileStatus = IO::makeStatus(ErrorCode::NotOpen);
                result.status = firstFailure(std::move(result.status), fileStatus);
            }
        }
        outputLock.unlock();

        if (!consoleStatus.ok())
            recordHealthFailure(Types::Health::FailureSource::Console, consoleStatus, true);
        if (!fileStatus.ok())
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
        if (deadline && consumeTestHook(loggerTestHookState.nextTimedFlushTimeout))
        {
            result.outcome = Types::FlushOutcome::TimedOut;
            return result;
        }
#endif
        {
            std::unique_lock<std::mutex> lock(loggerState().logMutex, std::defer_lock);
            if (deadline == nullptr)
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
                if (!lockBefore(lock, *deadline))
                {
                    result.outcome = Types::FlushOutcome::TimedOut;
                    return result;
                }
                if (!loggerState().logCondition.wait_until(
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

    Types::Report::Result reportPreformattedMessageImpl(
        LogLevel level,
        std::string_view source,
        std::string_view message,
        bool showPopup,
        bool alreadyTruncated,
        const std::chrono::milliseconds *timeout,
        bool unknownSource)
    {
        Types::Report::Result result;
        if (!isValidLevel(level))
        {
            result.status = IO::makeStatus(ErrorCode::InvalidArgument);
            return result;
        }
        if (timeout && timeout->count() < 0)
        {
            result.status = IO::makeStatus(ErrorCode::InvalidArgument);
            return result;
        }
        if (Unicode::Utf8::validate(source).outcome != Unicode::Types::ValidationOutcome::Valid ||
            Unicode::Utf8::validate(message).outcome != Unicode::Types::ValidationOutcome::Valid)
        {
            result.status = IO::makeStatus(ErrorCode::EncodingFailed);
            return result;
        }

        const FlushDeadline deadlineValue = timeout ? makeFlushDeadline(*timeout) : FlushDeadline{};
        const FlushDeadline *deadline = timeout ? &deadlineValue : nullptr;
        std::unique_lock<std::mutex> lifecycleLock(loggerState().lifecycleMutex, std::defer_lock);
        if (deadline == nullptr)
            lifecycleLock.lock();
        else if (!lockBefore(lifecycleLock, *deadline))
        {
            result.outcome = Types::Report::Outcome::TimedOut;
            return result;
        }

        std::string boundedScratch;
        bool truncatedNow = false;
        const std::string_view reportMessage = boundedMessageView(message, alreadyTruncated, boundedScratch, truncatedNow);
        const bool truncated = alreadyTruncated || truncatedNow;

        ReportSinkProgress normal = writeReportSynchronously(level, source, reportMessage, unknownSource, truncated, deadline);
        result.status = firstFailure(std::move(result.status), normal.status);
        std::size_t eligible = normal.eligible;
        std::size_t delivered = normal.delivered;
        if (normal.timedOut)
            result.outcome = Types::Report::Outcome::TimedOut;

        const bool debugEligible = loggerState().debugOutputEnabledAtomic.load(std::memory_order_acquire);
        if (debugEligible)
        {
            ++eligible;
            if (deadlineExpired(deadline))
                result.outcome = Types::Report::Outcome::TimedOut;
            else
            {
                const Status debugStatus = GameWIP::Logger::writeDebugOutput(level, source, reportMessage);
                if (debugStatus.ok())
                    ++delivered;
                else
                    result.status = firstFailure(std::move(result.status), debugStatus);
            }
        }

#if LOGGER_INTERNAL_TEST_HOOKS
        if (deadline != nullptr && consumeTestHook(loggerTestHookState.nextTimedFlushTimeout))
        {
            result.outcome = Types::Report::Outcome::TimedOut;
        }
#endif

        if (result.outcome != Types::Report::Outcome::TimedOut)
        {
            const Types::FlushResult flushed = flushSinksInternal(deadline);
            result.status = firstFailure(std::move(result.status), flushed.status);
            if (flushed.outcome == Types::FlushOutcome::TimedOut)
                result.outcome = Types::Report::Outcome::TimedOut;
        }

        const bool popupEligible = showPopup && loggerState().fatalPopupEnabledAtomic.load(std::memory_order_acquire);
        if (popupEligible)
        {
            ++eligible;
            if (deadlineExpired(deadline))
                result.outcome = Types::Report::Outcome::TimedOut;
            else
            {
                Status popupStatus;
#if LOGGER_INTERNAL_TEST_HOOKS
                if (consumeTestHook(loggerTestHookState.nextFatalPopupFailure))
                    popupStatus = forcedFatalPopupStatus();
                else
#endif
                    popupStatus = GameWIP::Logger::Detail::Platform::showFatalPopup(reportMessage);
                if (popupStatus.ok())
                    ++delivered;
                else
                {
                    result.status = firstFailure(std::move(result.status), popupStatus);
                    recordHealthFailure(Types::Health::FailureSource::FatalPopup, popupStatus, true);
                }
            }
        }

        result.delivery = deliveryFrom(eligible, delivered);
        return result;
    }
} // namespace GameWIP::Logger::Detail::Core

using namespace GameWIP::Logger::Detail::Core;

void GameWIP::Logger::Detail::Core::enqueuePreformattedMessage(LogLevel level, std::string_view source, std::string_view message) noexcept
{
    enqueuePreformattedMessage(level, source, message, false);
}
void GameWIP::Logger::Detail::Core::enqueuePreformattedMessage(
    LogLevel level,
    std::string_view source,
    std::string_view message,
    bool truncated) noexcept
{
    try
    {
        enqueueAndWakeWorker(makePendingEntry(level, source, message, truncated));
    }
    catch (...)
    {
        countAllocationFailure();
    }
}
void GameWIP::Logger::Detail::Core::enqueuePreformattedMessage(LogLevel level, SourceId source, std::string_view message) noexcept
{
    enqueuePreformattedMessage(level, source, message, false);
}
void GameWIP::Logger::Detail::Core::enqueuePreformattedMessage(LogLevel level, SourceId source, std::string_view message, bool truncated) noexcept
{
    try
    {
        enqueueAndWakeWorker(makePendingEntry(level, source, message, truncated));
    }
    catch (...)
    {
        countAllocationFailure();
    }
}

GameWIP::Logger::Types::Report::Result GameWIP::Logger::Detail::Core::reportPreformattedMessage(
    LogLevel level,
    std::string_view source,
    std::string_view message,
    bool showPopup,
    bool truncated,
    const std::chrono::milliseconds *timeout) noexcept
{
    return reportBoundary(
        [&]
        {
            return reportPreformattedMessageImpl(level, source, message, showPopup, truncated, timeout, false);
        });
}

GameWIP::Logger::Types::Report::Result GameWIP::Logger::Detail::Core::reportPreformattedMessage(
    LogLevel level,
    SourceId source,
    std::string_view message,
    bool showPopup,
    bool truncated,
    const std::chrono::milliseconds *timeout) noexcept
{
    return reportBoundary(
        [&]
        {
            const auto registry = loadSourceRegistry();
            bool unknown = false;
            const std::string_view sourceText = findSourceName(registry.get(), source, unknown);
            return reportPreformattedMessageImpl(level, sourceText, message, showPopup, truncated, timeout, unknown);
        });
}

void GameWIP::Logger::log(LogLevel level, std::string_view source, std::string_view message) noexcept
{
    if (!shouldLog(level))
        return;
    try
    {
        enqueueAndWakeWorker(makePendingEntry(level, source, message));
    }
    catch (...)
    {
        countAllocationFailure();
    }
}
void GameWIP::Logger::log(LogLevel level, SourceId source, std::string_view message) noexcept
{
    if (!shouldLog(level, source))
        return;
    try
    {
        enqueueAndWakeWorker(makePendingEntry(level, source, message));
    }
    catch (...)
    {
        countAllocationFailure();
    }
}

#define GAMEWIP_LOGGER_DEFINE_LEVEL(name, levelValue) \
    void GameWIP::Logger::name(std::string_view source, std::string_view message) noexcept \
    { \
        log(levelValue, source, message); \
    } \
    void GameWIP::Logger::name(SourceId source, std::string_view message) noexcept \
    { \
        log(levelValue, source, message); \
    }
GAMEWIP_LOGGER_DEFINE_LEVEL(trace, LogLevel::Trace)
GAMEWIP_LOGGER_DEFINE_LEVEL(debug, LogLevel::Debug)
GAMEWIP_LOGGER_DEFINE_LEVEL(info, LogLevel::Info)
GAMEWIP_LOGGER_DEFINE_LEVEL(warn, LogLevel::Warn)
GAMEWIP_LOGGER_DEFINE_LEVEL(error, LogLevel::Error)
GAMEWIP_LOGGER_DEFINE_LEVEL(fatal, LogLevel::Fatal)
#undef GAMEWIP_LOGGER_DEFINE_LEVEL

GameWIP::Logger::Types::Report::Result GameWIP::Logger::report(LogLevel level, std::string_view source, std::string_view message) noexcept
{
    return reportPreformattedMessage(level, source, message, false, false, nullptr);
}
GameWIP::Logger::Types::Report::Result GameWIP::Logger::report(
    LogLevel level,
    std::string_view source,
    std::chrono::milliseconds timeout,
    std::string_view message) noexcept
{
    return reportPreformattedMessage(level, source, message, false, false, &timeout);
}
GameWIP::Logger::Types::Report::Result GameWIP::Logger::report(LogLevel level, SourceId source, std::string_view message) noexcept
{
    return reportPreformattedMessage(level, source, message, false, false, nullptr);
}
GameWIP::Logger::Types::Report::Result GameWIP::Logger::report(
    LogLevel level,
    SourceId source,
    std::chrono::milliseconds timeout,
    std::string_view message) noexcept
{
    return reportPreformattedMessage(level, source, message, false, false, &timeout);
}

GameWIP::Logger::Types::Report::Result GameWIP::Logger::reportError(std::string_view source, std::string_view message) noexcept
{
    return report(LogLevel::Error, source, message);
}
GameWIP::Logger::Types::Report::Result GameWIP::Logger::reportError(
    std::string_view source,
    std::chrono::milliseconds timeout,
    std::string_view message) noexcept
{
    return report(LogLevel::Error, source, timeout, message);
}
GameWIP::Logger::Types::Report::Result GameWIP::Logger::reportError(SourceId source, std::string_view message) noexcept
{
    return report(LogLevel::Error, source, message);
}
GameWIP::Logger::Types::Report::Result GameWIP::Logger::reportError(
    SourceId source,
    std::chrono::milliseconds timeout,
    std::string_view message) noexcept
{
    return report(LogLevel::Error, source, timeout, message);
}

GameWIP::Logger::Types::Report::Result GameWIP::Logger::reportFatal(std::string_view source, std::string_view message) noexcept
{
    return reportPreformattedMessage(LogLevel::Fatal, source, message, true, false, nullptr);
}
GameWIP::Logger::Types::Report::Result GameWIP::Logger::reportFatal(
    std::string_view source,
    std::chrono::milliseconds timeout,
    std::string_view message) noexcept
{
    return reportPreformattedMessage(LogLevel::Fatal, source, message, true, false, &timeout);
}
GameWIP::Logger::Types::Report::Result GameWIP::Logger::reportFatal(SourceId source, std::string_view message) noexcept
{
    return reportPreformattedMessage(LogLevel::Fatal, source, message, true, false, nullptr);
}
GameWIP::Logger::Types::Report::Result GameWIP::Logger::reportFatal(
    SourceId source,
    std::chrono::milliseconds timeout,
    std::string_view message) noexcept
{
    return reportPreformattedMessage(LogLevel::Fatal, source, message, true, false, &timeout);
}

[[noreturn]] void GameWIP::Logger::fatalTerminate(std::string_view source, std::string_view message) noexcept
{
    static_cast<void>(reportFatal(source, message));
    std::terminate();
}
[[noreturn]] void GameWIP::Logger::fatalTerminate(SourceId source, std::string_view message) noexcept
{
    static_cast<void>(reportFatal(source, message));
    std::terminate();
}
[[noreturn]] void GameWIP::Logger::fatalTerminate(std::string_view source, std::chrono::milliseconds timeout, std::string_view message) noexcept
{
    static_cast<void>(reportFatal(source, timeout, message));
    std::terminate();
}
[[noreturn]] void GameWIP::Logger::fatalTerminate(SourceId source, std::chrono::milliseconds timeout, std::string_view message) noexcept
{
    static_cast<void>(reportFatal(source, timeout, message));
    std::terminate();
}

GameWIP::IO::Types::Status GameWIP::Logger::writeDebugOutput(LogLevel level, std::string_view source, std::string_view message) noexcept
{
    if (!loggerState().debugOutputEnabledAtomic.load(std::memory_order_acquire))
        return {};
    if (!isValidLevel(level))
        return IO::makeStatus(ErrorCode::InvalidArgument);
    if (Unicode::Utf8::validate(source).outcome != Unicode::Types::ValidationOutcome::Valid ||
        Unicode::Utf8::validate(message).outcome != Unicode::Types::ValidationOutcome::Valid)
        return IO::makeStatus(ErrorCode::EncodingFailed);

    try
    {
        std::string boundedScratch;
        bool truncated = false;
        const std::string_view messageText = boundedMessageView(message, false, boundedScratch, truncated);
        Status timeStatus;
        const std::string timestamp = getDebugTimestampText(&timeStatus);
        if (!timeStatus.ok())
            recordHealthFailure(Types::Health::FailureSource::TimeConversion, timeStatus, false);
        std::string line;
        buildLogLine(line, timestamp, getLogStyle(level).text, source, messageText);
        line.push_back('\n');
        const Status status = GameWIP::Logger::Detail::Platform::writeDebugOutput(line);
        if (!status.ok())
            recordHealthFailure(Types::Health::FailureSource::DebugOutput, status, true);
        return status;
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
