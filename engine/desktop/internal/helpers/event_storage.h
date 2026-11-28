/// @file event_storage.h
/// @brief Shared fixed-capacity event-storage helpers.

#pragma once

#include <cstddef>
#include <span>

namespace GameWIP::Desktop::Internal
{
    /// @brief Clears queued ring slots and releases the retained storage view.
    template <typename Event> void clearRetainedEventStorage(std::span<Event> &storage, std::size_t &head, std::size_t &count) noexcept
    {
        if(!storage.empty())
        {
            for(std::size_t index = 0; index < count; ++index)
            {
                storage[(head + index) % storage.size()] = {};
            }
        }

        head = 0;
        count = 0;
        storage = {};
    }
} // namespace GameWIP::Desktop::Internal
