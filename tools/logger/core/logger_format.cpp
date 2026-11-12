/// @file logger_format.cpp
/// @brief Message bounding, line construction, timestamp/source resolution, and formatting bridges.

#include "logger/internal/logger_core.h"

namespace GameWIP::Logger::Detail::Core
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
                    GameWIP::Logger::Detail::Platform::formatLocalTime(time, format.substr(segmentStart, segmentEnd - segmentStart), segment);
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

    // ------------------------------------------------------------
    // Timestamp formatting
    // ------------------------------------------------------------

    std::string formatTimeOrFallback(std::time_t time, std::string_view timeFormat, Status &outStatus)
    {
        std::string text;
        outStatus = GameWIP::Logger::Detail::Platform::formatLocalTime(time, timeFormat, text);

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

    // ------------------------------------------------------------
    // Source resolution
    // ------------------------------------------------------------

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

    // ------------------------------------------------------------
    // Message bounding and line assembly
    // ------------------------------------------------------------

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

    namespace
    {
        constexpr LayoutTemplate kDefaultLogLayout =
            makeLayout("{@timestamp}[{time}]{/}{@area}[{area:<auto}]{/}{@severity}[{level:<5}]{/} {message}");

        const std::string &compiledDefaultLogLayout()
        {
            static const std::string layout = Detail::compileLayoutTemplate(kDefaultLogLayout.text);
            return layout;
        }

        struct LayoutRecordData
        {
            std::string_view layout;
            std::string_view fields;
            std::string_view sourceFile;
            std::string_view sourceFunction;
            std::string_view sourceLine;
            std::string_view threadId;
            std::string_view processId;
        };

        bool readRecordEnvelope(std::string_view &message, LayoutRecordData &data)
        {
            if(message.empty() || message.front() != Protocol::kLayoutBegin)
            {
                return false;
            }
            const std::size_t layoutEnd = message.find(Protocol::kLayoutSeparator, 1);
            if(layoutEnd == std::string_view::npos)
            {
                return false;
            }
            std::size_t cursor = layoutEnd + 1;
            const std::size_t fieldsEnd = message.find(Protocol::kLayoutSeparator, cursor);
            if(fieldsEnd == std::string_view::npos)
            {
                return false;
            }

            data.layout = message.substr(1, layoutEnd - 1);
            data.fields = message.substr(cursor, fieldsEnd - cursor);
            cursor = fieldsEnd + 1;

            std::array<std::string_view *, 4> metadata{&data.sourceFile, &data.sourceFunction, &data.sourceLine, &data.threadId};
            for(std::string_view *value : metadata)
            {
                const std::size_t end = message.find(Protocol::kLayoutSeparator, cursor);
                if(end == std::string_view::npos)
                {
                    data = {};
                    return false;
                }
                *value = message.substr(cursor, end - cursor);
                cursor = end + 1;
            }

            const std::size_t processEnd = message.find(Protocol::kLayoutBegin, cursor);
            if(processEnd == std::string_view::npos)
            {
                data = {};
                return false;
            }
            data.processId = message.substr(cursor, processEnd - cursor);
            message.remove_prefix(processEnd + 1);
            return true;
        }

        void recoverMalformedRecordEnvelope(std::string_view &message, LayoutRecordData &data)
        {
            if(message.empty() || message.front() != Protocol::kLayoutBegin)
            {
                return;
            }
            const std::size_t payloadBegin = message.rfind(Protocol::kLayoutBegin);
            message =
                payloadBegin == 0 || payloadBegin == std::string_view::npos ? std::string_view("[truncated]") : message.substr(payloadBegin + 1);
            data = {};
        }

        std::string_view resolveLayoutValue(
            char placeholder,
            std::string_view timestamp,
            std::string_view levelText,
            std::string_view source,
            std::string_view message,
            const LayoutRecordData &data,
            char &role)
        {
            role = 0;
            switch(placeholder)
            {
            case 't':
                role = 'T';
                return timestamp;
            case 'l':
                role = 'L';
                return levelText;
            case 'a':
                role = 'A';
                return source;
            case 'm':
                return message;
            case 'F':
                return data.sourceFile;
            case 'u':
                return data.sourceFunction;
            case 'n':
                return data.sourceLine;
            case 'i':
                return data.threadId;
            case 'p':
                return data.processId;
            default:
                return {};
            }
        }

        void appendFormattedLayoutValue(
            std::string &outMessage,
            std::string_view value,
            char role,
            char name,
            char alignment,
            std::uint16_t requestedWidth,
            const std::array<char, Detail::kMaxLayoutColorScopeDepth> &activeColorRoles,
            std::size_t colorScopeDepth)
        {
            std::size_t targetWidth = value.size();
            if(alignment != 0)
            {
                targetWidth = requestedWidth == std::numeric_limits<std::uint16_t>::max()
                                  ? (name == 'a' ? std::max(value.size(), loggerState().sourceNameWidth) : 5)
                                  : requestedWidth;
                targetWidth = std::max(targetWidth, value.size());
            }

            const std::size_t padding = targetWidth - value.size();
            const std::size_t leftPadding = alignment == '>' ? padding : alignment == '^' ? padding / 2 : 0;
            const std::size_t rightPadding = padding - leftPadding;
            const auto appendText = [&]
            {
                outMessage.append(leftPadding, ' ');
                outMessage.append(value);
                outMessage.append(rightPadding, ' ');
            };

            if(role != 0 && (colorScopeDepth == 0 || activeColorRoles[colorScopeDepth - 1] != role))
            {
                outMessage.push_back(Protocol::kStyleBegin);
                outMessage.push_back(role);
                appendText();
                outMessage.push_back(Protocol::kStyleEnd);
                return;
            }
            appendText();
        }

        void renderCompiledLayout(
            std::string &outMessage,
            std::string_view layout,
            std::string_view timestamp,
            std::string_view levelText,
            std::string_view source,
            std::string_view message,
            const LayoutRecordData &data)
        {
            std::array<char, Detail::kMaxLayoutColorScopeDepth> activeColorRoles{};
            std::size_t colorScopeDepth = 0;
            outMessage.clear();

            for(std::size_t index = 0; index < layout.size(); ++index)
            {
                if(layout[index] != Detail::kCompiledLayoutMarker)
                {
                    outMessage.push_back(layout[index]);
                    continue;
                }
                if(index + 1 >= layout.size())
                {
                    break;
                }
                const char operation = layout[++index];
                if(operation == 'E')
                {
                    outMessage.push_back(Protocol::kStyleScopeEnd);
                    if(colorScopeDepth > 0)
                    {
                        --colorScopeDepth;
                    }
                    continue;
                }
                if(operation == 'S')
                {
                    if(index + 1 >= layout.size())
                    {
                        break;
                    }
                    const char role = layout[++index];
                    outMessage.push_back(Protocol::kStyleScopeBegin);
                    outMessage.push_back(role);
                    if(colorScopeDepth < activeColorRoles.size())
                    {
                        activeColorRoles[colorScopeDepth++] = role;
                    }
                    continue;
                }

                char placeholder = operation;
                char alignment = 0;
                std::uint16_t requestedWidth = 0;
                if(operation == '=')
                {
                    if(index + 4 >= layout.size())
                    {
                        break;
                    }
                    placeholder = layout[++index];
                    alignment = layout[++index];
                    const auto low = static_cast<unsigned char>(layout[++index]);
                    const auto high = static_cast<unsigned char>(layout[++index]);
                    requestedWidth = static_cast<std::uint16_t>(static_cast<std::uint32_t>(low) | (static_cast<std::uint32_t>(high) << 8u));
                }

                if(placeholder == 'f')
                {
                    outMessage.append(data.fields);
                    continue;
                }

                char role = 0;
                const std::string_view value = resolveLayoutValue(placeholder, timestamp, levelText, source, message, data, role);

                if(placeholder == 't' || placeholder == 'l' || placeholder == 'a' || placeholder == 'm' || placeholder == 'F' || placeholder == 'u' ||
                   placeholder == 'n' || placeholder == 'i' || placeholder == 'p')
                {
                    appendFormattedLayoutValue(outMessage, value, role, placeholder, alignment, requestedWidth, activeColorRoles, colorScopeDepth);
                }
            }
        }
    } // namespace

    void buildLogLine(
        std::string &outMessage,
        std::string_view timestamp,
        std::string_view levelText,
        std::string_view source,
        std::string_view message)
    {
        const std::shared_ptr<const std::string> configuredLayout = loggerState().layoutTemplate.load(std::memory_order_acquire);
        LayoutRecordData record;
        const bool hasRecordEnvelope = readRecordEnvelope(message, record);
        if(!hasRecordEnvelope)
        {
            recoverMalformedRecordEnvelope(message, record);
        }

        std::string_view selectedLayout = hasRecordEnvelope  ? record.layout
                                          : configuredLayout ? std::string_view(*configuredLayout)
                                                             : std::string_view{};
        if(selectedLayout.empty())
        {
            selectedLayout = compiledDefaultLogLayout();
        }

        renderCompiledLayout(outMessage, selectedLayout, timestamp, levelText, source, message, record);
    }
} // namespace GameWIP::Logger::Detail::Core

using namespace GameWIP::Logger::Detail::Core;

// ------------------------------------------------------------
// Formatting failure counters and scratch ownership
// ------------------------------------------------------------

void GameWIP::Logger::Detail::Core::recordAllocationFailure() noexcept
{
    if(runtimeStateRunning(loggerState().runtimeStateBits.load(std::memory_order_acquire)))
    {
        countAllocationFailure();
    }
}

void GameWIP::Logger::Detail::Core::recordFormatFailure() noexcept
{
    if(runtimeStateRunning(loggerState().runtimeStateBits.load(std::memory_order_acquire)))
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
    if(loggerState().releaseMessageMemoryAfterWriteAtomic.load(std::memory_order_acquire))
    {
        const std::size_t maxMessageLength = loggerState().maxMessageLengthAtomic.load(std::memory_order_acquire);

        // Large temporary format buffers can otherwise survive on long-lived threads
        // after the log message that needed them has already been delivered.
        if(scratch.capacity() > maxMessageLength)
        {
            std::string{}.swap(scratch);
        }
        else
        {
            scratch.clear();
        }
    }

    GameWIP::Logger::Detail::Platform::releaseFormatScratchForThread();
}
