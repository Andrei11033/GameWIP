/// @file output.h
/// @brief Public Terminal output, formatting, and control contracts.

#pragma once

#include "terminal/types.h"
#include "terminal/style.h"
#include "terminal/terminal_export.h"
#include "io/stream.h"

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace GameWIP::Terminal
{
    class Session;

    namespace Types::Input
    {
        enum class Stream;
    } // namespace Types::Input

    namespace Types::Output
    {
        struct Segment;
    } // namespace Types::Output

    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Output::Segment textSegment(std::string_view text) noexcept;
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Output::Segment styledTextSegment(
        std::string_view text,
        const Types::Style::Request &style) noexcept;
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Output::Segment byteSegment(std::span<const std::byte> bytes) noexcept;

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
                /// @brief Styling behavior.
                Types::Style::Mode styleMode = Types::Style::Mode::Auto;

                /// @brief Style to apply to this text write.
                Types::Style::Request style{};

                /// @brief Flush requested after the write.
                IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
            };

            /// @brief Options used by line output calls.
            struct LineOptions
            {
                /// @brief Styling behavior.
                Types::Style::Mode styleMode = Types::Style::Mode::Auto;

                /// @brief Style to apply to this line write.
                Types::Style::Request style{};

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
                /// @brief Styling behavior for styled segments.
                Types::Style::Mode styleMode = Types::Style::Mode::Auto;

                /// @brief Whether to append one line ending after all segments.
                bool appendLineEnding = false;

                /// @brief Line ending used when appendLineEnding is true.
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
                friend GAMEWIP_TERMINAL_EXPORT Segment GameWIP::Terminal::textSegment(std::string_view text) noexcept;
                friend GAMEWIP_TERMINAL_EXPORT Segment
                GameWIP::Terminal::styledTextSegment(std::string_view text, const Types::Style::Request &style) noexcept;
                friend GAMEWIP_TERMINAL_EXPORT Segment GameWIP::Terminal::byteSegment(std::span<const std::byte> bytes) noexcept;

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

        /// @brief Configuration retained by one persistent Terminal input session.

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

    // ------------------------------------------------------------
    // Output segment construction
    // ------------------------------------------------------------

    /// @name Output segment construction
    /// @{

    /// @brief Creates a plain text write segment.
    /// @param text Caller-owned UTF-8 text.
    /// @return A non-owning segment that refers to text.
    /// @warning The referenced text must remain alive until the segment's writeSegments() call returns.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Output::Segment textSegment(std::string_view text) noexcept;

    /// @brief Rejects temporary string storage that would leave the segment dangling.
    template <typename String>
        requires(std::same_as<std::remove_cvref_t<String>, std::string> && std::is_rvalue_reference_v<String &&>)
    [[nodiscard]] Types::Output::Segment textSegment(String &&text) noexcept = delete;

    /// @brief Creates a styled text write segment.
    /// @param text Caller-owned UTF-8 text.
    /// @param style Style copied into the segment.
    /// @return A non-owning segment that refers to text and owns a copy of style.
    /// @warning The referenced text must remain alive until the segment's writeSegments() call returns.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Output::Segment styledTextSegment(
        std::string_view text,
        const Types::Style::Request &style) noexcept;

    /// @brief Rejects temporary string storage that would leave the segment dangling.
    template <typename String>
        requires(std::same_as<std::remove_cvref_t<String>, std::string> && std::is_rvalue_reference_v<String &&>)
    [[nodiscard]] Types::Output::Segment styledTextSegment(String &&text, const Types::Style::Request &style) noexcept = delete;

    /// @brief Creates a byte write segment.
    /// @param bytes Caller-owned bytes.
    /// @return A non-owning segment that refers to bytes.
    /// @warning The referenced bytes must remain alive until the segment's writeSegments() call returns.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Output::Segment byteSegment(std::span<const std::byte> bytes) noexcept;

    /// @brief Rejects temporary contiguous storage that would leave the segment dangling.
    template <std::ranges::contiguous_range Range>
        requires(std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>, std::byte> && std::is_rvalue_reference_v<Range &&> &&
                 !std::ranges::borrowed_range<Range>)
    [[nodiscard]] Types::Output::Segment byteSegment(Range &&bytes) noexcept = delete;

    /// @}

    // ------------------------------------------------------------
    // Scoped terminal state
    // ------------------------------------------------------------

    /// @name Scoped terminal state
    /// @{

    /// @brief Movable, non-copyable RAII helper that leaves alternate screen mode when destroyed.
    /// @details Failure before the enter sequence is emitted produces an inactive scope. A flush failure after emission preserves
    /// active leave responsibility. Failed explicit leave remains active for retry. Nesting is coordinated per output stream; do
    /// not mix manual transitions with active scopes for that stream.
    class GAMEWIP_TERMINAL_EXPORT AlternateScreenScope final
    {
    public:
        /// @brief Creates an inactive alternate screen scope.
        AlternateScreenScope() noexcept;

        /// @brief Alternate-screen leave responsibility cannot be copied.
        AlternateScreenScope(const AlternateScreenScope &) = delete;
        /// @brief Alternate-screen leave responsibility cannot be copy-assigned.
        AlternateScreenScope &operator=(const AlternateScreenScope &) = delete;

        /// @brief Move-constructs the scope and transfers leave responsibility.
        AlternateScreenScope(AlternateScreenScope &&other) noexcept;

        /// @brief Leaves any destination-owned state, then transfers leave responsibility.
        /// @note If leaving the destination fails, this scope remains active and other is not consumed.
        AlternateScreenScope &operator=(AlternateScreenScope &&other) noexcept;

        /// @brief Leaves alternate screen mode on a best-effort basis without throwing.
        ~AlternateScreenScope() noexcept;

        /// @brief Returns whether this scope still owns leave responsibility.
        [[nodiscard]] bool active() const noexcept;

        /// @brief Returns the last setup or leave status tracked by this scope.
        [[nodiscard]] const IO::Types::Status &status() const noexcept;

        /// @brief Leaves alternate screen mode and reports leave failure.
        [[nodiscard]] IO::Types::Status leave() noexcept;

    private:
        friend class Session;
        friend GAMEWIP_TERMINAL_EXPORT AlternateScreenScope
        scopedAlternateScreen(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept;

        Types::Output::Stream stream_ = Types::Output::Stream::Stdout;
        Types::Output::ControlOptions options_{};
        IO::Types::Status status_{};
        bool active_ = false;
        bool restorationEmitted_ = false;
        bool restoreOnDestruction_ = true;
    };

    /// @brief Movable, non-copyable RAII helper that restores cursor visibility when destroyed.
    /// @details Failure before the hide sequence is emitted produces an inactive scope. A flush failure after emission preserves
    /// active restoration responsibility. Failed explicit restoration remains active for retry. Nesting is coordinated per output
    /// stream; do not mix manual visibility changes with active scopes for that stream.
    class GAMEWIP_TERMINAL_EXPORT CursorHiddenScope final
    {
    public:
        /// @brief Creates an inactive cursor-hidden scope.
        CursorHiddenScope() noexcept;

        /// @brief Cursor-restoration responsibility cannot be copied.
        CursorHiddenScope(const CursorHiddenScope &) = delete;
        /// @brief Cursor-restoration responsibility cannot be copy-assigned.
        CursorHiddenScope &operator=(const CursorHiddenScope &) = delete;

        /// @brief Move-constructs the scope and transfers restoration responsibility.
        CursorHiddenScope(CursorHiddenScope &&other) noexcept;

        /// @brief Restores any destination-owned state, then transfers restoration responsibility.
        /// @note If destination restoration fails, this scope remains active and other is not consumed.
        CursorHiddenScope &operator=(CursorHiddenScope &&other) noexcept;

        /// @brief Restores cursor visibility on a best-effort basis without throwing.
        ~CursorHiddenScope() noexcept;

        /// @brief Returns whether this scope still owns restoration responsibility.
        [[nodiscard]] bool active() const noexcept;

        /// @brief Returns the last setup or restoration status tracked by this scope.
        [[nodiscard]] const IO::Types::Status &status() const noexcept;

        /// @brief Restores cursor visibility and reports restoration failure.
        [[nodiscard]] IO::Types::Status restore() noexcept;

    private:
        friend class Session;
        friend GAMEWIP_TERMINAL_EXPORT CursorHiddenScope
        scopedCursorHidden(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept;

        Types::Output::Stream stream_ = Types::Output::Stream::Stdout;
        Types::Output::ControlOptions options_{};
        IO::Types::Status status_{};
        bool active_ = false;
        bool restorationEmitted_ = false;
        bool restoreOnDestruction_ = true;
    };

    /// @}

    // ------------------------------------------------------------
    // Buffered output
    // ------------------------------------------------------------

    /// @name Buffered output
    /// @{

    /// @brief Reusable caller-owned checked plain-text buffer for batching one Terminal write.
    /// @details The object owns its string storage and is not internally synchronized. Text arguments are valid UTF-8 by
    /// contract. Allocating mutation and formatting report failures through IO::Types::Status and preserve the previous
    /// complete buffer contents when an operation fails.
    class GAMEWIP_TERMINAL_EXPORT OutputBuffer final
    {
    public:
        /// @brief Creates an empty output buffer using the native line ending.
        OutputBuffer() noexcept = default;

        /// @brief Returns the line ending used by appendLine() and println().
        [[nodiscard]] Types::Output::LineEnding lineEnding() const noexcept;

        /// @brief Changes the line ending used by future line appends.
        /// @return InvalidArgument for an unknown enum value; the previous setting is preserved on failure.
        [[nodiscard]] IO::Types::Status setLineEnding(Types::Output::LineEnding lineEnding) noexcept;

        /// @brief Reserves text storage for future appends.
        /// @return OutOfMemory or SizeLimitExceeded instead of propagating allocation/length failures.
        [[nodiscard]] IO::Types::Status reserve(std::size_t bytes) noexcept;

        /// @brief Clears buffered text while retaining capacity.
        void clear() noexcept;

        /// @brief Returns whether the buffer is empty.
        [[nodiscard]] bool empty() const noexcept;

        /// @brief Returns buffered text size in bytes.
        [[nodiscard]] std::size_t size() const noexcept;

        /// @brief Returns a non-owning view of buffered text.
        /// @warning Mutating, moving, assigning, or destroying this buffer can invalidate the view.
        [[nodiscard]] std::string_view text() const noexcept;

        /// @brief Appends valid UTF-8 text.
        /// @return Checked allocation/size status. Failure preserves the previous buffer.
        [[nodiscard]] IO::Types::Status appendText(std::string_view utf8Text) noexcept;

        /// @brief Appends valid UTF-8 text followed by the configured line ending.
        /// @return Checked allocation/size status. Failure rolls back the complete line append.
        [[nodiscard]] IO::Types::Status appendLine(std::string_view utf8Text = {}) noexcept;

        /// @brief Formats text and appends it atomically to the buffer.
        /// @return InvalidArgument for formatting failure, OutOfMemory/SizeLimitExceeded for storage failure, or Unknown.
        template <class... Args> [[nodiscard]] IO::Types::Status print(std::format_string<Args...> format, Args &&...args) noexcept;

        /// @brief Formats text and appends it plus the configured line ending atomically.
        template <class... Args> [[nodiscard]] IO::Types::Status println(std::format_string<Args...> format, Args &&...args) noexcept;

        /// @brief Writes buffered text to stdout without clearing the buffer.
        [[nodiscard]] IO::Types::Status writeTo(const Types::Output::TextOptions &options = {}) const noexcept;

        /// @brief Writes buffered text to an output stream without clearing the buffer.
        [[nodiscard]] IO::Types::Status writeTo(Types::Output::Stream stream, const Types::Output::TextOptions &options = {}) const noexcept;

        /// @brief Writes buffered text to stdout and clears it only when the write succeeds.
        /// @note The name describes buffer clearing; a backend flush is requested only when options.flushMode is not None.
        [[nodiscard]] IO::Types::Status flushTo(const Types::Output::TextOptions &options = {}) noexcept;

        /// @brief Writes buffered text to an output stream and clears it only when the write succeeds.
        /// @note The name describes buffer clearing; a backend flush is requested only when options.flushMode is not None.
        [[nodiscard]] IO::Types::Status flushTo(Types::Output::Stream stream, const Types::Output::TextOptions &options = {}) noexcept;

    private:
        [[nodiscard]] IO::Types::Status vprint(std::string_view format, std::format_args arguments, bool appendLineEnding) noexcept;

        std::string text_;
        Types::Output::LineEnding lineEnding_ = Types::Output::LineEnding::Native;
    };

    /// @}

    /// @brief Observes a snapshot of currently active stdout capabilities without preparing the stream.
    /// @return Status and capabilities observed for the current stdout endpoint. Later endpoint changes can stale the snapshot.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Output::CapabilitiesResult getOutputCapabilities() noexcept;

    /// @brief Observes currently active capabilities without preparing the stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Output::CapabilitiesResult getOutputCapabilities(Types::Output::Stream stream) noexcept;

    /// @brief Enables stdout support required by styling and terminal controls.
    /// @details Preparation is idempotent. Redirected streams need no setup, detached streams report NotOpen,
    /// and styled writes or controls prepare lazily when needed.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Output::CapabilitiesResult prepareOutput() noexcept;

    /// @brief Enables stream support required by styling and terminal controls.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Output::CapabilitiesResult prepareOutput(Types::Output::Stream stream) noexcept;

    /// @brief Returns the terminal size for stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::SizeResult getTerminalSize() noexcept;

    /// @brief Returns the terminal size for an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::SizeResult getTerminalSize(Types::Output::Stream stream) noexcept;

    // ------------------------------------------------------------
    // Output
    // ------------------------------------------------------------

    /// @name Output
    /// @{

    /// @brief Writes UTF-8 text to stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status writeText(
        std::string_view utf8Text,
        const Types::Output::TextOptions &options = {}) noexcept;

    /// @brief Writes UTF-8 text to an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status writeText(
        Types::Output::Stream stream,
        std::string_view utf8Text,
        const Types::Output::TextOptions &options = {}) noexcept;

    /// @brief Writes UTF-8 text followed by a line ending to stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status writeLine(
        std::string_view utf8Text = {},
        const Types::Output::LineOptions &options = {}) noexcept;

    /// @brief Writes UTF-8 text followed by a line ending to an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status writeLine(
        Types::Output::Stream stream,
        std::string_view utf8Text = {},
        const Types::Output::LineOptions &options = {}) noexcept;

    /// @brief Writes bytes to stdout where the endpoint supports raw byte output.
    /// @return Accepted-byte count and status. A requested flush can fail after the full byte count was accepted.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::WriteResult writeBytes(
        std::span<const std::byte> bytes,
        const Types::Output::ByteOptions &options = {}) noexcept;

    /// @brief Writes bytes to an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::WriteResult writeBytes(
        Types::Output::Stream stream,
        std::span<const std::byte> bytes,
        const Types::Output::ByteOptions &options = {}) noexcept;

    /// @brief Writes text, styled text, and byte segments to stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status writeSegments(
        std::span<const Types::Output::Segment> segments,
        const Types::Output::SegmentOptions &options = {}) noexcept;

    /// @brief Writes text, styled text, and byte segments to an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status writeSegments(
        Types::Output::Stream stream,
        std::span<const Types::Output::Segment> segments,
        const Types::Output::SegmentOptions &options = {}) noexcept;

    /// @brief Formats text and writes it to stdout.
    /// @details Formatting occurs before final stdout serialization. Formatting-stage failures are converted to IO statuses.
    template <class... Args> [[nodiscard]] IO::Types::Status print(std::format_string<Args...> format, Args &&...args) noexcept;

    /// @brief Formats text and writes it to an output stream.
    template <class... Args>
    [[nodiscard]] IO::Types::Status print(Types::Output::Stream stream, std::format_string<Args...> format, Args &&...args) noexcept;

    /// @brief Formats text and writes it to stdout.
    template <class... Args>
    [[nodiscard]] IO::Types::Status print(const Types::Output::TextOptions &options, std::format_string<Args...> format, Args &&...args) noexcept;

    /// @brief Formats text and writes it to an output stream.
    template <class... Args>
    [[nodiscard]] IO::Types::Status print(
        Types::Output::Stream stream,
        const Types::Output::TextOptions &options,
        std::format_string<Args...> format,
        Args &&...args) noexcept;

    /// @brief Formats text and writes it followed by a line ending to stdout.
    /// @details Formatting occurs before final stdout serialization. Formatting-stage failures are converted to IO statuses.
    template <class... Args> [[nodiscard]] IO::Types::Status println(std::format_string<Args...> format, Args &&...args) noexcept;

    /// @brief Formats text and writes it followed by a line ending to an output stream.
    template <class... Args>
    [[nodiscard]] IO::Types::Status println(Types::Output::Stream stream, std::format_string<Args...> format, Args &&...args) noexcept;

    /// @brief Formats text and writes it followed by a line ending to stdout.
    template <class... Args>
    [[nodiscard]] IO::Types::Status println(const Types::Output::LineOptions &options, std::format_string<Args...> format, Args &&...args) noexcept;

    /// @brief Formats text and writes it followed by a line ending to an output stream.
    template <class... Args>
    [[nodiscard]] IO::Types::Status println(
        Types::Output::Stream stream,
        const Types::Output::LineOptions &options,
        std::format_string<Args...> format,
        Args &&...args) noexcept;

    /// @brief Flushes stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status flush(IO::Types::FlushMode mode = IO::Types::FlushMode::Data) noexcept;

    /// @brief Flushes an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status flush(
        Types::Output::Stream stream,
        IO::Types::FlushMode mode = IO::Types::FlushMode::Data) noexcept;

    /// @}

    // ------------------------------------------------------------
    // Terminal controls
    // ------------------------------------------------------------

    /// @name Terminal controls
    /// @{

    /// @brief Resets style on stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status resetStyle(const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Resets style on an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status resetStyle(
        Types::Output::Stream stream,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Moves the cursor on stdout.
    /// @note A zero amount emits no control sequence but can still honor options.flushMode.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status moveCursor(
        Types::Cursor::MoveDirection direction,
        std::uint32_t amount = 1,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Moves the cursor on an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status moveCursor(
        Types::Output::Stream stream,
        Types::Cursor::MoveDirection direction,
        std::uint32_t amount = 1,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Sets cursor position on stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status setCursorPosition(
        Types::Cursor::Position position,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Sets cursor position on an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status setCursorPosition(
        Types::Output::Stream stream,
        Types::Cursor::Position position,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Queries cursor position on stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Cursor::PositionResult getCursorPosition(const Types::Cursor::QueryOptions &options = {}) noexcept;

    /// @brief Queries cursor position through an output stream and reads protocol responses from an input stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Cursor::PositionResult getCursorPosition(
        Types::Output::Stream outputStream,
        Types::Input::Stream responseStream,
        const Types::Cursor::QueryOptions &options = {}) noexcept;

    /// @brief Saves cursor position on stdout.
    /// @note Saved position is backend state, not a guaranteed stack; a later save can replace it.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status saveCursorPosition(const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Saves cursor position on an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status saveCursorPosition(
        Types::Output::Stream stream,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Restores cursor position on stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status restoreCursorPosition(const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Restores cursor position on an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status restoreCursorPosition(
        Types::Output::Stream stream,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Sets cursor visibility on stdout.
    /// @warning Do not mix manual visibility changes with active CursorHiddenScope objects for stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status setCursorVisible(
        bool visible,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Sets cursor visibility on an output stream.
    /// @warning Do not mix manual visibility changes with active CursorHiddenScope objects for the same stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status setCursorVisible(
        Types::Output::Stream stream,
        bool visible,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Hides the cursor on stdout until the returned scope restores it.
    /// @warning Do not use manual visibility changes while the returned scope is active.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT CursorHiddenScope scopedCursorHidden(const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Hides the cursor on an output stream until the returned scope restores it.
    /// @warning Do not use manual visibility changes for the same stream while the returned scope is active.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT CursorHiddenScope
    scopedCursorHidden(Types::Output::Stream stream, const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Clears a screen or line region on stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status clear(
        Types::Output::ClearTarget target = Types::Output::ClearTarget::EntireScreen,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Clears a screen or line region on an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status clear(
        Types::Output::Stream stream,
        Types::Output::ClearTarget target = Types::Output::ClearTarget::EntireScreen,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Scrolls stdout.
    /// @note A zero line count emits no control sequence but can still honor options.flushMode.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status scroll(
        Types::Output::ScrollDirection direction,
        std::uint32_t lines = 1,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Scrolls an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status scroll(
        Types::Output::Stream stream,
        Types::Output::ScrollDirection direction,
        std::uint32_t lines = 1,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Enters alternate screen mode on stdout.
    /// @warning Do not mix manual transitions with active AlternateScreenScope objects for stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status enterAlternateScreen(const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Enters alternate screen mode on an output stream.
    /// @warning Do not mix manual transitions with active AlternateScreenScope objects for the same stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status enterAlternateScreen(
        Types::Output::Stream stream,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Leaves alternate screen mode on stdout.
    /// @warning Do not mix manual transitions with active AlternateScreenScope objects for stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status leaveAlternateScreen(const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Leaves alternate screen mode on an output stream.
    /// @warning Do not mix manual transitions with active AlternateScreenScope objects for the same stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status leaveAlternateScreen(
        Types::Output::Stream stream,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Enters alternate screen mode on stdout until the returned scope leaves it.
    /// @warning Do not use manual alternate-screen transitions while the returned scope is active.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT AlternateScreenScope scopedAlternateScreen(const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Enters alternate screen mode on an output stream until the returned scope leaves it.
    /// @warning Do not use manual alternate-screen transitions for the same stream while the returned scope is active.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT AlternateScreenScope
    scopedAlternateScreen(Types::Output::Stream stream, const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Sets the terminal title through stdout.
    /// @details Backend limits and sanitization apply; the current Win32 backend replaces C0 controls and DEL with spaces.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status setTitle(
        std::string_view utf8Title,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Sets the terminal title through an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status setTitle(
        Types::Output::Stream stream,
        std::string_view utf8Title,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Emits the terminal bell control through stdout.
    /// @note Terminal or user settings decide whether the control produces an audible sound.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status ringBell(const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Rings the terminal bell through an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status ringBell(
        Types::Output::Stream stream,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @}
    /// @cond INTERNAL_TERMINAL_DETAIL
    namespace Detail
    {
        /// @brief Exported ABI bridge used by the public print() templates.
        /// @warning Internal support symbol; consumers must call print() instead.
        [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status vprint(
            Types::Output::Stream stream,
            const Types::Output::TextOptions &options,
            std::string_view format,
            std::format_args arguments) noexcept;

        /// @brief Exported ABI bridge used by the public println() templates.
        /// @warning Internal support symbol; consumers must call println() instead.
        [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status vprintln(
            Types::Output::Stream stream,
            const Types::Output::LineOptions &options,
            std::string_view format,
            std::format_args arguments) noexcept;
    } // namespace Detail
    /// @endcond

    template <class... Args> IO::Types::Status OutputBuffer::print(std::format_string<Args...> format, Args &&...args) noexcept
    {
        return vprint(format.get(), std::make_format_args(args...), false);
    }

    template <class... Args> IO::Types::Status OutputBuffer::println(std::format_string<Args...> format, Args &&...args) noexcept
    {
        return vprint(format.get(), std::make_format_args(args...), true);
    }

    template <class... Args> IO::Types::Status print(std::format_string<Args...> format, Args &&...args) noexcept
    {
        return print(Types::Output::Stream::Stdout, format, std::forward<Args>(args)...);
    }

    template <class... Args> IO::Types::Status print(Types::Output::Stream stream, std::format_string<Args...> format, Args &&...args) noexcept
    {
        return print(stream, Types::Output::TextOptions{}, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    IO::Types::Status print(const Types::Output::TextOptions &options, std::format_string<Args...> format, Args &&...args) noexcept
    {
        return print(Types::Output::Stream::Stdout, options, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    IO::Types::Status print(
        Types::Output::Stream stream,
        const Types::Output::TextOptions &options,
        std::format_string<Args...> format,
        Args &&...args) noexcept
    {
        return Detail::vprint(stream, options, format.get(), std::make_format_args(args...));
    }

    template <class... Args> IO::Types::Status println(std::format_string<Args...> format, Args &&...args) noexcept
    {
        return println(Types::Output::Stream::Stdout, format, std::forward<Args>(args)...);
    }

    template <class... Args> IO::Types::Status println(Types::Output::Stream stream, std::format_string<Args...> format, Args &&...args) noexcept
    {
        return println(stream, Types::Output::LineOptions{}, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    IO::Types::Status println(const Types::Output::LineOptions &options, std::format_string<Args...> format, Args &&...args) noexcept
    {
        return println(Types::Output::Stream::Stdout, options, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    IO::Types::Status println(
        Types::Output::Stream stream,
        const Types::Output::LineOptions &options,
        std::format_string<Args...> format,
        Args &&...args) noexcept
    {
        return Detail::vprintln(stream, options, format.get(), std::make_format_args(args...));
    }
} // namespace GameWIP::Terminal
