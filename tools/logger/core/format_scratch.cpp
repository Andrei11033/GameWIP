/// @file format_scratch.cpp
/// @brief Formatting scratch leases and formatting failure counters.

#include "logger/internal/core.h"
#include "logger/detail/formatting.h"
#include "logger/internal/format_scratch.h"

namespace GameWIP::Logger::Internal::Core
{
    /// ------------------------------------------------------------
    /// Formatting failure counters
    /// ------------------------------------------------------------

    void recordAllocationFailure() noexcept
    {
        if(runtimeStateRunning(loggerState().runtimeStateBits.load(std::memory_order_acquire)))
        {
            countAllocationFailure();
        }
    }

    void recordFormatFailure() noexcept
    {
        if(runtimeStateRunning(loggerState().runtimeStateBits.load(std::memory_order_acquire)))
        {
            countFormatFailure();
        }
    }

    /// ------------------------------------------------------------
    /// Nested formatting scratch leases
    /// ------------------------------------------------------------

    std::string &formatScratch()
    {
        FormatScratchStorage &storage = GameWIP::Logger::Platform::formatScratchStorageForThread();

        if(storage.buffers.size() == storage.depth)
        {
            storage.buffers.emplace_back();
        }

        std::string &scratch = storage.buffers[storage.depth++];
        scratch.clear();
        return scratch;
    }

    std::size_t getMaxMessageLengthForFormatting()
    {
        return loggerState().maxMessageLengthAtomic.load(std::memory_order_acquire);
    }

    FormatPolicy getFormatPolicyForFormatting()
    {
        return formatPolicyFromValue(loggerState().formatPolicyAtomic.load(std::memory_order_acquire));
    }

    void releaseFormatScratchIfNeeded(std::string &scratch) noexcept
    {
        if(loggerState().releaseMessageMemoryAfterWriteAtomic.load(std::memory_order_acquire))
        {
            const std::size_t maxMessageLength = loggerState().maxMessageLengthAtomic.load(std::memory_order_acquire);

            // Release oversized buffers after their message has been delivered.
            if(scratch.capacity() > maxMessageLength)
            {
                std::string{}.swap(scratch);
            }
            else
            {
                scratch.clear();
            }
        }

        FormatScratchStorage *storage = GameWIP::Logger::Platform::findFormatScratchStorageForThread();
        if(storage == nullptr || storage->depth == 0)
        {
            return;
        }
        --storage->depth;

        // Keep the outer buffer warm and discard buffers used only for nested calls.
        if(storage->depth > 0 && storage->depth + 1 == storage->buffers.size())
        {
            storage->buffers.pop_back();
        }
    }
} // namespace GameWIP::Logger::Internal::Core
