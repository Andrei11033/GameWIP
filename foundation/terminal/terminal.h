/// @file terminal.h
/// @brief Public API for the GameWIP Terminal foundation library.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "io/io.h"

/// @brief Platform-neutral terminal stream I/O, styling, and control primitives.
namespace GameWIP::Terminal
{
    /// @brief Sentinel timeout meaning wait forever.
    inline constexpr std::chrono::milliseconds kWaitForever{-1};

    /// @brief Sentinel timeout meaning do not wait.
    inline constexpr std::chrono::milliseconds kNoWait{0};

    /// @brief Default timeout used by best-effort terminal control queries.
    inline constexpr std::chrono::milliseconds kDefaultQueryTimeout{100};

    /// @brief Default maximum byte count accepted by text reads.
    inline constexpr std::uint64_t kDefaultMaxTextBytes = IO::kDefaultBufferSize;

    /// @brief Default maximum byte count accepted by line reads.
    inline constexpr std::uint64_t kDefaultMaxLineBytes = 64 * 1024;

    /// @brief Passive Terminal data shapes.
    namespace Types
    {
        /// @brief Standard terminal input streams.
        enum class InputStream
        {
            /// @brief Process standard input.
            Stdin
        };

        /// @brief Standard terminal output streams.
        enum class OutputStream
        {
            /// @brief Process standard output.
            Stdout,

            /// @brief Process standard error.
            Stderr
        };

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

        /// @brief Outcome of a terminal read operation.
        enum class ReadOutcome
        {
            /// @brief Data was read normally.
            Completed,

            /// @brief The input stream reached end-of-stream.
            EndOfStream,

            /// @brief The read timed out before completing.
            TimedOut,

            /// @brief A non-blocking read found no available input.
            WouldBlock
        };

        /// @brief Line ending consumed by a line read.
        enum class ConsumedLineEnding
        {
            /// @brief No line ending was consumed.
            None,

            /// @brief A line feed ending was consumed.
            Lf,

            /// @brief A carriage-return plus line-feed ending was consumed.
            CrLf,

            /// @brief A carriage-return ending was consumed.
            Cr
        };

        /// @brief Styling behavior requested by text output calls.
        enum class StyleMode
        {
            /// @brief Never emit styling; write plain text.
            Never,

            /// @brief Emit styling only when supported.
            Auto,

            /// @brief Force styling when possible; fail with Unsupported when it cannot be forced.
            Always
        };

        /// @brief Kind of terminal color stored in Color.
        enum class ColorKind
        {
            /// @brief Use the terminal default color.
            Default,

            /// @brief Use one of the portable basic terminal colors.
            Basic,

            /// @brief Use RGB color when supported.
            Rgb
        };

        /// @brief Portable basic terminal colors.
        enum class BasicColor
        {
            Black,
            Red,
            Green,
            Yellow,
            Blue,
            Magenta,
            Cyan,
            White,

            BrightBlack,
            BrightRed,
            BrightGreen,
            BrightYellow,
            BrightBlue,
            BrightMagenta,
            BrightCyan,
            BrightWhite
        };

        /// @brief Portable terminal color request.
        struct Color
        {
            /// @brief Color representation kind.
            ColorKind kind = ColorKind::Default;

            /// @brief Basic color used when kind is ColorKind::Basic.
            BasicColor basic = BasicColor::White;

            /// @brief Red channel used when kind is ColorKind::Rgb.
            std::uint8_t red = 0;

            /// @brief Green channel used when kind is ColorKind::Rgb.
            std::uint8_t green = 0;

            /// @brief Blue channel used when kind is ColorKind::Rgb.
            std::uint8_t blue = 0;
        };

        /// @brief Portable terminal text style request.
        struct TextStyle
        {
            /// @brief Requested foreground color.
            Color foreground{};

            /// @brief Requested background color.
            Color background{};

            /// @brief Request bold/intense text when supported.
            bool bold = false;

            /// @brief Request dim text when supported.
            bool dim = false;

            /// @brief Request italic text when supported.
            bool italic = false;

            /// @brief Request underlined text when supported.
            bool underline = false;

            /// @brief Request inverse foreground/background text when supported.
            bool inverse = false;

            /// @brief Request strikethrough text when supported.
            bool strikethrough = false;
        };

        /// @brief Styling features supported by an output stream.
        struct StyleCapabilities
        {
            /// @brief True when the portable 16-color set is supported.
            bool basicColor = false;

            /// @brief True when RGB color output is supported.
            bool rgbColor = false;

            /// @brief True when bold/intense text is supported.
            bool bold = false;

            /// @brief True when dim text is supported.
            bool dim = false;

            /// @brief True when italic text is supported.
            bool italic = false;

            /// @brief True when underline is supported.
            bool underline = false;

            /// @brief True when inverse foreground/background text is supported.
            bool inverse = false;

            /// @brief True when strikethrough is supported.
            bool strikethrough = false;
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

        /// @brief How readLine() reports consumed line endings for line reads.
        enum class ReadLineEndingMode
        {
            /// @brief Do not include the consumed line ending in the returned line.
            Strip,

            /// @brief Preserve the consumed line ending when practical.
            Keep,

            /// @brief Return a trailing '\n' for any consumed line ending.
            NormalizeToLf
        };

        /// @brief Common terminal input mode presets.
        enum class InputModePreset
        {
            /// @brief Backend/default mode for the input stream.
            Default,

            /// @brief Normal interactive input: line-buffered, echoed, and control keys processed.
            InteractiveLine,

            /// @brief Raw byte-oriented input where practical.
            RawBytes
        };

        /// @brief Portable terminal input mode.
        struct InputMode
        {
            /// @brief Whether input waits for a complete line before reads complete.
            bool lineBuffered = true;

            /// @brief Whether typed input is echoed by the terminal.
            bool echoInput = true;

            /// @brief Whether platform terminal control keys are processed.
            bool processControlKeys = true;
        };

        /// @brief Capabilities of a standard terminal input stream.
        struct InputCapabilities
        {
            /// @brief Detected stream endpoint kind.
            StreamKind kind = StreamKind::Detached;

