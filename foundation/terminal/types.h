/// @file types.h
/// @brief Small vocabulary shared by Terminal input and output.

#pragma once

#include <cstdint>

namespace GameWIP::Terminal::Types
{
        /// @brief What kind of native stream endpoint the terminal backend detected.
        enum class StreamKind
        {
            /// @brief The stream is missing, detached, or has no valid backend handle.
            Detached,

            /// @brief The stream is attached to a real terminal/console.
            Terminal,

            /// @brief The stream is redirected to or from a pipe, file, IDE capture stream, or similar endpoint.
            Redirected,

            /// @brief The stream exists but does not fit the normal terminal/redirected categories.
            Other
        };

        /// @brief Terminal size in character cells.
        struct Size
        {
            /// @brief Width in columns.
            std::uint32_t columns = 0;

            /// @brief Height in rows.
            std::uint32_t rows = 0;
        };
} // namespace GameWIP::Terminal::Types
