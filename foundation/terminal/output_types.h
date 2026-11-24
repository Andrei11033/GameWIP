/// @file output_types.h
/// @brief Public Terminal output types and segment factories.

#pragma once

#include "io/stream.h"
#include "terminal/style.h"
#include "terminal/terminal_export.h"
#include "terminal/types.h"

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

namespace GameWIP::Terminal
{
    namespace Types::Output
    {
        struct Segment;
    } // namespace Types::Output

    /// @name Output segment construction
    /// @{

    /// @brief Creates a plain text write segment.
    /// @param text Caller-owned UTF-8 text.
    /// @return A non-owning segment that refers to text.
    /// @warning The referenced text must remain alive until the segment's writeSegments() call returns.
    [[nodiscard]] TERMINAL_EXPORT Types::Output::Segment textSegment(std::string_view text) noexcept;

    /// @brief Rejects temporary string storage that would leave the segment dangling.
    template <typename String>
        requires(std::same_as<std::remove_cvref_t<String>, std::string> && std::is_rvalue_reference_v<String &&>)
    [[nodiscard]] Types::Output::Segment textSegment(String &&text) noexcept = delete;

    /// @brief Creates a styled text write segment.
    /// @param text Caller-owned UTF-8 text.
    /// @param style Style copied into the segment.
    /// @return A non-owning segment that refers to text and owns a copy of style.
    /// @warning The referenced text must remain alive until the segment's writeSegments() call returns.
    [[nodiscard]] TERMINAL_EXPORT Types::Output::Segment styledTextSegment(std::string_view text, const Types::Style::Request &style) noexcept;

    /// @brief Rejects temporary string storage that would leave the segment dangling.
    template <typename String>
        requires(std::same_as<std::remove_cvref_t<String>, std::string> && std::is_rvalue_reference_v<String &&>)
    [[nodiscard]] Types::Output::Segment styledTextSegment(String &&text, const Types::Style::Request &style) noexcept = delete;

    /// @brief Creates a byte write segment.
    /// @param bytes Caller-owned bytes.
    /// @return A non-owning segment that refers to bytes.
    /// @warning The referenced bytes must remain alive until the segment's writeSegments() call returns.
    [[nodiscard]] TERMINAL_EXPORT Types::Output::Segment byteSegment(std::span<const std::byte> bytes) noexcept;

    /// @brief Rejects temporary contiguous storage that would leave the segment dangling.
    template <std::ranges::contiguous_range Range>
        requires(std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>, std::byte> && std::is_rvalue_reference_v<Range &&> &&
                 !std::ranges::borrowed_range<Range>)
    [[nodiscard]] Types::Output::Segment byteSegment(Range &&bytes) noexcept = delete;

    /// @}

    /// @brief Default timeout used by best-effort terminal control queries.
    inline constexpr std::chrono::milliseconds kDefaultQueryTimeout{100};

    namespace Types
    {
        namespace Output
        {
            /// @brief Standard terminal output streams.
            enum class Stream
            {
                /// @brief Process standard output.
                Stdout,

                /// @brief Process standard error.
                Stderr
            };

            /// @brief Line ending appended by line output helpers.
            enum class LineEnding
            {
                /// @brief Platform-native line ending.
                Native,

                /// @brief Line feed.
                Lf,

                /// @brief Carriage return followed by line feed.
                CrLf
            };

            /// @brief Capabilities of a standard terminal output stream.
            struct Capabilities
            {
                // Endpoint and data lanes
                /// @brief Detected stream endpoint kind.
                StreamKind kind = StreamKind::Detached;

                /// @brief True when UTF-8 text output is supported.
                bool supportsUtf8Text = false;

                /// @brief True when byte output is supported.
                bool supportsByteOutput = false;

                /// @brief True when flushing is supported or meaningful.
                bool supportsFlush = false;

                /// @brief Supported style features.
                Types::Style::Capabilities style{};

                // Cursor and screen control
                /// @brief True when terminal size can be queried.
                bool supportsTerminalSize = false;

                /// @brief True when relative or absolute cursor movement is supported.
                bool supportsCursorMovement = false;

                /// @brief True when cursor position querying is supported.
                bool supportsCursorPositionQuery = false;

                /// @brief True when saving and restoring cursor position is supported.
                bool supportsCursorSaveRestore = false;

                /// @brief True when cursor visibility can be changed.
                bool supportsCursorVisibility = false;

                /// @brief True when screen or line clearing is supported.
                bool supportsClear = false;

                /// @brief True when terminal scrolling is supported.
                bool supportsScroll = false;

                /// @brief True when alternate screen mode is supported.
                bool supportsAlternateScreen = false;

                // Terminal presentation signals
                /// @brief True when terminal title changes are supported.
                bool supportsTitle = false;

                /// @brief True when terminal bell output is supported.
                bool supportsBell = false;
            };

            /// @brief Result returned by output capability queries.
            struct CapabilitiesResult
            {
                /// @brief Operation status.
                IO::Types::Status status;

                /// @brief Reported output capabilities.
                Types::Output::Capabilities capabilities;
            };

            /// @brief Screen or line region targeted by clear().
            enum class ClearTarget
            {
                /// @brief Clear the visible terminal screen.
                EntireScreen,

                /// @brief Clear from the cursor to the beginning of the screen.
                ScreenBeforeCursor,

                /// @brief Clear from the cursor to the end of the screen.
                ScreenAfterCursor,

                /// @brief Clear the visible terminal screen and scrollback when supported.
                EntireScreenAndScrollback,

                /// @brief Clear the current terminal line.
                EntireLine,

                /// @brief Clear from the cursor to the beginning of the current line.
                LineBeforeCursor,

                /// @brief Clear from the cursor to the end of the current line.
                LineAfterCursor
            };