            /// @brief True when UTF-8 text input is supported.
            bool supportsUtf8Text = false;

            /// @brief True when byte input is supported.
            bool supportsByteInput = false;

            /// @brief True when line input is supported.
            bool supportsLineInput = false;

            /// @brief True when raw byte input mode is supported.
            bool supportsRawInput = false;

            /// @brief True when echo can be controlled.
            bool supportsEchoControl = false;

            /// @brief True when input mode can be queried or changed.
            bool supportsInputMode = false;

            /// @brief True when input availability can be queried without normal blocking.
            bool supportsInputAvailability = false;

            /// @brief True when timed reads are supported.
            bool supportsReadTimeout = false;
        };

        /// @brief Capabilities of a standard terminal output stream.
        struct OutputCapabilities
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
            StyleCapabilities style{};

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

        /// @brief Result returned by input capability queries.
        struct InputCapabilityResult
        {
            /// @brief Operation status.
            IO::Types::Status status;

            /// @brief Reported input capabilities.
            InputCapabilities capabilities;
        };

        /// @brief Result returned by output capability queries.
        struct OutputCapabilityResult
        {
            /// @brief Operation status.
            IO::Types::Status status;

            /// @brief Reported output capabilities.
            OutputCapabilities capabilities;
        };

        /// @brief Terminal size in character cells.
        struct TerminalSize
        {
            /// @brief Width in columns.
            std::uint32_t columns = 0;

            /// @brief Height in rows.
            std::uint32_t rows = 0;
        };

        /// @brief Result returned by terminal size queries.
        struct TerminalSizeResult
        {
            /// @brief Operation status.
            IO::Types::Status status;

            /// @brief Reported terminal size.
            TerminalSize size;
        };

        /// @brief Zero-based cursor position.
        struct CursorPosition
        {
            /// @brief Zero-based column.
            std::uint32_t column = 0;

            /// @brief Zero-based row.
            std::uint32_t row = 0;
        };

        /// @brief Result returned by cursor position queries.
        struct CursorPositionResult
        {
            /// @brief Operation status.
            IO::Types::Status status;

            /// @brief Reported cursor position.
            CursorPosition position;
        };

        /// @brief Relative cursor movement direction.
        enum class CursorMoveDirection
        {
            Up,
            Down,
            Left,
            Right
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
            Up,
            Down
        };

        /// @brief Options used by byte reads.
        struct ByteReadOptions
        {
            /// @brief Negative waits forever, zero does not wait, positive waits up to that duration.
            std::chrono::milliseconds timeout = kWaitForever;

            /// @brief Whether a successful read may return fewer bytes than requested.
            bool allowPartial = true;
        };

        /// @brief Options used by text reads.
        /// @details Text reads return one available UTF-8 chunk after input becomes available.
        struct TextReadOptions
        {
            /// @brief Negative waits forever, zero does not wait, positive waits up to that duration.
            std::chrono::milliseconds timeout = kWaitForever;

            /// @brief Maximum accepted byte count for one available UTF-8 text chunk.
            std::uint64_t maxBytes = kDefaultMaxTextBytes;
        };

        /// @brief Options used by line reads.
        /// @details Line reads do not expose allowPartial; they stop at a line ending, terminating outcome, truncation, or failure.
        struct LineReadOptions
        {
            /// @brief Negative waits forever, zero does not wait, positive waits up to that duration.
            std::chrono::milliseconds timeout = kWaitForever;

            /// @brief Maximum accepted byte count for the returned line representation.
            std::uint64_t maxBytes = kDefaultMaxLineBytes;

            /// @brief How a consumed line ending is represented.
            ReadLineEndingMode lineEndingMode = ReadLineEndingMode::Strip;
        };

        /// @brief Options used by text output calls.
        struct TextWriteOptions
        {
            /// @brief Styling behavior.
            StyleMode styleMode = StyleMode::Auto;

            /// @brief Style to apply to this text write.
            TextStyle style{};

            /// @brief Flush requested after the write.
            IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
        };

        /// @brief Options used by line output calls.
        struct LineWriteOptions
        {
            /// @brief Styling behavior.
            StyleMode styleMode = StyleMode::Auto;

            /// @brief Style to apply to this line write.
            TextStyle style{};

            /// @brief Line ending appended after the text.
            LineEnding lineEnding = LineEnding::Native;

            /// @brief Flush requested after the write.
            IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
        };

        /// @brief Options used by byte output calls.
        struct ByteWriteOptions
        {
            /// @brief Flush requested after the write.
            IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
        };

        /// @brief Options used by segmented output calls.
        struct SegmentWriteOptions
        {
            /// @brief Styling behavior for styled segments.
            StyleMode styleMode = StyleMode::Auto;

            /// @brief Whether to append one line ending after all segments.
            bool appendLineEnding = false;

            /// @brief Line ending used when appendLineEnding is true.
            LineEnding lineEnding = LineEnding::Native;

            /// @brief Flush requested after the full segment batch.
            IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
        };

        /// @brief Options used by terminal control calls.
        struct ControlOptions
        {
            /// @brief Flush requested after the control operation.
            IO::Types::FlushMode flushMode = IO::Types::FlushMode::None;
        };

        /// @brief Options used by cursor position queries.
        struct CursorPositionQueryOptions
        {
            /// @brief Maximum time to wait for a terminal cursor-position response.
            /// @note Win32 real-console queries use the console API directly and do not wait for an input-stream response.
            std::chrono::milliseconds timeout = kDefaultQueryTimeout;

            /// @brief Flush requested after sending the cursor-position query.
            /// @note Cursor position queries may consume a terminal response from the corresponding input stream.
            IO::Types::FlushMode flushMode = IO::Types::FlushMode::Data;
        };

        /// @brief Result returned by input mode queries.
        struct InputModeResult
        {
            /// @brief Operation status.
            IO::Types::Status status;

            /// @brief Reported input mode.
            InputMode mode;
        };

        /// @brief Result returned by input availability queries.
        struct InputAvailabilityResult
        {
            /// @brief Operation status.
            IO::Types::Status status;

            /// @brief True when input can be read without a normal blocking wait.
            bool available = false;

