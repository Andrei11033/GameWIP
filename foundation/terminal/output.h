/// @file output.h
/// @brief Public Terminal output operations and buffers.

#pragma once

#include "io/stream.h"
#include "terminal/output_types.h"
#include "terminal/terminal_export.h"
#include "status/result.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace GameWIP::Terminal
{
    class Session;

    namespace Types::Input
    {
        enum class Stream;
    } // namespace Types::Input

    /// @name Scoped terminal state
    /// @{

    /// @brief Movable, non-copyable RAII helper that leaves alternate screen mode when destroyed.
    /// @details Failure before the enter sequence is emitted produces an inactive scope. A flush failure after emission preserves
    /// active leave responsibility. Failed explicit leave remains active for retry. Nesting is coordinated per output stream; do
    /// not mix manual transitions with active scopes for that stream.
    class TERMINAL_EXPORT AlternateScreenScope final
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
        [[nodiscard]] const GameWIP::Status::Types::Status &status() const noexcept;

        /// @brief Leaves alternate screen mode and reports leave failure.
        [[nodiscard]] GameWIP::Status::Types::Result<void> leave() noexcept;

    private:
        friend class Session;
        friend TERMINAL_EXPORT AlternateScreenScope
        scopedAlternateScreen(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept;

        Types::Output::Stream stream_ = Types::Output::Stream::Stdout;
        Types::Output::ControlOptions options_{};
        GameWIP::Status::Types::Status status_{};
        bool active_ = false;
        bool restorationEmitted_ = false;
        bool restoreOnDestruction_ = true;
    };

    /// @brief Movable, non-copyable RAII helper that restores cursor visibility when destroyed.
    /// @details Failure before the hide sequence is emitted produces an inactive scope. A flush failure after emission preserves
    /// active restoration responsibility. Failed explicit restoration remains active for retry. Nesting is coordinated per output
    /// stream; do not mix manual visibility changes with active scopes for that stream.
    class TERMINAL_EXPORT CursorHiddenScope final
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
        [[nodiscard]] const GameWIP::Status::Types::Status &status() const noexcept;

        /// @brief Restores cursor visibility and reports restoration failure.
        [[nodiscard]] GameWIP::Status::Types::Result<void> restore() noexcept;

    private:
        friend class Session;
        friend TERMINAL_EXPORT CursorHiddenScope
        scopedCursorHidden(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept;

        Types::Output::Stream stream_ = Types::Output::Stream::Stdout;
        Types::Output::ControlOptions options_{};
        GameWIP::Status::Types::Status status_{};
        bool active_ = false;
        bool restorationEmitted_ = false;
        bool restoreOnDestruction_ = true;
    };

    /// @}

    /// @name Buffered output
    /// @{

    /// @brief Reusable caller-owned checked plain-text buffer for batching one Terminal write.
    /// @details Owns its storage and is not internally synchronized. Appends reject invalid UTF-8 with InvalidArgument.
    /// A failed operation leaves the previous contents unchanged.
    class TERMINAL_EXPORT OutputBuffer final
    {
    public:
        /// @brief Creates an empty output buffer using the native line ending.
        OutputBuffer() noexcept = default;

        /// @brief Returns the line ending used by appendLine() and println().
        [[nodiscard]] Types::Output::LineEnding lineEnding() const noexcept;

        /// @brief Changes the line ending used by future line appends.
        /// @return InvalidArgument for an unknown enum value; the previous setting is preserved on failure.
        [[nodiscard]] GameWIP::Status::Types::Result<void> setLineEnding(Types::Output::LineEnding lineEnding) noexcept;

        /// @brief Reserves text storage for future appends.
        /// @return OutOfMemory or SizeLimitExceeded instead of propagating allocation/length failures.
        [[nodiscard]] GameWIP::Status::Types::Result<void> reserve(std::size_t bytes) noexcept;

        /// @brief Clears buffered text while retaining capacity.
        void clear() noexcept;

        /// @brief Returns whether the buffer is empty.
        [[nodiscard]] bool empty() const noexcept;

        /// @brief Returns buffered text size in bytes.
        [[nodiscard]] std::size_t size() const noexcept;

        /// @brief Returns a non-owning view of buffered text.
        /// @warning Mutating, moving, assigning, or destroying this buffer can invalidate the view.
        [[nodiscard]] std::string_view text() const noexcept;

        /// @brief Appends UTF-8 text.
        /// @return InvalidArgument for invalid UTF-8, or an allocation/size status. Failure preserves the buffer.
        [[nodiscard]] GameWIP::Status::Types::Result<void> appendText(std::string_view utf8Text) noexcept;

        /// @brief Appends UTF-8 text followed by the configured line ending.
        /// @return As appendText(); failure rolls back the whole line.
        [[nodiscard]] GameWIP::Status::Types::Result<void> appendLine(std::string_view utf8Text = {}) noexcept;

        /// @brief Formats text and appends it atomically to the buffer.
        /// @return InvalidArgument for formatting failure, OutOfMemory/SizeLimitExceeded for storage failure, or Unknown.
        template <class... Args>
        [[nodiscard]] GameWIP::Status::Types::Result<void> print(std::format_string<Args...> format, Args &&...args) noexcept;

        /// @brief Formats text and appends it plus the configured line ending atomically.
        template <class... Args>
        [[nodiscard]] GameWIP::Status::Types::Result<void> println(std::format_string<Args...> format, Args &&...args) noexcept;

        /// @brief Writes buffered text to stdout without clearing the buffer.
        [[nodiscard]] GameWIP::Status::Types::Result<void> writeTo(const Types::Output::TextOptions &options = {}) const noexcept;

        /// @brief Writes buffered text to an output stream without clearing the buffer.
        [[nodiscard]] GameWIP::Status::Types::Result<void> writeTo(Types::Output::Stream stream, const Types::Output::TextOptions &options = {})
            const noexcept;

        /// @brief Writes buffered text to stdout and clears it only when the write succeeds.
        /// @note The name describes buffer clearing; a backend flush is requested only when options.flushMode is not None.
        [[nodiscard]] GameWIP::Status::Types::Result<void> flushTo(const Types::Output::TextOptions &options = {}) noexcept;

        /// @brief Writes buffered text to an output stream and clears it only when the write succeeds.
        /// @note The name describes buffer clearing; a backend flush is requested only when options.flushMode is not None.
        [[nodiscard]] GameWIP::Status::Types::Result<void> flushTo(
            Types::Output::Stream stream,
            const Types::Output::TextOptions &options = {}) noexcept;

    private:
        [[nodiscard]] GameWIP::Status::Types::Result<void> vprint(
            std::string_view format,
            std::format_args arguments,
            bool appendLineEnding) noexcept;

        std::string text_;
        Types::Output::LineEnding lineEnding_ = Types::Output::LineEnding::Native;
    };

    /// @}

    /// @brief Observes a snapshot of active stdout capabilities without preparing the stream.
    /// @return Status and capabilities observed for the current stdout endpoint. Later endpoint changes can stale the snapshot.
    [[nodiscard]] TERMINAL_EXPORT Types::Output::CapabilitiesResult getOutputCapabilities() noexcept;

    /// @brief Observes active capabilities without preparing the stream.
    [[nodiscard]] TERMINAL_EXPORT Types::Output::CapabilitiesResult getOutputCapabilities(Types::Output::Stream stream) noexcept;

    /// @brief Enables stdout support required by styling and terminal controls.
    /// @details Preparation is idempotent. Redirected streams need no setup, detached streams report NotOpen,
    /// and styled writes or controls prepare lazily when needed.
    [[nodiscard]] TERMINAL_EXPORT Types::Output::CapabilitiesResult prepareOutput() noexcept;

    /// @brief Enables stream support required by styling and terminal controls.
    [[nodiscard]] TERMINAL_EXPORT Types::Output::CapabilitiesResult prepareOutput(Types::Output::Stream stream) noexcept;

    /// @brief Returns the terminal size for stdout.
    [[nodiscard]] TERMINAL_EXPORT Types::SizeResult getTerminalSize() noexcept;

    /// @brief Returns the terminal size for an output stream.
    [[nodiscard]] TERMINAL_EXPORT Types::SizeResult getTerminalSize(Types::Output::Stream stream) noexcept;

    /// @name Output
    /// @{

    /// @brief Writes UTF-8 text to stdout.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> writeText(
        std::string_view utf8Text,
        const Types::Output::TextOptions &options = {}) noexcept;

    /// @brief Writes UTF-8 text to an output stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> writeText(
        Types::Output::Stream stream,
        std::string_view utf8Text,
        const Types::Output::TextOptions &options = {}) noexcept;

    /// @brief Writes UTF-8 text followed by a line ending to stdout.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> writeLine(
        std::string_view utf8Text = {},
        const Types::Output::LineOptions &options = {}) noexcept;

    /// @brief Writes UTF-8 text followed by a line ending to an output stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> writeLine(
        Types::Output::Stream stream,
        std::string_view utf8Text = {},
        const Types::Output::LineOptions &options = {}) noexcept;

    /// @brief Writes bytes to stdout where the endpoint supports raw byte output.
    /// @return Accepted-byte count and status. A requested flush can fail after the full byte count was accepted.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<IO::Types::WriteProgress> writeBytes(
        std::span<const std::byte> bytes,
        const Types::Output::ByteOptions &options = {}) noexcept;

    /// @brief Writes bytes to an output stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<IO::Types::WriteProgress> writeBytes(
        Types::Output::Stream stream,
        std::span<const std::byte> bytes,
        const Types::Output::ByteOptions &options = {}) noexcept;

    /// @brief Writes text, styled text, and byte segments to stdout.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> writeSegments(
        std::span<const Types::Output::Segment> segments,
        const Types::Output::SegmentOptions &options = {}) noexcept;

    /// @brief Writes text, styled text, and byte segments to an output stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> writeSegments(
        Types::Output::Stream stream,
        std::span<const Types::Output::Segment> segments,
        const Types::Output::SegmentOptions &options = {}) noexcept;

    /// @brief Formats text and writes it to stdout.
    /// @details Formatting occurs before final stdout serialization. Formatting-stage failures use shared status results.
    template <class... Args> [[nodiscard]] GameWIP::Status::Types::Result<void> print(std::format_string<Args...> format, Args &&...args) noexcept;

    /// @brief Formats text and writes it to an output stream.
    template <class... Args>
    [[nodiscard]] GameWIP::Status::Types::Result<void> print(
        Types::Output::Stream stream,
        std::format_string<Args...> format,
        Args &&...args) noexcept;

    /// @brief Formats text and writes it to stdout.
    template <class... Args>
    [[nodiscard]] GameWIP::Status::Types::Result<void> print(
        const Types::Output::TextOptions &options,
        std::format_string<Args...> format,
        Args &&...args) noexcept;

    /// @brief Formats text and writes it to an output stream.
    template <class... Args>
    [[nodiscard]] GameWIP::Status::Types::Result<void> print(
        Types::Output::Stream stream,
        const Types::Output::TextOptions &options,
        std::format_string<Args...> format,
        Args &&...args) noexcept;

    /// @brief Formats text and writes it followed by a line ending to stdout.
    /// @details Formatting occurs before final stdout serialization. Formatting-stage failures use shared status results.
    template <class... Args> [[nodiscard]] GameWIP::Status::Types::Result<void> println(std::format_string<Args...> format, Args &&...args) noexcept;

    /// @brief Formats text and writes it followed by a line ending to an output stream.
    template <class... Args>
    [[nodiscard]] GameWIP::Status::Types::Result<void> println(
        Types::Output::Stream stream,
        std::format_string<Args...> format,
        Args &&...args) noexcept;

    /// @brief Formats text and writes it followed by a line ending to stdout.
    template <class... Args>
    [[nodiscard]] GameWIP::Status::Types::Result<void> println(
        const Types::Output::LineOptions &options,
        std::format_string<Args...> format,
        Args &&...args) noexcept;

    /// @brief Formats text and writes it followed by a line ending to an output stream.
    template <class... Args>
    [[nodiscard]] GameWIP::Status::Types::Result<void> println(
        Types::Output::Stream stream,
        const Types::Output::LineOptions &options,
        std::format_string<Args...> format,
        Args &&...args) noexcept;

    /// @brief Flushes stdout.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> flush(IO::Types::FlushMode mode = IO::Types::FlushMode::Data) noexcept;

    /// @brief Flushes an output stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> flush(
        Types::Output::Stream stream,
        IO::Types::FlushMode mode = IO::Types::FlushMode::Data) noexcept;

    /// @}

    /// @name Terminal controls
    /// @{

    /// @brief Resets style on stdout.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> resetStyle(const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Resets style on an output stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> resetStyle(
        Types::Output::Stream stream,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Moves the cursor on stdout.
    /// @note A zero amount emits no control sequence but can still honor options.flushMode.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> moveCursor(
        Types::Cursor::MoveDirection direction,
        std::uint32_t amount = 1,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Moves the cursor on an output stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> moveCursor(
        Types::Output::Stream stream,
        Types::Cursor::MoveDirection direction,
        std::uint32_t amount = 1,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Sets cursor position on stdout.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> setCursorPosition(
        Types::Cursor::Position position,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Sets cursor position on an output stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> setCursorPosition(
        Types::Output::Stream stream,
        Types::Cursor::Position position,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Queries cursor position on stdout.
    [[nodiscard]] TERMINAL_EXPORT Types::Cursor::PositionResult getCursorPosition(const Types::Cursor::QueryOptions &options = {}) noexcept;

    /// @brief Queries cursor position through an output stream and reads protocol responses from an input stream.
    [[nodiscard]] TERMINAL_EXPORT Types::Cursor::PositionResult getCursorPosition(
        Types::Output::Stream outputStream,
        Types::Input::Stream responseStream,
        const Types::Cursor::QueryOptions &options = {}) noexcept;

    /// @brief Saves cursor position on stdout.
    /// @note Saved position is backend state, not a guaranteed stack; a later save can replace it.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> saveCursorPosition(const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Saves cursor position on an output stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> saveCursorPosition(
        Types::Output::Stream stream,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Restores cursor position on stdout.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> restoreCursorPosition(
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Restores cursor position on an output stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> restoreCursorPosition(
        Types::Output::Stream stream,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Sets cursor visibility on stdout.
    /// @warning Do not mix manual visibility changes with active CursorHiddenScope objects for stdout.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> setCursorVisible(
        bool visible,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Sets cursor visibility on an output stream.
    /// @warning Do not mix manual visibility changes with active CursorHiddenScope objects for the same stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> setCursorVisible(
        Types::Output::Stream stream,
        bool visible,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Hides the cursor on stdout until the returned scope restores it.
    /// @warning Do not use manual visibility changes while the returned scope is active.
    [[nodiscard]] TERMINAL_EXPORT CursorHiddenScope scopedCursorHidden(const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Hides the cursor on an output stream until the returned scope restores it.
    /// @warning Do not use manual visibility changes for the same stream while the returned scope is active.
    [[nodiscard]] TERMINAL_EXPORT CursorHiddenScope
    scopedCursorHidden(Types::Output::Stream stream, const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Clears a screen or line region on stdout.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> clear(
        Types::Output::ClearTarget target = Types::Output::ClearTarget::EntireScreen,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Clears a screen or line region on an output stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> clear(
        Types::Output::Stream stream,
        Types::Output::ClearTarget target = Types::Output::ClearTarget::EntireScreen,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Scrolls stdout.
    /// @note A zero line count emits no control sequence but can still honor options.flushMode.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> scroll(
        Types::Output::ScrollDirection direction,
        std::uint32_t lines = 1,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Scrolls an output stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> scroll(
        Types::Output::Stream stream,
        Types::Output::ScrollDirection direction,
        std::uint32_t lines = 1,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Enters alternate screen mode on stdout.
    /// @warning Do not mix manual transitions with active AlternateScreenScope objects for stdout.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> enterAlternateScreen(
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Enters alternate screen mode on an output stream.
    /// @warning Do not mix manual transitions with active AlternateScreenScope objects for the same stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> enterAlternateScreen(
        Types::Output::Stream stream,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Leaves alternate screen mode on stdout.
    /// @warning Do not mix manual transitions with active AlternateScreenScope objects for stdout.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> leaveAlternateScreen(
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Leaves alternate screen mode on an output stream.
    /// @warning Do not mix manual transitions with active AlternateScreenScope objects for the same stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> leaveAlternateScreen(
        Types::Output::Stream stream,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Enters alternate screen mode on stdout until the returned scope leaves it.
    /// @warning Do not use manual alternate-screen transitions while the returned scope is active.
    [[nodiscard]] TERMINAL_EXPORT AlternateScreenScope scopedAlternateScreen(const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Enters alternate screen mode on an output stream until the returned scope leaves it.
    /// @warning Do not use manual alternate-screen transitions for the same stream while the returned scope is active.
    [[nodiscard]] TERMINAL_EXPORT AlternateScreenScope
    scopedAlternateScreen(Types::Output::Stream stream, const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Sets the terminal title through stdout.
    /// @details Backend limits and sanitization apply; the current Win32 backend replaces C0 controls and DEL with spaces.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> setTitle(
        std::string_view utf8Title,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Sets the terminal title through an output stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> setTitle(
        Types::Output::Stream stream,
        std::string_view utf8Title,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Emits the terminal bell control through stdout.
    /// @note Terminal or user settings decide whether the control produces an audible sound.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> ringBell(const Types::Output::ControlOptions &options = {}) noexcept;

    /// @brief Rings the terminal bell through an output stream.
    [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> ringBell(
        Types::Output::Stream stream,
        const Types::Output::ControlOptions &options = {}) noexcept;

    /// @}
    /// @cond INTERNAL_TERMINAL_DETAIL
    namespace Internal
    {
        /// @brief Exported ABI bridge used by the public print() templates.
        /// @warning Internal support symbol; consumers must call print() instead.
        [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> vprint(
            Types::Output::Stream stream,
            const Types::Output::TextOptions &options,
            std::string_view format,
            std::format_args arguments) noexcept;

        /// @brief Exported ABI bridge used by the public println() templates.
        /// @warning Internal support symbol; consumers must call println() instead.
        [[nodiscard]] TERMINAL_EXPORT GameWIP::Status::Types::Result<void> vprintln(
            Types::Output::Stream stream,
            const Types::Output::LineOptions &options,
            std::string_view format,
            std::format_args arguments) noexcept;
    } // namespace Internal
    /// @endcond

    template <class... Args> GameWIP::Status::Types::Result<void> OutputBuffer::print(std::format_string<Args...> format, Args &&...args) noexcept
    {
        return vprint(format.get(), std::make_format_args(args...), false);
    }

    template <class... Args> GameWIP::Status::Types::Result<void> OutputBuffer::println(std::format_string<Args...> format, Args &&...args) noexcept
    {
        return vprint(format.get(), std::make_format_args(args...), true);
    }

    template <class... Args> GameWIP::Status::Types::Result<void> print(std::format_string<Args...> format, Args &&...args) noexcept
    {
        return print(Types::Output::Stream::Stdout, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    GameWIP::Status::Types::Result<void> print(Types::Output::Stream stream, std::format_string<Args...> format, Args &&...args) noexcept
    {
        return print(stream, Types::Output::TextOptions{}, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    GameWIP::Status::Types::Result<void> print(const Types::Output::TextOptions &options, std::format_string<Args...> format, Args &&...args) noexcept
    {
        return print(Types::Output::Stream::Stdout, options, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    GameWIP::Status::Types::Result<void> print(
        Types::Output::Stream stream,
        const Types::Output::TextOptions &options,
        std::format_string<Args...> format,
        Args &&...args) noexcept
    {
        return Internal::vprint(stream, options, format.get(), std::make_format_args(args...));
    }

    template <class... Args> GameWIP::Status::Types::Result<void> println(std::format_string<Args...> format, Args &&...args) noexcept
    {
        return println(Types::Output::Stream::Stdout, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    GameWIP::Status::Types::Result<void> println(Types::Output::Stream stream, std::format_string<Args...> format, Args &&...args) noexcept
    {
        return println(stream, Types::Output::LineOptions{}, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    GameWIP::Status::Types::Result<void> println(
        const Types::Output::LineOptions &options,
        std::format_string<Args...> format,
        Args &&...args) noexcept
    {
        return println(Types::Output::Stream::Stdout, options, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    GameWIP::Status::Types::Result<void> println(
        Types::Output::Stream stream,
        const Types::Output::LineOptions &options,
        std::format_string<Args...> format,
        Args &&...args) noexcept
    {
        return Internal::vprintln(stream, options, format.get(), std::make_format_args(args...));
    }
} // namespace GameWIP::Terminal
