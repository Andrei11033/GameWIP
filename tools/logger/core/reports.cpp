/// @file reports.cpp
/// @brief Synchronous report construction, fatal handling, and debugger output.

#include "logger/internal/core.h"
#include "logger/reports.h"

namespace GameWIP::Logger::Internal::Core
{
    namespace
    {
        /// ------------------------------------------------------------
        /// Report deadlines and results
        /// ------------------------------------------------------------

        [[nodiscard]] bool markReportTimedOutIfExpired(const FlushDeadline *deadline, Types::Report::Info &info) noexcept
        {
            if(!deadlineExpired(deadline))
            {
                return false;
            }

            info.outcome = Types::Report::Outcome::TimedOut;
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

    /// ------------------------------------------------------------
    /// Report delivery
    /// ------------------------------------------------------------

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
        Types::Report::Info info;
        Status status;

        // Validate the request before taking Logger's lifecycle lock.
        if(!isValidLevel(level))
        {
            return Types::Report::Result::failure(makeStatus(ErrorCode::InvalidArgument));
        }

        if(timeout && timeout->count() < 0)
        {
            return Types::Report::Result::failure(makeStatus(ErrorCode::InvalidArgument));
        }

        if(Unicode::Utf8::validate(source).outcome != Unicode::Types::ValidationOutcome::Valid ||
           Unicode::Utf8::validate(plainOutputText(message)).outcome != Unicode::Types::ValidationOutcome::Valid)
        {
            return Types::Report::Result::failure(makeStatus(ErrorCode::EncodingFailed));
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
            info.outcome = Types::Report::Outcome::TimedOut;
            return Types::Report::Result::success(std::move(info));
        }

        std::string boundedScratch;
        bool truncatedNow = false;
        const std::string_view reportMessage = boundedMessageView(message, alreadyTruncated, boundedScratch, truncatedNow);
        const bool truncated = alreadyTruncated || truncatedNow;

        // Deliver to the configured sinks, then mirror to debugger output when enabled.
        const ReportSinkResult normal = writeReportSynchronously(level, source, reportMessage, unknownSource, truncated, deadline, sourceColor);
        status = firstFailure(std::move(status), normal.status());
        std::size_t eligible = 0;
        std::size_t delivered = 0;

        if(normal.hasValue())
        {
            eligible = normal.value().eligible;
            delivered = normal.value().delivered;

            if(normal.value().timedOut)
            {
                info.outcome = Types::Report::Outcome::TimedOut;
            }
        }

        const bool debugEligible = loggerState().debugOutputEnabledAtomic.load(std::memory_order_acquire);
        if(debugEligible)
        {
            ++eligible;

            if(!markReportTimedOutIfExpired(deadline, info))
            {
                const Status debugStatus = writeDebugOutputInternal(level, source, reportMessage);
                if(debugStatus.ok())
                {
                    ++delivered;
                }
                else
                {
                    status = firstFailure(std::move(status), debugStatus);
                }
            }
        }

#if LOGGER_INTERNAL_TEST_HOOKS
        if(deadline != nullptr && consumeTestHook(loggerTestHookState.nextTimedFlushTimeout))
        {
            info.outcome = Types::Report::Outcome::TimedOut;
        }
#endif

        // Flush report sinks before showing the fatal popup, then summarize delivery.
        if(info.outcome != Types::Report::Outcome::TimedOut)
        {
            const Types::FlushResult flushed = flushSinksInternal(deadline);
            status = firstFailure(std::move(status), flushed.status());

            if(flushed.hasValue() && flushed.value().outcome == Types::FlushOutcome::TimedOut)
            {
                info.outcome = Types::Report::Outcome::TimedOut;
            }
        }

        const bool popupEligible = showPopup && loggerState().fatalPopupEnabledAtomic.load(std::memory_order_acquire);
        if(popupEligible)
        {
            ++eligible;

            if(!markReportTimedOutIfExpired(deadline, info))
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
                    popupStatus = GameWIP::Logger::Platform::showFatalPopup(popupLine);
                }

                if(popupStatus.ok())
                {
                    ++delivered;
                }
                else
                {
                    status = firstFailure(std::move(status), popupStatus);
                    recordHealthFailure(Types::Health::FailureSource::FatalPopup, popupStatus, true);
                }
            }
        }

        info.delivery = deliveryFrom(eligible, delivered);

        if(status.ok())
        {
            return Types::Report::Result::success(std::move(info));
        }

        return Types::Report::Result::failure(std::move(status), std::move(info));
    }
} // namespace GameWIP::Logger::Internal::Core
using namespace GameWIP::Logger::Internal::Core;

/// ------------------------------------------------------------
/// Synchronous reporting
/// ------------------------------------------------------------

GameWIP::Logger::Types::Report::Result GameWIP::Logger::Internal::Core::reportPreformattedMessage(
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

GameWIP::Logger::Types::Report::Result GameWIP::Logger::Internal::Core::reportPreformattedMessage(
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

/// ------------------------------------------------------------
/// Debugger output
/// ------------------------------------------------------------

Status GameWIP::Logger::Internal::Core::writeDebugOutputInternal(LogLevel level, std::string_view source, std::string_view message) noexcept
{
    if(!loggerState().debugOutputEnabledAtomic.load(std::memory_order_acquire))
    {
        return {};
    }

    if(!isValidLevel(level))
    {
        return makeStatus(ErrorCode::InvalidArgument);
    }

    try
    {
        // Bound the text and build a plain diagnostic line.
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

        // Write to the native debugger channel and retain failures in health.
        const Status status = GameWIP::Logger::Platform::writeDebugOutput(line);
        if(!status.ok())
        {
            recordHealthFailure(Types::Health::FailureSource::DebugOutput, status, true);
        }

        return status;
    }
    catch(const std::bad_alloc &)
    {
        return makeStatus(ErrorCode::OutOfMemory);
    }
    catch(...)
    {
        return makeStatus(ErrorCode::Unknown);
    }
}

GameWIP::Status::Types::Result<void> GameWIP::Logger::writeDebugOutput(LogLevel level, std::string_view source, std::string_view message) noexcept
{
    if(!loggerState().debugOutputEnabledAtomic.load(std::memory_order_acquire))
    {
        return GameWIP::Status::Types::Result<void>::success();
    }

    if(Unicode::Utf8::validate(source).outcome != Unicode::Types::ValidationOutcome::Valid ||
       Unicode::Utf8::validate(message).outcome != Unicode::Types::ValidationOutcome::Valid)
    {
        return GameWIP::Status::Types::Result<void>::failure(makeStatus(ErrorCode::EncodingFailed));
    }
    return GameWIP::Logger::Internal::Core::resultFromStatus(GameWIP::Logger::Internal::Core::writeDebugOutputInternal(level, source, message));
}
