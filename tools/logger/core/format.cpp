/// @file format.cpp
/// @brief Timestamp, source, and bounded-message formatting.

#include "logger/internal/core.h"

namespace GameWIP::Logger::Internal::Core
{
    namespace
    {
        inline constexpr char kTimestampMillisecondsMarker = '\x1F';

        std::string formatTimestampForSecond(std::time_t time, std::string_view format, Status &outStatus)
        {
            std::string result;
            result.reserve(format.size() + 16);
            std::size_t segmentStart = 0;
            const auto appendTimeSegment = [&](std::size_t segmentEnd)
            {
                if(segmentEnd == segmentStart)
                {
                    return true;
                }
                std::string segment;
                const Status status =
                    GameWIP::Logger::Platform::formatLocalTime(time, format.substr(segmentStart, segmentEnd - segmentStart), segment);
                if(!status.ok())
                {
                    outStatus = status;
                    return false;
                }
                result.append(segment);
                return true;
            };

            for(std::size_t index = 0; index < format.size();)
            {
                if(format[index] == '%' && index + 1 < format.size())
                {
                    if(format[index + 1] == 'f')
                    {
                        if(!appendTimeSegment(index))
                        {
                            return "invalid-time";
                        }
                        result.push_back(kTimestampMillisecondsMarker);
                        index += 2;
                        segmentStart = index;
                        continue;
                    }

                    if(format[index + 1] == '%')
                    {
                        index += 2;
                        continue;
                    }
                }
                ++index;
            }

            if(!appendTimeSegment(format.size()))
            {
                return "invalid-time";
            }
            return result;
        }

        void renderTimestampMilliseconds(std::string &outText, std::string_view cachedFormat, std::int64_t milliseconds)
        {
            if(milliseconds < 0)
            {
                milliseconds += 1000;
            }
            outText.clear();
            outText.reserve(cachedFormat.size() + 2);
            for(const char character : cachedFormat)
            {
                if(character == kTimestampMillisecondsMarker)
                {
                    outText.push_back(static_cast<char>('0' + milliseconds / 100));
                    outText.push_back(static_cast<char>('0' + milliseconds / 10 % 10));
                    outText.push_back(static_cast<char>('0' + milliseconds % 10));
                }
                else
                {
                    outText.push_back(character);
                }
            }
        }
    } // namespace

    /// ------------------------------------------------------------
    /// Timestamp formatting
    /// ------------------------------------------------------------

    std::string formatTimeOrFallback(std::time_t time, std::string_view timeFormat, Status &outStatus)
    {
        std::string text;
        outStatus = GameWIP::Logger::Platform::formatLocalTime(time, timeFormat, text);

        if(!outStatus.ok())
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
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

        if(!cache.valid || cache.second != currentSecond)
        {
            Status status;
            cache.formatText = formatTimestampForSecond(currentSecond, loggerState().timestampFormat, status);

            if(!status.ok())
            {
                recordHealthFailure(Types::Health::FailureSource::TimeConversion, status, false);
            }
            cache.second = currentSecond;
            cache.valid = true;
        }

        renderTimestampMilliseconds(cache.text, cache.formatText, milliseconds);

        return cache.text;
    }

    std::string getDebugTimestampText(Status *outStatus)
    {
        std::lock_guard<std::mutex> lock(loggerState().debugTimestampMutex);
        const auto now = std::chrono::system_clock::now();
        const std::time_t currentSecond = std::chrono::system_clock::to_time_t(now);
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        Status status;

        if(!loggerState().debugTimestampCache.valid || loggerState().debugTimestampCache.second != currentSecond)
        {
            loggerState().debugTimestampCache.formatText = formatTimestampForSecond(currentSecond, loggerState().timestampFormat, status);
            loggerState().debugTimestampCache.second = currentSecond;
            loggerState().debugTimestampCache.valid = true;
        }

        if(outStatus)
        {
            *outStatus = status;
        }

        renderTimestampMilliseconds(loggerState().debugTimestampCache.text, loggerState().debugTimestampCache.formatText, milliseconds);

        return loggerState().debugTimestampCache.text;
    }

    /// ------------------------------------------------------------
    /// Source resolution
    /// ------------------------------------------------------------

    std::string_view findSourceName(const SourceRegistry *registry, SourceId source, bool &outUnknownSource)
    {
        outUnknownSource = false;

        if(registry)
        {
            if(const RegisteredSource *registeredSource = findSource(*registry, source))
            {
                return registeredSource->name;
            }
        }

        outUnknownSource = true;
        return "UnknownSource";
    }

    std::string_view resolveSourceText(const QueuedLogEntry &entry, const SourceRegistry *registry, bool &outUnknownSource)
    {
        if(!entry.usesRegisteredSource)
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

    /// ------------------------------------------------------------
    /// Message bounding
    /// ------------------------------------------------------------

    void buildTruncatedMessage(std::string &outMessage, std::string_view message, std::size_t maxMessageLength)
    {
        constexpr std::string_view suffix = "... [truncated]";
        outMessage.clear();

        if(maxMessageLength == 0)
        {
            return;
        }

        if(maxMessageLength <= suffix.size())
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

        if(alreadyTruncated)
        {
            return message;
        }

        const std::size_t maxMessageLength = loggerState().maxMessageLengthAtomic.load(std::memory_order_acquire);

        if(message.size() <= maxMessageLength)
        {
            return message;
        }

        buildTruncatedMessage(scratch, message, maxMessageLength);
        outTruncated = true;

        return scratch;
    }

} // namespace GameWIP::Logger::Internal::Core
