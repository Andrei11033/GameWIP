/// @file logger_format.cpp
/// @brief Logger formatting helpers and public bridge functions used by header templates.

#include "logger/internal/logger_core.h"

namespace GameWIP::Logger::Detail::Core
{
    //-------------------------------------------------------------------------------------------------
    // Formatting and sink helpers
    //-------------------------------------------------------------------------------------------------

    /// @brief Formats local time through the platform bridge and returns a fallback on failure.
    /// @param time Time value to format.
    /// @param timeFormat strftime-compatible format string.
    /// @param outError Receives platform error details.
    /// @return Formatted time text, or "invalid-time" on platform failure.
    std::string formatTimeOrFallback(std::time_t time, std::string_view timeFormat, PlatformError &outError)
    {
        std::string text;
        outError = GameWIP::Logger::Detail::Platform::formatLocalTime(time, timeFormat, text);
        if (hasPlatformError(outError))
        {
            return "invalid-time";
        }

        return text;
    }

    /// @brief Formats the current local time.
    /// @param timeFormat strftime-compatible format string.
    /// @param outError Receives platform error details.
    /// @return Formatted current local time, or "invalid-time" on platform failure.
    std::string getCurrentTimeText(std::string_view timeFormat, PlatformError &outError)
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        return formatTimeOrFallback(time, timeFormat, outError);
    }

    /// @brief Returns a cached per-second timestamp, refreshing the cache when needed.
    /// @param cache Timestamp cache to read/update.
    /// @return View into cache.text.
    std::string_view getTimestampText(TimestampCache &cache)
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t currentSecond = std::chrono::system_clock::to_time_t(now);

        if (!cache.valid || cache.second != currentSecond)
        {
            PlatformError platformError;
            cache.text = formatTimeOrFallback(currentSecond, "%H:%M:%S", platformError);
            if (hasPlatformError(platformError))
            {
                recordResult(LoggerResult::PlatformCallFailed, platformError);
            }

            cache.second = currentSecond;
            cache.valid = true;
        }

        return cache.text;
    }

    /// @brief Returns a copy of the platform debug output timestamp cache text.
    /// @return Timestamp string safe to use after releasing debugTimestampMutex.
    std::string getDebugTimestampText()
    {
        std::lock_guard<std::mutex> lock(loggerState().debugTimestampMutex);
        return std::string(getTimestampText(loggerState().debugTimestampCache));
    }

    /// @brief Resolves a SourceId into display text.
    /// @param source SourceId to resolve.
    /// @param outUnknownSource Receives true when the source was not registered.
    /// @return Registered source name, or "UnknownSource".
    std::string_view findSourceName(const SourceRegistry *registry, SourceId source, bool &outUnknownSource)
    {
        outUnknownSource = false;
        if (registry)
        {
            const RegisteredSource *registeredSource = findSource(*registry, source);
            if (registeredSource != nullptr)
            {
                return registeredSource->name;
            }
        }

        outUnknownSource = true;
        return "UnknownSource";
    }

    /// @brief Resolves the source text for a queued entry.
    /// @param entry Queued entry to inspect.
    /// @param outUnknownSource Receives true when a registered source entry used an unknown SourceId.
    /// @return Source text to write in the log line.
    std::string_view resolveSourceText(const QueuedLogEntry &entry, const SourceRegistry *registry, bool &outUnknownSource)
    {
        if (!entry.usesRegisteredSource)
        {
            outUnknownSource = false;
            return entry.sourceText.view();
        }

        return findSourceName(registry, entry.sourceId, outUnknownSource);
    }

    /// @brief Increments the unknown source counter after a message is preserved and written.
    void recordUnknownSourceUse()
    {
        loggerState().stats.unknownSourceUses.fetch_add(1, std::memory_order_relaxed);
    }

    /// @brief Stores message text in an entry, truncating with a suffix when needed.
    /// @param entry Entry to mutate.
    /// @param message Message text to copy.
    /// @param maxMessageLength Maximum stored message length.
    /// @param outTruncated Receives true when truncation occurred.
    void assignMessage(QueuedLogEntry &entry, std::string_view message, std::size_t maxMessageLength, bool &outTruncated)
    {
        outTruncated = message.size() > maxMessageLength;
        if (!outTruncated)
        {
            entry.message.assign(message);
            return;
        }

        constexpr std::string_view suffix = "... [truncated]";
        if (maxMessageLength <= suffix.size())
        {
            entry.message.assign(suffix.substr(0, maxMessageLength));
            return;
        }

        entry.message.assignJoined(message.substr(0, maxMessageLength - suffix.size()), suffix);
    }

    /// @brief Builds a truncated message copy using the same suffix as queued entries.
    /// @param outMessage Receives the truncated text.
    /// @param message Source message text.
    /// @param maxMessageLength Maximum stored/debugged message length.
    void buildTruncatedMessage(std::string &outMessage, std::string_view message, std::size_t maxMessageLength)
    {
        constexpr std::string_view suffix = "... [truncated]";
        outMessage.clear();
        if (maxMessageLength == 0)
        {
            return;
        }
        if (maxMessageLength <= suffix.size())
        {
            outMessage.assign(suffix.substr(0, maxMessageLength));
            return;
        }

        outMessage.reserve(maxMessageLength);
        outMessage.append(message.substr(0, maxMessageLength - suffix.size()));
        outMessage.append(suffix);
    }

    /// @brief Returns a message view bounded by the active maximum message length.
    /// @param message Original message.
    /// @param alreadyTruncated True when the caller already applied Logger truncation.
    /// @param scratch Scratch storage used only when truncation is needed.
    /// @param outTruncated Receives true when this call truncated message.
    /// @return Either message or a view into scratch.
    std::string_view boundedMessageView(std::string_view message, bool alreadyTruncated, std::string &scratch, bool &outTruncated)
    {
        outTruncated = false;
        if (alreadyTruncated)
        {
            return message;
        }

        const std::size_t maxMessageLength = loggerState().maxMessageLengthAtomic.load(std::memory_order_acquire);
        if (message.size() <= maxMessageLength)
        {
            return message;
        }

        buildTruncatedMessage(scratch, message, maxMessageLength);
        outTruncated = true;
        return scratch;
    }

    /// @brief Builds the final [time][level][source]: message line without std::format.
    /// @param outMessage Reusable output scratch string.
    /// @param timestamp Timestamp text.
    /// @param levelText Level label.
    /// @param source Source text.
    /// @param message Message text.
    void buildLogLine(
        std::string &outMessage,
        std::string_view timestamp,
        std::string_view levelText,
        std::string_view source,
        std::string_view message)
    {
        constexpr std::size_t fixedFormatLength = 8;
        const std::size_t requiredCapacity = timestamp.size() + levelText.size() + source.size() + message.size() + fixedFormatLength;

        outMessage.clear();
        if (outMessage.capacity() < requiredCapacity)
        {
            outMessage.reserve(requiredCapacity);
        }

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

//-------------------------------------------------------------------------------------------------
// Private Logger bridge accounting helpers
//-------------------------------------------------------------------------------------------------

/// @brief Counts an allocation/internal-format failure from public template catch paths.
void GameWIP::Logger::Detail::Core::recordAllocationFailure()
{
    if ((loggerState().runtimeStateBits.load(std::memory_order_acquire) & kRuntimeStateRunningBit) != 0)
    {
        countAllocationFailure();
    }
}

/// @brief Counts an invalid runtime format failure from public template catch paths.
void GameWIP::Logger::Detail::Core::recordFormatFailure()
{
    if ((loggerState().runtimeStateBits.load(std::memory_order_acquire) & kRuntimeStateRunningBit) != 0)
    {
        countFormatFailure();
    }
}

/// @brief Returns reusable per-thread format storage for header-only formatting overloads.
/// @return Per-thread scratch string.
std::string &GameWIP::Logger::Detail::Core::formatScratch()
{
    return GameWIP::Logger::Detail::Platform::formatScratchForThread();
}

/// @brief Returns the active maximum message length for header-only bounded formatting.
/// @return Current max message length.
std::size_t GameWIP::Logger::Detail::Core::getMaxMessageLengthForFormatting()
{
    return loggerState().maxMessageLengthAtomic.load(std::memory_order_acquire);
}

/// @brief Returns the active formatted-message memory/speed policy.
/// @return Current policy used by header-only formatting overloads.
FormatPolicy GameWIP::Logger::Detail::Core::getFormatPolicyForFormatting()
{
    return formatPolicyFromValue(loggerState().formatPolicyAtomic.load(std::memory_order_acquire));
}

/// @brief Releases thread-local formatting scratch capacity when the active config requests it.
/// @param scratch Scratch string to optionally shrink.
void GameWIP::Logger::Detail::Core::releaseFormatScratchIfNeeded(std::string &scratch)
{
    if (!loggerState().releaseMessageMemoryAfterWriteAtomic.load(std::memory_order_acquire))
    {
        return;
    }

    const std::size_t maxMessageLength = loggerState().maxMessageLengthAtomic.load(std::memory_order_acquire);
    if (scratch.capacity() > maxMessageLength)
    {
        std::string{}.swap(scratch);
        return;
    }

    scratch.clear();
}