            /// @brief Best-effort byte estimate. May be zero even when available is true.
            std::uint64_t estimatedBytes = 0;
        };

        /// @brief Result returned by byte reads.
        struct ByteReadResult
        {
            /// @brief Operation status.
            IO::Types::Status status;

            /// @brief Read outcome.
            ReadOutcome outcome = ReadOutcome::Completed;

            /// @brief Number of bytes copied into the caller buffer.
            std::size_t bytesRead = 0;
        };

        /// @brief Result returned by text reads.
        struct TextReadResult
        {
            /// @brief Operation status.
            IO::Types::Status status;

            /// @brief Read outcome.
            ReadOutcome outcome = ReadOutcome::Completed;

            /// @brief UTF-8 text bytes read.
            std::string text;

            /// @brief True when maxBytes limited the returned text.
            bool wasTruncated = false;
        };

        /// @brief Result returned by line reads.
        struct LineReadResult
        {
            /// @brief Operation status.
            IO::Types::Status status;

            /// @brief Read outcome.
            ReadOutcome outcome = ReadOutcome::Completed;

            /// @brief UTF-8 line text.
            std::string line;

            /// @brief Consumed line ending.
            ConsumedLineEnding consumedLineEnding = ConsumedLineEnding::None;

            /// @brief True when maxBytes limited the returned line.
            bool wasTruncated = false;
        };

        /// @brief Kind of write segment.
        enum class WriteSegmentKind
        {
            /// @brief Plain UTF-8 text.
            Text,

            /// @brief UTF-8 text with style.
            StyledText,

            /// @brief Raw bytes.
            Bytes
        };

        /// @brief One piece of optimized batched terminal output.
        struct WriteSegment
        {
            /// @brief Segment kind.
            WriteSegmentKind kind = WriteSegmentKind::Text;

            /// @brief Text used by Text and StyledText segments.
            std::string_view text{};

            /// @brief Bytes used by Bytes segments.
            std::span<const std::byte> bytes{};

            /// @brief Style used by StyledText segments.
            TextStyle style{};
        };
    } // namespace Types

    /// @brief Creates a terminal default color.
    [[nodiscard]] Types::Color defaultColor() noexcept;

    /// @brief Creates a basic terminal color.
    [[nodiscard]] Types::Color basicColor(Types::BasicColor color) noexcept;

    /// @brief Creates an RGB terminal color.
    [[nodiscard]] Types::Color rgbColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept;

    /// @brief Creates an input mode from a preset.
    [[nodiscard]] Types::InputMode makeInputMode(Types::InputModePreset preset) noexcept;

    /// @brief Creates a plain text write segment.
    [[nodiscard]] Types::WriteSegment textSegment(std::string_view text) noexcept;

    /// @brief Creates a styled text write segment.
    [[nodiscard]] Types::WriteSegment styledSegment(std::string_view text, const Types::TextStyle &style) noexcept;

    /// @brief Creates a byte write segment.
    [[nodiscard]] Types::WriteSegment byteSegment(std::span<const std::byte> bytes) noexcept;

    /// @brief RAII helper that restores a previous terminal input mode.
    class InputModeScope final
    {
    public:
        /// @brief Creates an inactive input mode scope.
        InputModeScope() noexcept;

        InputModeScope(const InputModeScope &) = delete;
        InputModeScope &operator=(const InputModeScope &) = delete;

        /// @brief Move-constructs the scope and transfers restoration responsibility.
        InputModeScope(InputModeScope &&other) noexcept;

        /// @brief Move-assigns the scope and transfers restoration responsibility.
        InputModeScope &operator=(InputModeScope &&other) noexcept;

        /// @brief Restores the previous input mode on a best-effort basis without throwing.
        ~InputModeScope() noexcept;

        /// @brief Returns whether this scope still owns restoration responsibility.
        [[nodiscard]] bool active() const noexcept;

        /// @brief Returns the last setup or restoration status tracked by this scope.
        [[nodiscard]] const IO::Types::Status &status() const noexcept;

        /// @brief Restores the previous input mode and reports restoration failure.
        [[nodiscard]] IO::Types::Status restore() noexcept;

        /// @brief Releases restoration responsibility without changing input mode.
        void release() noexcept;

    private:
        friend InputModeScope scopedInputMode(Types::InputStream stream, const Types::InputMode &mode) noexcept;

        Types::InputStream stream_ = Types::InputStream::Stdin;
        Types::InputMode previousMode_{};
        IO::Types::Status status_{};
        bool active_ = false;
    };

    /// @brief RAII helper that leaves alternate screen mode when destroyed.
    class AlternateScreenScope final
    {
    public:
        /// @brief Creates an inactive alternate screen scope.
        AlternateScreenScope() noexcept;

        AlternateScreenScope(const AlternateScreenScope &) = delete;
        AlternateScreenScope &operator=(const AlternateScreenScope &) = delete;

        /// @brief Move-constructs the scope and transfers leave responsibility.
        AlternateScreenScope(AlternateScreenScope &&other) noexcept;

        /// @brief Move-assigns the scope and transfers leave responsibility.
        AlternateScreenScope &operator=(AlternateScreenScope &&other) noexcept;

        /// @brief Leaves alternate screen mode on a best-effort basis without throwing.
        ~AlternateScreenScope() noexcept;

        /// @brief Returns whether this scope still owns leave responsibility.
        [[nodiscard]] bool active() const noexcept;

        /// @brief Returns the last setup or leave status tracked by this scope.
        [[nodiscard]] const IO::Types::Status &status() const noexcept;

        /// @brief Leaves alternate screen mode and reports leave failure.
        [[nodiscard]] IO::Types::Status leave() noexcept;

        /// @brief Releases leave responsibility without changing alternate screen mode.
        void release() noexcept;

    private:
        friend AlternateScreenScope scopedAlternateScreen(Types::OutputStream stream, const Types::ControlOptions &options) noexcept;

        Types::OutputStream stream_ = Types::OutputStream::Stdout;
        Types::ControlOptions options_{};
        IO::Types::Status status_{};
        bool active_ = false;
    };

