/// @file logger_reports.cpp
/// @brief Sink writes, synchronous emergency reports, flushing, and direct debug output.

#include "logger/internal/logger_core.h"

namespace GameWIP::Logger::Detail::Core
{
    namespace
    {
        // ------------------------------------------------------------
        // Deadline and report-result helpers
        // ------------------------------------------------------------

        [[nodiscard]] bool deadlineExpired(const FlushDeadline *deadline) noexcept
        {
            return deadline != nullptr && std::chrono::steady_clock::now() >= *deadline;
        }

        [[nodiscard]] bool markReportTimedOutIfExpired(const FlushDeadline *deadline, Types::Report::Result &result) noexcept
        {
            if(!deadlineExpired(deadline))
            {
                return false;
            }

            result.outcome = Types::Report::Outcome::TimedOut;
            return true;
        }

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

        [[nodiscard]] Types::Report::Delivery deliveryFrom(std::size_t eligible, std::size_t delivered) noexcept
        {
            if(delivered == 0)
            {
                return Types::Report::Delivery::None;
            }

            if(delivered >= eligible)
            {
                return Types::Report::Delivery::Complete;
            }

            return Types::Report::Delivery::Partial;
        }

        template <typename Function> [[nodiscard]] Types::Report::Result reportBoundary(Function &&function) noexcept
        {
            try
            {
                return std::forward<Function>(function)();
            }
            catch(const std::bad_alloc &)
            {
                return reportFailure(ErrorCode::OutOfMemory);
            }
            catch(...)
            {
                return reportFailure(ErrorCode::Unknown);
            }
        }
    } // namespace

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

    // ------------------------------------------------------------
    // Console color resolution and rendering
    // ------------------------------------------------------------

    static Terminal::Types::Style::Request terminalStyle(Color color)
    {
        Terminal::Types::Style::Request style;
        switch(color.kind)
        {
        case Color::Kind::Default:
            style.foreground = Terminal::defaultColor();
            break;
        case Color::Kind::Basic:
            style.foreground = Terminal::basicColor(static_cast<Terminal::Types::Style::BasicColor>(color.basic));
            break;
        case Color::Kind::Rgb:
            style.foreground = Terminal::rgbColor(color.red, color.green, color.blue);
            break;
        }
        return style;
    }

    static Color levelColor(LogLevel level)
    {
        const Theme &theme = loggerState().theme;
        switch(level)
        {
        case LogLevel::Trace:
            return theme.trace;
        case LogLevel::Debug:
            return theme.debug;
        case LogLevel::Info:
            return theme.info;
        case LogLevel::Warn:
            return theme.warn;
        case LogLevel::Error:
            return theme.error;
        case LogLevel::Fatal:
            return theme.fatal;
        }
        return theme.message;
    }

    static Color markedValueColor(char role, Color explicitColor)
    {
        switch(role)
        {
        case 'C':
            return loggerState().theme.code;
        case 'P':
            return loggerState().theme.path;
        case 'N':
            return loggerState().theme.number;
        case 'Q':
            return loggerState().theme.quoted;
        case 'F':
            return loggerState().theme.field;
        case 'X':
            return explicitColor;
        default:
            return loggerState().theme.message;
        }
    }

    static Color semanticColor(char role, Color severityColor, Color sourceColor)
    {
        switch(role)
        {
        case 'T':
            return loggerState().theme.timestamp;
        case 'L':
            return severityColor;
        case 'A':
            return sourceColor;
        case 'M':
            return loggerState().theme.messageUsesSeverityColor ? severityColor : loggerState().theme.message;
        default:
            return markedValueColor(role, {});
        }
    }

    static std::string plainOutputText(std::string_view text)
    {
        if(!text.empty() && text.front() == Protocol::kLayoutBegin)
        {
            const std::size_t payloadBegin = text.find(Protocol::kLayoutBegin, 1);
            if(payloadBegin == std::string_view::npos)
            {
                return {};
            }
            text.remove_prefix(payloadBegin + 1);
        }

        std::string result;
        result.reserve(text.size());
        for(std::size_t index = 0; index < text.size(); ++index)
        {
            if(text[index] == Protocol::kStyleBegin)
            {
                const std::size_t headerLength = index + 1 < text.size() && text[index + 1] == 'X' ? 7 : 2;
                index = std::min(text.size() - 1, index + headerLength - 1);
                continue;
            }
            if(text[index] == Protocol::kStyleScopeBegin)
            {
                ++index;
                continue;
            }
            if(text[index] != Protocol::kStyleEnd && text[index] != Protocol::kStyleScopeEnd && text[index] != Protocol::kLayoutBegin &&
               text[index] != Protocol::kLayoutSeparator)
            {
                result.push_back(text[index]);
            }
        }
        return result;
    }

