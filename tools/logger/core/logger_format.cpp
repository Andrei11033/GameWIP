/// @file logger_format.cpp
/// @brief Message bounding, line construction, timestamp/source resolution, and formatting bridges.

#include "logger/internal/logger_core.h"

namespace GameWIP::Logger::Detail::Core
{
    std::string formatTimeOrFallback(std::time_t time, std::string_view timeFormat, Status &outStatus)
    {
        std::string text;
        outStatus = GameWIP::Logger::Detail::Platform::formatLocalTime(time, timeFormat, text);
        if (!outStatus.ok())
        {
            return "invalid-time";
        }
        return text;
    }

    std::string getCurrentTimeText(std::string_view timeFormat, Status &outStatus)
    {
        const auto now = std::chrono::system_clock::now();
        return formatTimeOrFallback(std::chrono::system_clock::to_time_t(now), timeFormat, outStatus);
    }

    std::string_view getTimestampText(TimestampCache &cache)
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t currentSecond = std::chrono::system_clock::to_time_t(now);
        if (!cache.valid || cache.second != currentSecond)
        {
            Status status;
            cache.text = formatTimeOrFallback(currentSecond, "%H:%M:%S", status);
            if (!status.ok())
            {
                recordHealthFailure(Types::FailureSource::TimeConversion, status, false);
            }
            cache.second = currentSecond;
            cache.valid = true;
        }
        return cache.text;
    }

    std::string getDebugTimestampText(Status *outStatus)
    {
        std::lock_guard<std::mutex> lock(loggerState().debugTimestampMutex);
        const auto now = std::chrono::system_clock::now();
        const std::time_t currentSecond = std::chrono::system_clock::to_time_t(now);
        Status status;
        if (!loggerState().debugTimestampCache.valid || loggerState().debugTimestampCache.second != currentSecond)
        {
            loggerState().debugTimestampCache.text = formatTimeOrFallback(currentSecond, "%H:%M:%S", status);
            loggerState().debugTimestampCache.second = currentSecond;
            loggerState().debugTimestampCache.valid = true;
        }
        if (outStatus)
            *outStatus = status;
        return loggerState().debugTimestampCache.text;
    }

    std::string_view findSourceName(const SourceRegistry *registry, SourceId source, bool &outUnknownSource)
    {
        outUnknownSource = false;
        if (registry)
        {
            if (const RegisteredSource *registeredSource = findSource(*registry, source))
            {
                return registeredSource->name;
            }
        }
        outUnknownSource = true;
        return "UnknownSource";
    }

    std::string_view resolveSourceText(const QueuedLogEntry &entry, const SourceRegistry *registry, bool &outUnknownSource)
    {
        if (!entry.usesRegisteredSource)
        {
            outUnknownSource = false;
            return entry.sourceText.view();
        }
        return findSourceName(registry, entry.sourceId, outUnknownSource);
    }

    void recordUnknownSourceUse()
    {
        loggerState().stats.unknownSourceUses.fetch_add(1, std::memory_order_relaxed);
    }

    void buildTruncatedMessage(std::string &outMessage, std::string_view message, std::size_t maxMessageLength)
    {
        constexpr std::string_view suffix = "... [truncated]";
        outMessage.clear();
        if (maxMessageLength == 0)
            return;
        if (maxMessageLength <= suffix.size())
        {
            outMessage.assign(suffix.substr(0, maxMessageLength));
            return;
        }
        const std::size_t prefixLimit = maxMessageLength - suffix.size();
        const std::size_t prefixBytes = utf8PrefixBoundary(message, prefixLimit);
        outMessage.reserve(prefixBytes + suffix.size());
        outMessage.append(message.substr(0, prefixBytes));
        outMessage.append(suffix);
    }

    std::string_view boundedMessageView(std::string_view message, bool alreadyTruncated, std::string &scratch, bool &outTruncated)
    {
        outTruncated = false;
        if (alreadyTruncated)
            return message;
        const std::size_t maxMessageLength = loggerState().maxMessageLengthAtomic.load(std::memory_order_acquire);
        if (message.size() <= maxMessageLength)
            return message;
        buildTruncatedMessage(scratch, message, maxMessageLength);
        outTruncated = true;
        return scratch;
    }

    void buildLogLine(
        std::string &outMessage,
        std::string_view timestamp,
        std::string_view levelText,
        std::string_view source,
        std::string_view message)
    {
        constexpr std::size_t fixedFormatLength = 8;
        const std::size_t required = timestamp.size() + levelText.size() + source.size() + message.size() + fixedFormatLength;
        outMessage.clear();
        if (outMessage.capacity() < required)
            outMessage.reserve(required);
        outMessage.append("[");
        outMessage.append(timestamp);
        outMessage.append("][");
        outMessage.append(levelText);
        outMessage.append("][");
        outMessage.append(source);
        outMessage.append("]: ");
        outMessage.append(message);
    }
} // namespace GameWIP::Logger::Detail::Core

using namespace GameWIP::Logger::Detail::Core;

void GameWIP::Logger::Detail::Core::recordAllocationFailure()
{
    if (runtimeStateRunning(loggerState().runtimeStateBits.load(std::memory_order_acquire)))
    {
        countAllocationFailure();
    }
}

void GameWIP::Logger::Detail::Core::recordFormatFailure()
{
    if (runtimeStateRunning(loggerState().runtimeStateBits.load(std::memory_order_acquire)))
    {
        countFormatFailure();
    }
}

std::string &GameWIP::Logger::Detail::Core::formatScratch()
{
    return GameWIP::Logger::Detail::Platform::formatScratchForThread();
}

std::size_t GameWIP::Logger::Detail::Core::getMaxMessageLengthForFormatting()
{
    return loggerState().maxMessageLengthAtomic.load(std::memory_order_acquire);
}

FormatPolicy GameWIP::Logger::Detail::Core::getFormatPolicyForFormatting()
{
    return formatPolicyFromValue(loggerState().formatPolicyAtomic.load(std::memory_order_acquire));
}

void GameWIP::Logger::Detail::Core::releaseFormatScratchIfNeeded(std::string &scratch) noexcept
{
    if (loggerState().releaseMessageMemoryAfterWriteAtomic.load(std::memory_order_acquire))
    {
        const std::size_t maxMessageLength = loggerState().maxMessageLengthAtomic.load(std::memory_order_acquire);
        if (scratch.capacity() > maxMessageLength)
            std::string{}.swap(scratch);
        else
            scratch.clear();
    }
    GameWIP::Logger::Detail::Platform::releaseFormatScratchForThread();
}