    /// @brief RAII helper that restores cursor visibility by showing the cursor when destroyed.
    class CursorVisibilityScope final
    {
    public:
        /// @brief Creates an inactive cursor visibility scope.
        CursorVisibilityScope() noexcept;

        CursorVisibilityScope(const CursorVisibilityScope &) = delete;
        CursorVisibilityScope &operator=(const CursorVisibilityScope &) = delete;

        /// @brief Move-constructs the scope and transfers restoration responsibility.
        CursorVisibilityScope(CursorVisibilityScope &&other) noexcept;

        /// @brief Move-assigns the scope and transfers restoration responsibility.
        CursorVisibilityScope &operator=(CursorVisibilityScope &&other) noexcept;

        /// @brief Restores cursor visibility on a best-effort basis without throwing.
        ~CursorVisibilityScope() noexcept;

        /// @brief Returns whether this scope still owns restoration responsibility.
        [[nodiscard]] bool active() const noexcept;

        /// @brief Returns the last setup or restoration status tracked by this scope.
        [[nodiscard]] const IO::Types::Status &status() const noexcept;

        /// @brief Restores cursor visibility and reports restoration failure.
        [[nodiscard]] IO::Types::Status restore() noexcept;

        /// @brief Releases restoration responsibility without changing cursor visibility.
        void release() noexcept;

    private:
        friend CursorVisibilityScope scopedCursorHidden(Types::OutputStream stream, const Types::ControlOptions &options) noexcept;

        Types::OutputStream stream_ = Types::OutputStream::Stdout;
        Types::ControlOptions options_{};
        IO::Types::Status status_{};
        bool active_ = false;
    };

    /// @brief Reader for terminal input.
    class Reader final
    {
    public:
        /// @brief Creates a reader using stdin as its default stream.
        explicit Reader(Types::InputStream defaultStream = Types::InputStream::Stdin) noexcept;

        /// @brief Returns the input stream used by streamless member calls.
        [[nodiscard]] Types::InputStream defaultStream() const noexcept;

        /// @brief Sets the input stream used by streamless member calls.
        void setDefaultStream(Types::InputStream stream) noexcept;

        /// @brief Gets capabilities for the default input stream.
        [[nodiscard]] Types::InputCapabilityResult getCapabilities() const;

        /// @brief Gets capabilities for an input stream.
        [[nodiscard]] Types::InputCapabilityResult getCapabilities(Types::InputStream stream) const;

        /// @brief Checks whether input is available on the default input stream.
        [[nodiscard]] Types::InputAvailabilityResult getInputAvailability() const;

        /// @brief Checks whether input is available on an input stream.
        [[nodiscard]] Types::InputAvailabilityResult getInputAvailability(Types::InputStream stream) const;

        /// @brief Gets the current mode for the default input stream.
        [[nodiscard]] Types::InputModeResult getInputMode() const;

        /// @brief Gets the current mode for an input stream.
        [[nodiscard]] Types::InputModeResult getInputMode(Types::InputStream stream) const;

        /// @brief Sets the mode for the default input stream.
        [[nodiscard]] IO::Types::Status setInputMode(const Types::InputMode &mode) const;

        /// @brief Sets the mode for an input stream.
        [[nodiscard]] IO::Types::Status setInputMode(Types::InputStream stream, const Types::InputMode &mode) const;

        /// @brief Restores the backend/default mode for the default input stream.
        [[nodiscard]] IO::Types::Status restoreDefaultInputMode() const;

        /// @brief Restores the backend/default mode for an input stream.
        [[nodiscard]] IO::Types::Status restoreDefaultInputMode(Types::InputStream stream) const;

        /// @brief Temporarily sets the default input stream mode and returns a restoration scope.
        [[nodiscard]] InputModeScope scopedInputMode(const Types::InputMode &mode) const noexcept;

        /// @brief Temporarily sets an input stream mode and returns a restoration scope.
        [[nodiscard]] InputModeScope scopedInputMode(Types::InputStream stream, const Types::InputMode &mode) const noexcept;

        /// @brief Reads one line from the default input stream.
        [[nodiscard]] Types::LineReadResult readLine(const Types::LineReadOptions &options = {}) const;

        /// @brief Reads one available UTF-8 text chunk from the default input stream.
        [[nodiscard]] Types::TextReadResult readText(const Types::TextReadOptions &options = {}) const;

        /// @brief Reads bytes from the default input stream into caller storage.
        [[nodiscard]] Types::ByteReadResult readBytes(std::span<std::byte> outputBuffer, const Types::ByteReadOptions &options = {}) const;

    private:
        Types::InputStream defaultStream_ = Types::InputStream::Stdin;
    };

    /// @brief Writer for terminal output and terminal control operations.
    class Writer final
    {
    public:
        /// @brief Creates a writer using stdout as its default stream.
        explicit Writer(Types::OutputStream defaultStream = Types::OutputStream::Stdout) noexcept;

        /// @brief Returns the output stream used by streamless member calls.
        [[nodiscard]] Types::OutputStream defaultStream() const noexcept;

        /// @brief Sets the output stream used by streamless member calls.
        void setDefaultStream(Types::OutputStream stream) noexcept;

        /// @brief Gets capabilities for the default output stream.
        [[nodiscard]] Types::OutputCapabilityResult getCapabilities() const;

        /// @brief Gets capabilities for an output stream.
        [[nodiscard]] Types::OutputCapabilityResult getCapabilities(Types::OutputStream stream) const;

        /// @brief Gets terminal size for the default output stream.
        [[nodiscard]] Types::TerminalSizeResult getTerminalSize() const;

        /// @brief Gets terminal size for an output stream.
        [[nodiscard]] Types::TerminalSizeResult getTerminalSize(Types::OutputStream stream) const;

        /// @brief Writes UTF-8 text to the default output stream.
        [[nodiscard]] IO::Types::Status writeText(std::string_view utf8Text, const Types::TextWriteOptions &options = {}) const;

        /// @brief Writes UTF-8 text followed by a line ending to the default output stream.
        [[nodiscard]] IO::Types::Status writeLine(std::string_view utf8Text = {}, const Types::LineWriteOptions &options = {}) const;

