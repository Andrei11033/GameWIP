/// @file queue_storage.h
/// @brief Logger queue entries and slots.

#pragma once

#include "logger/internal/text_storage.h"

#include <atomic>
#include <cstddef>
#include <string_view>

namespace GameWIP::Logger::Internal::Core
{
    /// ------------------------------------------------------------
    /// Queue entries and slots
    /// ------------------------------------------------------------

    struct QueuedLogEntry
    {
        // Severity and source
        LogLevel level = LogLevel::Info;
        bool usesRegisteredSource = false;
        SourceId sourceId = 0;
        InlineLogText<kInlineSourceCapacity> sourceText;

        // Retained message
        DynamicLogText message;
    };

    struct PendingLogEntry
    {
        // Severity and source
        LogLevel level = LogLevel::Info;
        bool usesRegisteredSource = false;
        SourceId sourceId = 0;
        InlineLogText<kInlineSourceCapacity> sourceText;

        // Borrowed message for queue publication
        std::string_view message;
        bool alreadyTruncated = false;
    };

    struct QueueSlot
    {
        // Queue publication state
        std::atomic<std::size_t> sequence{0};
        bool skip = false;

        // Retained queue entry
        QueuedLogEntry entry;

        // Keep ring slots at stable addresses.
        QueueSlot() = default;
        QueueSlot(const QueueSlot &) = delete;
        QueueSlot &operator=(const QueueSlot &) = delete;
        QueueSlot(QueueSlot &&) = delete;
        QueueSlot &operator=(QueueSlot &&) = delete;
    };
} // namespace GameWIP::Logger::Internal::Core
