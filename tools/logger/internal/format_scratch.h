/// @file format_scratch.h
/// @brief Per-thread storage for nested Logger formatting calls.

#pragma once

#include <cstddef>
#include <deque>
#include <string>

namespace GameWIP::Logger::Internal
{
    /// @brief Retains nested format buffers without invalidating active references.
    struct FormatScratchStorage
    {
        std::deque<std::string> buffers;
        std::size_t depth = 0;
    };
} // namespace GameWIP::Logger::Internal