        /// @brief Writes bytes to the default output stream.
        [[nodiscard]] IO::Types::WriteResult writeBytes(std::span<const std::byte> bytes, const Types::ByteWriteOptions &options = {}) const;

        /// @brief Writes text, styled text, and byte segments to the default output stream.
        [[nodiscard]] IO::Types::Status writeSegments(
            std::span<const Types::WriteSegment> segments,
            const Types::SegmentWriteOptions &options = {}) const;

        /// @brief Formats text and writes it to the default output stream.
        template <class... Args>
        [[nodiscard]] IO::Types::Status print(std::format_string<Args...> format, Args &&...args) const;

        /// @brief Formats text and writes it to the default output stream.
        template <class... Args>
        [[nodiscard]] IO::Types::Status print(
            const Types::TextWriteOptions &options,
            std::format_string<Args...> format,
            Args &&...args) const;

        /// @brief Formats text and writes it followed by a line ending to the default output stream.
        template <class... Args>
        [[nodiscard]] IO::Types::Status println(std::format_string<Args...> format, Args &&...args) const;

        /// @brief Formats text and writes it followed by a line ending to the default output stream.
        template <class... Args>
        [[nodiscard]] IO::Types::Status println(
            const Types::LineWriteOptions &options,
            std::format_string<Args...> format,
            Args &&...args) const;

        /// @brief Flushes the default output stream.
        [[nodiscard]] IO::Types::Status flush(IO::Types::FlushMode mode = IO::Types::FlushMode::Data) const;

        /// @brief Flushes an output stream.
        [[nodiscard]] IO::Types::Status flush(Types::OutputStream stream, IO::Types::FlushMode mode = IO::Types::FlushMode::Data) const;

        /// @brief Resets style on the default output stream.
        [[nodiscard]] IO::Types::Status resetStyle(const Types::ControlOptions &options = {}) const;

        /// @brief Resets style on an output stream.
        [[nodiscard]] IO::Types::Status resetStyle(Types::OutputStream stream, const Types::ControlOptions &options = {}) const;

        /// @brief Moves the cursor on the default output stream.
        [[nodiscard]] IO::Types::Status moveCursor(
            Types::CursorMoveDirection direction,
            std::uint32_t amount = 1,
            const Types::ControlOptions &options = {}) const;

        /// @brief Moves the cursor on an output stream.
        [[nodiscard]] IO::Types::Status moveCursor(
            Types::OutputStream stream,
            Types::CursorMoveDirection direction,
            std::uint32_t amount = 1,
            const Types::ControlOptions &options = {}) const;

        /// @brief Sets cursor position on the default output stream.
        [[nodiscard]] IO::Types::Status setCursorPosition(Types::CursorPosition position, const Types::ControlOptions &options = {}) const;

        /// @brief Sets cursor position on an output stream.
        [[nodiscard]] IO::Types::Status setCursorPosition(
            Types::OutputStream stream,
            Types::CursorPosition position,
            const Types::ControlOptions &options = {}) const;

        /// @brief Queries cursor position on the default output stream.
        [[nodiscard]] Types::CursorPositionResult getCursorPosition(const Types::CursorPositionQueryOptions &options = {}) const;

        /// @brief Queries cursor position on an output stream.
        [[nodiscard]] Types::CursorPositionResult getCursorPosition(Types::OutputStream stream, const Types::CursorPositionQueryOptions &options = {})
            const;

        /// @brief Saves cursor position on the default output stream.
        [[nodiscard]] IO::Types::Status saveCursorPosition(const Types::ControlOptions &options = {}) const;

        /// @brief Saves cursor position on an output stream.
        [[nodiscard]] IO::Types::Status saveCursorPosition(Types::OutputStream stream, const Types::ControlOptions &options = {}) const;

        /// @brief Restores cursor position on the default output stream.
        [[nodiscard]] IO::Types::Status restoreCursorPosition(const Types::ControlOptions &options = {}) const;

        /// @brief Restores cursor position on an output stream.
        [[nodiscard]] IO::Types::Status restoreCursorPosition(Types::OutputStream stream, const Types::ControlOptions &options = {}) const;

        /// @brief Sets cursor visibility on the default output stream.
        [[nodiscard]] IO::Types::Status setCursorVisible(bool visible, const Types::ControlOptions &options = {}) const;

        /// @brief Sets cursor visibility on an output stream.
        [[nodiscard]] IO::Types::Status setCursorVisible(Types::OutputStream stream, bool visible, const Types::ControlOptions &options = {}) const;

        /// @brief Hides the cursor on the default output stream until the returned scope restores it.
        [[nodiscard]] CursorVisibilityScope scopedCursorHidden(const Types::ControlOptions &options = {}) const noexcept;

        /// @brief Hides the cursor on an output stream until the returned scope restores it.
        [[nodiscard]] CursorVisibilityScope scopedCursorHidden(Types::OutputStream stream, const Types::ControlOptions &options = {}) const noexcept;

        /// @brief Clears a screen or line region on the default output stream.
        [[nodiscard]] IO::Types::Status clear(Types::ClearTarget target = Types::ClearTarget::EntireScreen, const Types::ControlOptions &options = {})
            const;

        /// @brief Clears a screen or line region on an output stream.
        [[nodiscard]] IO::Types::Status clear(
            Types::OutputStream stream,
            Types::ClearTarget target = Types::ClearTarget::EntireScreen,
            const Types::ControlOptions &options = {}) const;

        /// @brief Scrolls the default output stream.
        [[nodiscard]] IO::Types::Status scroll(Types::ScrollDirection direction, std::uint32_t lines = 1, const Types::ControlOptions &options = {})
            const;

        /// @brief Scrolls an output stream.
        [[nodiscard]] IO::Types::Status scroll(
            Types::OutputStream stream,
            Types::ScrollDirection direction,
            std::uint32_t lines = 1,
            const Types::ControlOptions &options = {}) const;

        /// @brief Enters alternate screen mode on the default output stream.
        [[nodiscard]] IO::Types::Status enterAlternateScreen(const Types::ControlOptions &options = {}) const;