    static Status writeConsoleLine(const LogStyle &logStyle, std::string_view line, Color severityColor, Color sourceColor)
    {
        using Terminal::Types::Output::Segment;
        // Default layouts and ordinary marked messages stay on the stack; only unusually segmented layouts need growth.
        std::array<Segment, 16> fixedSegments{
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({}),
            Terminal::textSegment({})};
        std::size_t fixedCount = 0;
        bool usesDynamicSegments = false;
        std::vector<Segment> dynamicSegments;
        const auto appendSegment = [&](Segment segment)
        {
            if(usesDynamicSegments)
            {
                dynamicSegments.push_back(segment);
            }
            else if(fixedCount < fixedSegments.size())
            {
                fixedSegments[fixedCount++] = segment;
            }
            else
            {
                dynamicSegments.reserve(fixedSegments.size() * 2);
                dynamicSegments.insert(dynamicSegments.end(), fixedSegments.begin(), fixedSegments.end());
                dynamicSegments.push_back(segment);
                usesDynamicSegments = true;
            }
        };

        const auto stream = logStyle.useStderr ? Terminal::Types::Output::Stream::Stderr : Terminal::Types::Output::Stream::Stdout;
        const Color plainMessageColor = loggerState().theme.messageUsesSeverityColor ? severityColor : loggerState().theme.message;
        const auto styleFor = [](Color color)
        {
            return terminalStyle(color);
        };

        const auto appendStyledText = [&](std::string_view text, Color baseColor)
        {
            std::array<Color, Detail::kMaxLayoutColorScopeDepth> colorScopes{};
            std::size_t colorScopeDepth = 0;
            std::size_t plainStart = 0;
            std::size_t index = 0;
            const auto currentColor = [&]
            {
                return colorScopeDepth == 0 ? baseColor : colorScopes[colorScopeDepth - 1];
            };
            while(index < text.size())
            {
                if(text[index] == Protocol::kStyleScopeBegin && index + 1 < text.size())
                {
                    if(index > plainStart)
                    {
                        appendSegment(Terminal::styledTextSegment(text.substr(plainStart, index - plainStart), styleFor(currentColor())));
                    }
                    if(colorScopeDepth < colorScopes.size())
                    {
                        colorScopes[colorScopeDepth++] = semanticColor(text[index + 1], severityColor, sourceColor);
                    }
                    index += 2;
                    plainStart = index;
                    continue;
                }
                if(text[index] == Protocol::kStyleScopeEnd)
                {
                    if(index > plainStart)
                    {
                        appendSegment(Terminal::styledTextSegment(text.substr(plainStart, index - plainStart), styleFor(currentColor())));
                    }
                    if(colorScopeDepth > 0)
                    {
                        --colorScopeDepth;
                    }
                    ++index;
                    plainStart = index;
                    continue;
                }
                if(text[index] != Protocol::kStyleBegin)
                {
                    ++index;
                    continue;
                }
                if(index > plainStart)
                {
                    appendSegment(Terminal::styledTextSegment(text.substr(plainStart, index - plainStart), styleFor(currentColor())));
                }
                if(index + 1 >= text.size())
                {
                    index = text.size();
                    plainStart = index;
                    break;
                }
                const char role = text[index + 1];
                Color explicitColor{};
                std::size_t valueStart = index + 2;
                if(role == 'X')
                {
                    if(index + 7 > text.size())
                    {
                        index = text.size();
                        plainStart = index;
                        break;
                    }
                    explicitColor.kind = static_cast<Color::Kind>(static_cast<unsigned char>(text[index + 2]));
                    explicitColor.basic = static_cast<Color::Basic>(static_cast<unsigned char>(text[index + 3]));
                    explicitColor.red = static_cast<std::uint8_t>(text[index + 4]);
                    explicitColor.green = static_cast<std::uint8_t>(text[index + 5]);
                    explicitColor.blue = static_cast<std::uint8_t>(text[index + 6]);
                    valueStart = index + 7;
                }
                const std::size_t markerEnd = text.find(Protocol::kStyleEnd, valueStart);
                const std::size_t valueEnd = markerEnd == std::string_view::npos ? text.size() : markerEnd;
                const Color markedColor = role == 'X' ? explicitColor : semanticColor(role, severityColor, sourceColor);
                appendSegment(Terminal::styledTextSegment(text.substr(valueStart, valueEnd - valueStart), styleFor(markedColor)));
                index = markerEnd == std::string_view::npos ? text.size() : markerEnd + 1;
                plainStart = index;
            }
            if(plainStart < text.size())
            {
                appendSegment(Terminal::styledTextSegment(text.substr(plainStart), styleFor(currentColor())));
            }
        };

        appendStyledText(line, plainMessageColor);
        Terminal::Types::Output::SegmentOptions options;
        options.styleMode = loggerState().consoleColorEnabledAtomic.load(std::memory_order_acquire) ? Terminal::Types::Style::Mode::Auto
                                                                                                    : Terminal::Types::Style::Mode::Never;
        options.appendLineEnding = true;
        options.lineEnding = Terminal::Types::Output::LineEnding::Native;
        options.flushMode =
            loggerState().flushConsoleEveryWriteAtomic.load(std::memory_order_acquire) ? IO::Types::FlushMode::Data : IO::Types::FlushMode::None;

        const std::span<const Segment> segments =
            usesDynamicSegments ? std::span<const Segment>(dynamicSegments) : std::span<const Segment>(fixedSegments).first(fixedCount);
        return Terminal::writeSegments(stream, segments, options);
    }

#if LOGGER_INTERNAL_TEST_HOOKS
    // ------------------------------------------------------------
    // File sink operations
    // ------------------------------------------------------------

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

