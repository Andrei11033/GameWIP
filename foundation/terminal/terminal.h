/// @file
/// @brief Planned public header for the GameWIP Terminal library.
///
/// Contract stub only. GameWIP::Terminal is not implemented yet.

#pragma once

namespace GameWIP::Terminal
{

    /// @brief Passive Terminal data shapes planned for the future implementation.
    namespace Types
    {

        enum class Stream;
        enum class ColorMode;
        enum class Color;

        struct TextStyle;
        struct Capabilities;
        struct CapabilityResult;
        struct WriteOptions;

    } // namespace Types

    class StreamWriter;

    // Planned API only:
    // - getCapabilities(Stream)
    // - writeBytes(Stream, ...)
    // - writeText(Stream, ...)
    // - writeLine(Stream, ...)
    // - writeStyled(Stream, ...)
    // - writeStyledLine(Stream, ...)
    // - flush(Stream)

} // namespace GameWIP::Terminal