        /// @brief Enters alternate screen mode on an output stream.
        [[nodiscard]] IO::Types::Status enterAlternateScreen(Types::OutputStream stream, const Types::ControlOptions &options = {}) const;

        /// @brief Leaves alternate screen mode on the default output stream.
        [[nodiscard]] IO::Types::Status leaveAlternateScreen(const Types::ControlOptions &options = {}) const;

        /// @brief Leaves alternate screen mode on an output stream.
        [[nodiscard]] IO::Types::Status leaveAlternateScreen(Types::OutputStream stream, const Types::ControlOptions &options = {}) const;

        /// @brief Enters alternate screen mode on the default output stream until the returned scope leaves it.
        [[nodiscard]] AlternateScreenScope scopedAlternateScreen(const Types::ControlOptions &options = {}) const noexcept;

        /// @brief Enters alternate screen mode on an output stream until the returned scope leaves it.
        [[nodiscard]] AlternateScreenScope scopedAlternateScreen(Types::OutputStream stream, const Types::ControlOptions &options = {})
            const noexcept;

        /// @brief Sets the terminal title through the default output stream.
        [[nodiscard]] IO::Types::Status setTitle(std::string_view utf8Title, const Types::ControlOptions &options = {}) const;

        /// @brief Sets the terminal title through an output stream.
        [[nodiscard]] IO::Types::Status setTitle(Types::OutputStream stream, std::string_view utf8Title, const Types::ControlOptions &options = {})
            const;

        /// @brief Rings the terminal bell through the default output stream.
        [[nodiscard]] IO::Types::Status ringBell(const Types::ControlOptions &options = {}) const;

        /// @brief Rings the terminal bell through an output stream.
        [[nodiscard]] IO::Types::Status ringBell(Types::OutputStream stream, const Types::ControlOptions &options = {}) const;

    private:
        Types::OutputStream defaultStream_ = Types::OutputStream::Stdout;
    };

    /// @brief Reusable plain-text output buffer for batching Terminal writes.
    class OutputBuffer final
    {
    public:
        /// @brief Creates an empty output buffer using a line ending for appendLine() and println().
        explicit OutputBuffer(Types::LineEnding lineEnding = Types::LineEnding::Native);

        /// @brief Reserves text storage for future appends.
        void reserve(std::size_t bytes);

        /// @brief Clears buffered text while retaining capacity.
        void clear() noexcept;

        /// @brief Returns whether the buffer is empty.
        [[nodiscard]] bool empty() const noexcept;

        /// @brief Returns buffered text size in bytes.
        [[nodiscard]] std::size_t size() const noexcept;

        /// @brief Returns a view of buffered UTF-8 text.
        [[nodiscard]] std::string_view text() const noexcept;

        /// @brief Appends UTF-8 text to the buffer.
        void appendText(std::string_view utf8Text);

        /// @brief Appends UTF-8 text followed by the configured line ending.
        void appendLine(std::string_view utf8Text = {});

        /// @brief Formats text and appends it to the buffer.
        template <class... Args>
        void print(std::format_string<Args...> format, Args &&...args);

        /// @brief Formats text and appends it followed by the configured line ending.
        template <class... Args>
        void println(std::format_string<Args...> format, Args &&...args);

        /// @brief Writes buffered text to a writer without clearing the buffer.
        [[nodiscard]] IO::Types::Status writeTo(const Writer &writer, const Types::TextWriteOptions &options = {}) const;

        /// @brief Writes buffered text to a writer and clears it on success.
        [[nodiscard]] IO::Types::Status flushTo(const Writer &writer, const Types::TextWriteOptions &options = {});

    private:
        std::string text_;
        Types::LineEnding lineEnding_ = Types::LineEnding::Native;
    };

    /// @brief Gets capabilities for an input stream.
    [[nodiscard]] Types::InputCapabilityResult getInputCapabilities(Types::InputStream stream = Types::InputStream::Stdin);

    /// @brief Gets capabilities for an output stream.
    [[nodiscard]] Types::OutputCapabilityResult getOutputCapabilities(Types::OutputStream stream = Types::OutputStream::Stdout);

    /// @brief Gets terminal size for an output stream.
    [[nodiscard]] Types::TerminalSizeResult getTerminalSize(Types::OutputStream stream = Types::OutputStream::Stdout);

    /// @brief Checks whether input is available on an input stream.
    [[nodiscard]] Types::InputAvailabilityResult getInputAvailability(Types::InputStream stream = Types::InputStream::Stdin);

    /// @brief Gets the current mode for an input stream.
    [[nodiscard]] Types::InputModeResult getInputMode(Types::InputStream stream = Types::InputStream::Stdin);

    /// @brief Sets the mode for stdin.
    [[nodiscard]] IO::Types::Status setInputMode(const Types::InputMode &mode);

    /// @brief Sets the mode for an input stream.
    [[nodiscard]] IO::Types::Status setInputMode(Types::InputStream stream, const Types::InputMode &mode);

    /// @brief Restores the backend/default mode for an input stream.
    [[nodiscard]] IO::Types::Status restoreDefaultInputMode(Types::InputStream stream = Types::InputStream::Stdin);

    /// @brief Temporarily sets stdin mode and returns a restoration scope.
    [[nodiscard]] InputModeScope scopedInputMode(const Types::InputMode &mode) noexcept;

    /// @brief Temporarily sets an input stream mode and returns a restoration scope.
    [[nodiscard]] InputModeScope scopedInputMode(Types::InputStream stream, const Types::InputMode &mode) noexcept;

    /// @brief Reads one line from stdin.
    [[nodiscard]] Types::LineReadResult readLine(const Types::LineReadOptions &options = {});

    /// @brief Reads one line from an input stream.
    [[nodiscard]] Types::LineReadResult readLine(Types::InputStream stream, const Types::LineReadOptions &options = {});

    /// @brief Reads one available UTF-8 text chunk from stdin.
    [[nodiscard]] Types::TextReadResult readText(const Types::TextReadOptions &options = {});

    /// @brief Reads one available UTF-8 text chunk from an input stream.
    [[nodiscard]] Types::TextReadResult readText(Types::InputStream stream, const Types::TextReadOptions &options = {});

