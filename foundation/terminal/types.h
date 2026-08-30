/// @file types.h
/// @brief Small vocabulary shared by Terminal input and output.

#pragma once

#include <cstdint>

/// @brief Platform-neutral UTF-8 standard-stream I/O, styling, and terminal control primitives.
/// @details The shared library owns process-wide stdin/stdout/stderr coordination. Checked Terminal operations translate
/// expected owned allocation, formatting, conversion, and backend failures into IO statuses/results. Caller-owned argument
/// construction that occurs before Terminal receives control remains outside that boundary.
namespace GameWIP::Terminal
{
    /// @brief Terminal stream, styling, input, and result types.
    namespace Types
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
    } // namespace Types
} // namespace GameWIP::Terminal