            /// @brief Terminal scroll direction.
            enum class ScrollDirection
            {
                /// @brief Scroll terminal contents upward.
                Up,
                /// @brief Scroll terminal contents downward.
                Down
            };

            /// @brief Options used by text output calls.
            struct TextOptions
            {
                // Text styling
                /// @brief Styling behavior.
                Types::Style::Mode styleMode = Types::Style::Mode::Auto;

                /// @brief Style to apply to this text write.
                Types::Style::Request style{};

                // Write completion
                /// @brief Flush requested after the write.
                IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
            };

            /// @brief Options used by line output calls.
            struct LineOptions
            {
                // Line styling
                /// @brief Styling behavior.
                Types::Style::Mode styleMode = Types::Style::Mode::Auto;

                /// @brief Style to apply to this line write.
                Types::Style::Request style{};

                // Line framing and completion
                /// @brief Line ending appended after the text.
                Types::Output::LineEnding lineEnding = Types::Output::LineEnding::Native;

                /// @brief Flush requested after the write.
                IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
            };

            /// @brief Options used by byte output calls.
            struct ByteOptions
            {
                /// @brief Flush requested after the write.
                IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
            };

            /// @brief Options used by segmented output calls.
            struct SegmentOptions
            {
                // Segment styling
                /// @brief Styling behavior for styled segments.
                Types::Style::Mode styleMode = Types::Style::Mode::Auto;

                /// @brief Whether to append one line ending after all segments.
                bool appendLineEnding = false;

                // Record framing and completion
                /// @brief Line ending used when appending.
                Types::Output::LineEnding lineEnding = Types::Output::LineEnding::Native;

                /// @brief Flush requested after the full segment batch.
                IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
            };

            /// @brief Options used by terminal control calls.
            struct ControlOptions
            {
                /// @brief Flush requested after the control operation.
                IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
            };

            /// @brief Kind of write segment.
            enum class SegmentKind
            {
                /// @brief Plain UTF-8 text.
                Text,

                /// @brief UTF-8 text with style.
                StyledText,

                /// @brief Raw bytes.
                Bytes
            };

            /// @brief One piece of optimized batched terminal output.
            /// @details Text and byte payloads are non-owning views. Copying a segment copies those views rather than the
            /// referenced storage. Caller-owned payload storage must remain valid and unchanged until writeSegments() returns.
            /// Types::Style::Request is copied into styled segments.
            struct Segment
            {
                /// @brief Returns the segment kind.
                [[nodiscard]] Types::Output::SegmentKind kind() const noexcept
                {
                    return kind_;
                }

                /// @brief Returns text stored by Text and StyledText segments.
                /// @note Meaningful only when kind() is Text or StyledText. The returned view has the payload's caller-owned lifetime.
                [[nodiscard]] std::string_view text() const noexcept
                {
                    return text_;
                }

                /// @brief Returns bytes stored by Bytes segments.
                /// @note Meaningful only when kind() is Bytes. The returned span has the payload's caller-owned lifetime.
                [[nodiscard]] std::span<const std::byte> bytes() const noexcept
                {
                    return bytes_;
                }

                /// @brief Returns the style stored by StyledText segments.
                /// @note Meaningful only when kind() is StyledText. The returned reference remains valid for this segment's lifetime.
                [[nodiscard]] const Types::Style::Request &style() const noexcept
                {
                    return style_;
                }

            private:
                friend TERMINAL_EXPORT Segment GameWIP::Terminal::textSegment(std::string_view text) noexcept;
                friend TERMINAL_EXPORT Segment
                GameWIP::Terminal::styledTextSegment(std::string_view text, const Types::Style::Request &style) noexcept;
                friend TERMINAL_EXPORT Segment GameWIP::Terminal::byteSegment(std::span<const std::byte> bytes) noexcept;

                Segment(
                    Types::Output::SegmentKind kind,
                    std::string_view text,
                    std::span<const std::byte> bytes,
                    const Types::Style::Request &style) noexcept;

                Types::Output::SegmentKind kind_ = Types::Output::SegmentKind::Text;
                std::string_view text_;
                std::span<const std::byte> bytes_;
                Types::Style::Request style_{};
            };
        } // namespace Output

        /// @brief Result returned by terminal size queries.
        struct SizeResult
        {
            /// @brief Operation status.
            IO::Types::Status status;

            /// @brief Reported terminal size.
            Types::Size size;
        };

        namespace Cursor
        {
            /// @brief Zero-based cursor position.
            struct Position
            {
                /// @brief Zero-based column.
                std::uint32_t column = 0;

                /// @brief Zero-based row.
                std::uint32_t row = 0;
            };

            /// @brief Result returned by cursor position queries.
            struct PositionResult
            {
                /// @brief Operation status.
                IO::Types::Status status;

                /// @brief Reported cursor position.
                Types::Cursor::Position position;
            };

            /// @brief Relative cursor movement direction.
            enum class MoveDirection
            {
                /// @brief Move toward smaller row coordinates.
                Up,
                /// @brief Move toward larger row coordinates.
                Down,
                /// @brief Move toward smaller column coordinates.
                Left,
                /// @brief Move toward larger column coordinates.
                Right
            };

            /// @brief Options used by cursor position queries.
            struct QueryOptions
            {
                /// @brief Maximum time to wait for a terminal cursor-position response.
                /// @note A backend may answer directly without waiting for an input-stream response.
                std::chrono::milliseconds timeout = kDefaultQueryTimeout;

                /// @brief Flush requested after sending the cursor-position query.
                /// @note Cursor position queries may consume a terminal response from the corresponding input stream.
                IO::Types::FlushMode flushMode = IO::Types::FlushMode::Data;
            };
        } // namespace Cursor
    } // namespace Types

} // namespace GameWIP::Terminal