    /// @brief Reads bytes from stdin into caller storage.
    [[nodiscard]] Types::ByteReadResult readBytes(std::span<std::byte> outputBuffer, const Types::ByteReadOptions &options = {});

    /// @brief Reads bytes from an input stream into caller storage.
    [[nodiscard]] Types::ByteReadResult readBytes(
        Types::InputStream stream,
        std::span<std::byte> outputBuffer,
        const Types::ByteReadOptions &options = {});

    /// @brief Writes UTF-8 text to stdout.
    [[nodiscard]] IO::Types::Status writeText(std::string_view utf8Text, const Types::TextWriteOptions &options = {});

    /// @brief Writes UTF-8 text to an output stream.
    [[nodiscard]] IO::Types::Status writeText(
        Types::OutputStream stream,
        std::string_view utf8Text,
        const Types::TextWriteOptions &options = {});

    /// @brief Writes UTF-8 text followed by a line ending to stdout.
    [[nodiscard]] IO::Types::Status writeLine(std::string_view utf8Text = {}, const Types::LineWriteOptions &options = {});

    /// @brief Writes UTF-8 text followed by a line ending to an output stream.
    [[nodiscard]] IO::Types::Status writeLine(
        Types::OutputStream stream,
        std::string_view utf8Text = {},
        const Types::LineWriteOptions &options = {});

    /// @brief Writes bytes to stdout.
    [[nodiscard]] IO::Types::WriteResult writeBytes(std::span<const std::byte> bytes, const Types::ByteWriteOptions &options = {});

    /// @brief Writes bytes to an output stream.
    [[nodiscard]] IO::Types::WriteResult writeBytes(
        Types::OutputStream stream,
        std::span<const std::byte> bytes,
        const Types::ByteWriteOptions &options = {});

    /// @brief Writes text, styled text, and byte segments to stdout.
    [[nodiscard]] IO::Types::Status writeSegments(
        std::span<const Types::WriteSegment> segments,
        const Types::SegmentWriteOptions &options = {});

    /// @brief Writes text, styled text, and byte segments to an output stream.
    [[nodiscard]] IO::Types::Status writeSegments(
        Types::OutputStream stream,
        std::span<const Types::WriteSegment> segments,
        const Types::SegmentWriteOptions &options = {});

    /// @brief Formats text and writes it to stdout.
    template <class... Args>
    [[nodiscard]] IO::Types::Status print(std::format_string<Args...> format, Args &&...args);

    /// @brief Formats text and writes it to an output stream.
    template <class... Args>
    [[nodiscard]] IO::Types::Status print(Types::OutputStream stream, std::format_string<Args...> format, Args &&...args);

    /// @brief Formats text and writes it to stdout.
    template <class... Args>
    [[nodiscard]] IO::Types::Status print(
        const Types::TextWriteOptions &options,
        std::format_string<Args...> format,
        Args &&...args);

    /// @brief Formats text and writes it to an output stream.
    template <class... Args>
    [[nodiscard]] IO::Types::Status print(
        Types::OutputStream stream,
        const Types::TextWriteOptions &options,
        std::format_string<Args...> format,
        Args &&...args);

    /// @brief Formats text and writes it followed by a line ending to stdout.
    template <class... Args>
    [[nodiscard]] IO::Types::Status println(std::format_string<Args...> format, Args &&...args);

    /// @brief Formats text and writes it followed by a line ending to an output stream.
    template <class... Args>
    [[nodiscard]] IO::Types::Status println(Types::OutputStream stream, std::format_string<Args...> format, Args &&...args);

    /// @brief Formats text and writes it followed by a line ending to stdout.
    template <class... Args>
    [[nodiscard]] IO::Types::Status println(
        const Types::LineWriteOptions &options,
        std::format_string<Args...> format,
        Args &&...args);

    /// @brief Formats text and writes it followed by a line ending to an output stream.
    template <class... Args>
    [[nodiscard]] IO::Types::Status println(
        Types::OutputStream stream,
        const Types::LineWriteOptions &options,
        std::format_string<Args...> format,
        Args &&...args);

    /// @brief Flushes an output stream.
    [[nodiscard]] IO::Types::Status flush(
        Types::OutputStream stream = Types::OutputStream::Stdout,
        IO::Types::FlushMode mode = IO::Types::FlushMode::Data);

    /// @brief Resets style on an output stream.
    [[nodiscard]] IO::Types::Status resetStyle(Types::OutputStream stream = Types::OutputStream::Stdout, const Types::ControlOptions &options = {});

    /// @brief Moves the cursor on stdout.
    [[nodiscard]] IO::Types::Status moveCursor(
        Types::CursorMoveDirection direction,
        std::uint32_t amount = 1,
        const Types::ControlOptions &options = {});

    /// @brief Moves the cursor on an output stream.
    [[nodiscard]] IO::Types::Status moveCursor(
        Types::OutputStream stream,
        Types::CursorMoveDirection direction,
        std::uint32_t amount = 1,
        const Types::ControlOptions &options = {});

    /// @brief Sets cursor position on stdout.
    [[nodiscard]] IO::Types::Status setCursorPosition(Types::CursorPosition position, const Types::ControlOptions &options = {});

    /// @brief Sets cursor position on an output stream.
    [[nodiscard]] IO::Types::Status setCursorPosition(
        Types::OutputStream stream,
        Types::CursorPosition position,
        const Types::ControlOptions &options = {});

    /// @brief Queries cursor position on stdout.
    [[nodiscard]] Types::CursorPositionResult getCursorPosition(const Types::CursorPositionQueryOptions &options = {});

    /// @brief Queries cursor position on an output stream.
    [[nodiscard]] Types::CursorPositionResult getCursorPosition(Types::OutputStream stream, const Types::CursorPositionQueryOptions &options = {});

    /// @brief Saves cursor position on an output stream.
    [[nodiscard]] IO::Types::Status saveCursorPosition(
        Types::OutputStream stream = Types::OutputStream::Stdout,
        const Types::ControlOptions &options = {});