    // ------------------------------------------------------------
    // Synchronous report sink delivery
    // ------------------------------------------------------------

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

    // ------------------------------------------------------------
    // Normal queued sink delivery
    // ------------------------------------------------------------

    SinkWriteResult writeLogEntry(const QueuedLogEntry &entry, TimestampCache &timestamp, std::string &lineScratch, std::string &fileBatchScratch)
    {
        SinkWriteResult result;
        const std::uint32_t runtime = loggerState().runtimeStateBits.load(std::memory_order_acquire);
        const OutputMode mode = runtimeStateOutput(runtime);

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

        if(unknown)
        {
            recordUnknownSourceUse();
        }

        return result;
    }

    // ------------------------------------------------------------
    // Sink flushing and report orchestration
    // ------------------------------------------------------------

    bool flushFileBatch(std::string &fileBatchScratch, bool forceFlush)
    {
        if(fileBatchScratch.empty())
        {
            return true;
        }

        Status status = IO::makeStatus(ErrorCode::NotOpen);
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

    static Types::Report::Result reportPreformattedMessageImpl(
        LogLevel level,
        std::string_view source,
        std::string_view message,
        bool showPopup,
        bool alreadyTruncated,
        const std::chrono::milliseconds *timeout,
        bool unknownSource,
        Color sourceColor)
    {
        Types::Report::Result result;

        // Validate the request before taking Logger's lifecycle lock.
        if(!isValidLevel(level))
        {
            result.status = IO::makeStatus(ErrorCode::InvalidArgument);
            return result;
        }

        if(timeout && timeout->count() < 0)
        {
            result.status = IO::makeStatus(ErrorCode::InvalidArgument);
            return result;
        }

        if(Unicode::Utf8::validate(source).outcome != Unicode::Types::ValidationOutcome::Valid ||
           Unicode::Utf8::validate(plainOutputText(message)).outcome != Unicode::Types::ValidationOutcome::Valid)
        {
            result.status = IO::makeStatus(ErrorCode::EncodingFailed);
            return result;
        }

        const FlushDeadline deadlineValue = timeout ? makeFlushDeadline(*timeout) : FlushDeadline{};
        const FlushDeadline *deadline = timeout ? &deadlineValue : nullptr;
        std::unique_lock<std::mutex> lifecycleLock(loggerState().lifecycleMutex, std::defer_lock);

        if(deadline == nullptr)
        {
            lifecycleLock.lock();
        }
        else if(!lockBefore(lifecycleLock, *deadline))
        {
            result.outcome = Types::Report::Outcome::TimedOut;
            return result;
        }

        std::string boundedScratch;
        bool truncatedNow = false;
        const std::string_view reportMessage = boundedMessageView(message, alreadyTruncated, boundedScratch, truncatedNow);
        const bool truncated = alreadyTruncated || truncatedNow;

        // Deliver to the configured sinks, then mirror to debugger output when enabled.
        ReportSinkProgress normal = writeReportSynchronously(level, source, reportMessage, unknownSource, truncated, deadline, sourceColor);
        result.status = firstFailure(std::move(result.status), normal.status);
        std::size_t eligible = normal.eligible;
        std::size_t delivered = normal.delivered;

        if(normal.timedOut)
        {
            result.outcome = Types::Report::Outcome::TimedOut;
        }

        const bool debugEligible = loggerState().debugOutputEnabledAtomic.load(std::memory_order_acquire);
        if(debugEligible)
        {
            ++eligible;

            if(!markReportTimedOutIfExpired(deadline, result))
            {
                const Status debugStatus = writeDebugOutputInternal(level, source, reportMessage);
                if(debugStatus.ok())
                {
                    ++delivered;
                }
                else
                {
                    result.status = firstFailure(std::move(result.status), debugStatus);
                }
            }
        }

#if LOGGER_INTERNAL_TEST_HOOKS
        if(deadline != nullptr && consumeTestHook(loggerTestHookState.nextTimedFlushTimeout))
        {
            result.outcome = Types::Report::Outcome::TimedOut;
        }
#endif

        // Flush report sinks before showing the fatal popup, then summarize delivery.
        if(result.outcome != Types::Report::Outcome::TimedOut)
        {
            const Types::FlushResult flushed = flushSinksInternal(deadline);
            result.status = firstFailure(std::move(result.status), flushed.status);

            if(flushed.outcome == Types::FlushOutcome::TimedOut)
            {
                result.outcome = Types::Report::Outcome::TimedOut;
            }
        }

        const bool popupEligible = showPopup && loggerState().fatalPopupEnabledAtomic.load(std::memory_order_acquire);
        if(popupEligible)
        {
            ++eligible;

            if(!markReportTimedOutIfExpired(deadline, result))
            {
                Status popupStatus;
#if LOGGER_INTERNAL_TEST_HOOKS
                if(consumeTestHook(loggerTestHookState.nextFatalPopupFailure))
                {
                    popupStatus = forcedFatalPopupStatus();
                }
                else
#endif
                {
                    TimestampCache popupTimestamp;
                    std::string popupLine;
                    buildLogLine(popupLine, getTimestampText(popupTimestamp), getLogStyle(level).text, source, reportMessage);
                    popupLine = plainOutputText(popupLine);
                    popupStatus = GameWIP::Logger::Detail::Platform::showFatalPopup(popupLine);
                }

                if(popupStatus.ok())
                {
                    ++delivered;
                }
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

std::uint32_t GameWIP::Logger::Detail::Core::currentProcessId() noexcept
{
    return GameWIP::Logger::Detail::Platform::currentProcessId();
}

// ------------------------------------------------------------
// Synchronous reporting
// ------------------------------------------------------------

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
            return reportPreformattedMessageImpl(level, source, message, showPopup, truncated, timeout, false, loggerState().theme.area);
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
            Color sourceColor = loggerState().theme.area;
            if(const RegisteredSource *registered = registry ? findSource(*registry, source) : nullptr; registered && registered->color)
            {
                sourceColor = *registered->color;
            }
            return reportPreformattedMessageImpl(level, sourceText, message, showPopup, truncated, timeout, unknown, sourceColor);
        });
}

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

// ------------------------------------------------------------
// Debugger output
// ------------------------------------------------------------

Status GameWIP::Logger::Detail::Core::writeDebugOutputInternal(LogLevel level, std::string_view source, std::string_view message) noexcept
{
    if(!loggerState().debugOutputEnabledAtomic.load(std::memory_order_acquire))
    {
        return {};
    }

    if(!isValidLevel(level))
    {
        return IO::makeStatus(ErrorCode::InvalidArgument);
    }

    try
    {
        std::string boundedScratch;
        bool truncated = false;
        const std::string_view messageText = boundedMessageView(message, false, boundedScratch, truncated);
        Status timeStatus;
        const std::string timestamp = getDebugTimestampText(&timeStatus);
        if(!timeStatus.ok())
        {
            recordHealthFailure(Types::Health::FailureSource::TimeConversion, timeStatus, false);
        }

        std::string line;
        buildLogLine(line, timestamp, getLogStyle(level).text, source, messageText);
        line = plainOutputText(line);
        line.push_back('\n');

        const Status status = GameWIP::Logger::Detail::Platform::writeDebugOutput(line);
        if(!status.ok())
        {
            recordHealthFailure(Types::Health::FailureSource::DebugOutput, status, true);
        }

        return status;
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

GameWIP::IO::Types::Status GameWIP::Logger::writeDebugOutput(LogLevel level, std::string_view source, std::string_view message) noexcept
{
    if(!loggerState().debugOutputEnabledAtomic.load(std::memory_order_acquire))
    {
        return {};
    }
    if(Unicode::Utf8::validate(source).outcome != Unicode::Types::ValidationOutcome::Valid ||
       Unicode::Utf8::validate(message).outcome != Unicode::Types::ValidationOutcome::Valid)
    {
        return IO::makeStatus(ErrorCode::EncodingFailed);
    }
    return GameWIP::Logger::Detail::Core::writeDebugOutputInternal(level, source, message);
}