    /// @brief Restores cursor position on an output stream.
    [[nodiscard]] IO::Types::Status restoreCursorPosition(
        Types::OutputStream stream = Types::OutputStream::Stdout,
        const Types::ControlOptions &options = {});

    /// @brief Sets cursor visibility on stdout.
    [[nodiscard]] IO::Types::Status setCursorVisible(bool visible, const Types::ControlOptions &options = {});

    /// @brief Sets cursor visibility on an output stream.
    [[nodiscard]] IO::Types::Status setCursorVisible(Types::OutputStream stream, bool visible, const Types::ControlOptions &options = {});

    /// @brief Hides the cursor on stdout until the returned scope restores it.
    [[nodiscard]] CursorVisibilityScope scopedCursorHidden(const Types::ControlOptions &options = {}) noexcept;

    /// @brief Hides the cursor on an output stream until the returned scope restores it.
    [[nodiscard]] CursorVisibilityScope scopedCursorHidden(Types::OutputStream stream, const Types::ControlOptions &options = {}) noexcept;

    /// @brief Clears a screen or line region on stdout.
    [[nodiscard]] IO::Types::Status clear(Types::ClearTarget target = Types::ClearTarget::EntireScreen, const Types::ControlOptions &options = {});

    /// @brief Clears a screen or line region on an output stream.
    [[nodiscard]] IO::Types::Status clear(
        Types::OutputStream stream,
        Types::ClearTarget target = Types::ClearTarget::EntireScreen,
        const Types::ControlOptions &options = {});

    /// @brief Scrolls stdout.
    [[nodiscard]] IO::Types::Status scroll(Types::ScrollDirection direction, std::uint32_t lines = 1, const Types::ControlOptions &options = {});

    /// @brief Scrolls an output stream.
    [[nodiscard]] IO::Types::Status scroll(
        Types::OutputStream stream,
        Types::ScrollDirection direction,
        std::uint32_t lines = 1,
        const Types::ControlOptions &options = {});

    /// @brief Enters alternate screen mode on an output stream.
    [[nodiscard]] IO::Types::Status enterAlternateScreen(
        Types::OutputStream stream = Types::OutputStream::Stdout,
        const Types::ControlOptions &options = {});

    /// @brief Leaves alternate screen mode on an output stream.
    [[nodiscard]] IO::Types::Status leaveAlternateScreen(
        Types::OutputStream stream = Types::OutputStream::Stdout,
        const Types::ControlOptions &options = {});

    /// @brief Enters alternate screen mode on stdout until the returned scope leaves it.
    [[nodiscard]] AlternateScreenScope scopedAlternateScreen(const Types::ControlOptions &options = {}) noexcept;

    /// @brief Enters alternate screen mode on an output stream until the returned scope leaves it.
    [[nodiscard]] AlternateScreenScope scopedAlternateScreen(Types::OutputStream stream, const Types::ControlOptions &options = {}) noexcept;

    /// @brief Sets the terminal title through stdout.
    [[nodiscard]] IO::Types::Status setTitle(std::string_view utf8Title, const Types::ControlOptions &options = {});

    /// @brief Sets the terminal title through an output stream.
    [[nodiscard]] IO::Types::Status setTitle(Types::OutputStream stream, std::string_view utf8Title, const Types::ControlOptions &options = {});

    /// @brief Rings the terminal bell through an output stream.
    [[nodiscard]] IO::Types::Status ringBell(Types::OutputStream stream = Types::OutputStream::Stdout, const Types::ControlOptions &options = {});

    template <class... Args>
    IO::Types::Status Writer::print(std::format_string<Args...> format, Args &&...args) const
    {
        return print(Types::TextWriteOptions{}, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    IO::Types::Status Writer::print(const Types::TextWriteOptions &options, std::format_string<Args...> format, Args &&...args) const
    {
        return writeText(std::format(format, std::forward<Args>(args)...), options);
    }

    template <class... Args>
    IO::Types::Status Writer::println(std::format_string<Args...> format, Args &&...args) const
    {
        return println(Types::LineWriteOptions{}, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    IO::Types::Status Writer::println(const Types::LineWriteOptions &options, std::format_string<Args...> format, Args &&...args) const
    {
        return writeLine(std::format(format, std::forward<Args>(args)...), options);
    }

    template <class... Args>
    void OutputBuffer::print(std::format_string<Args...> format, Args &&...args)
    {
        std::format_to(std::back_inserter(text_), format, std::forward<Args>(args)...);
    }

    template <class... Args>
    void OutputBuffer::println(std::format_string<Args...> format, Args &&...args)
    {
        print(format, std::forward<Args>(args)...);
        appendLine();
    }

    template <class... Args>
    IO::Types::Status print(std::format_string<Args...> format, Args &&...args)
    {
        return print(Types::OutputStream::Stdout, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    IO::Types::Status print(Types::OutputStream stream, std::format_string<Args...> format, Args &&...args)
    {
        return print(stream, Types::TextWriteOptions{}, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    IO::Types::Status print(const Types::TextWriteOptions &options, std::format_string<Args...> format, Args &&...args)
    {
        return print(Types::OutputStream::Stdout, options, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    IO::Types::Status print(
        Types::OutputStream stream,
        const Types::TextWriteOptions &options,
        std::format_string<Args...> format,
        Args &&...args)
    {
        return writeText(stream, std::format(format, std::forward<Args>(args)...), options);
    }

    template <class... Args>
    IO::Types::Status println(std::format_string<Args...> format, Args &&...args)
    {
        return println(Types::OutputStream::Stdout, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    IO::Types::Status println(Types::OutputStream stream, std::format_string<Args...> format, Args &&...args)
    {
        return println(stream, Types::LineWriteOptions{}, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    IO::Types::Status println(const Types::LineWriteOptions &options, std::format_string<Args...> format, Args &&...args)
    {
        return println(Types::OutputStream::Stdout, options, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    IO::Types::Status println(
        Types::OutputStream stream,
        const Types::LineWriteOptions &options,
        std::format_string<Args...> format,
        Args &&...args)
    {
        return writeLine(stream, std::format(format, std::forward<Args>(args)...), options);
    }
} // namespace GameWIP::Terminal
