/// @file terminal.h
/// @brief Public API for the Terminal foundation library.

#pragma once

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "io/io.h"
#include "terminal/terminal_export.h"

/// @brief Platform-neutral UTF-8 standard-stream I/O, styling, and terminal control primitives.
/// @details The shared library owns process-wide stdin/stdout/stderr coordination. Expected terminal and backend failures
/// use IO statuses/results; selected caller-owned in-memory operations retain normal standard-library exception behavior.
namespace GameWIP::Terminal
{
    namespace Types
    {
        enum class BasicColor;
        struct Color;
        struct TextStyle;
        struct WriteSegment;
    } // namespace Types

    /// @brief Creates a basic terminal color.
    /// @param color Portable basic color to request.
    /// @return The requested color, or the terminal default color when color is not a known BasicColor value.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Color basicColor(Types::BasicColor color) noexcept;

    /// @brief Creates an RGB terminal color.
    /// @param red Red channel in the range [0, 255].
    /// @param green Green channel in the range [0, 255].
    /// @param blue Blue channel in the range [0, 255].
    /// @return The requested RGB color.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Color rgbColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept;

    /// @brief Creates a plain text write segment.
    /// @param text Caller-owned UTF-8 text.
    /// @return A non-owning segment that refers to text.
    /// @warning The referenced text must remain alive until the segment's writeSegments() call returns.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::WriteSegment textSegment(std::string_view text) noexcept;

    /// @brief Rejects temporary string storage that would leave the segment dangling.
    template <typename String>
        requires(std::same_as<std::remove_cvref_t<String>, std::string> && std::is_rvalue_reference_v<String &&>)
    [[nodiscard]] Types::WriteSegment textSegment(String &&text) noexcept = delete;

    /// @brief Creates a styled text write segment.
    /// @param text Caller-owned UTF-8 text.
    /// @param style Style copied into the segment.
    /// @return A non-owning segment that refers to text and owns a copy of style.
    /// @warning The referenced text must remain alive until the segment's writeSegments() call returns.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::WriteSegment styledTextSegment(std::string_view text, const Types::TextStyle &style) noexcept;

    /// @brief Rejects temporary string storage that would leave the segment dangling.
    template <typename String>
        requires(std::same_as<std::remove_cvref_t<String>, std::string> && std::is_rvalue_reference_v<String &&>)
    [[nodiscard]] Types::WriteSegment styledTextSegment(String &&text, const Types::TextStyle &style) noexcept = delete;

    /// @brief Creates a byte write segment.
    /// @param bytes Caller-owned bytes.
    /// @return A non-owning segment that refers to bytes.
    /// @warning The referenced bytes must remain alive until the segment's writeSegments() call returns.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::WriteSegment byteSegment(std::span<const std::byte> bytes) noexcept;

    /// @brief Rejects temporary contiguous storage that would leave the segment dangling.
    template <std::ranges::contiguous_range Range>
        requires(std::same_as<std::remove_cv_t<std::ranges::range_value_t<Range>>, std::byte> && std::is_rvalue_reference_v<Range &&> &&
                 !std::ranges::borrowed_range<Range>)
    [[nodiscard]] Types::WriteSegment byteSegment(Range &&bytes) noexcept = delete;

    /// @brief Sentinel timeout requesting an unbounded wait.
    /// @note The selected endpoint may still reject blocking or timeout behavior as Unsupported.
    inline constexpr std::chrono::milliseconds kWaitForever{-1};

    /// @brief Sentinel timeout requesting a non-blocking attempt.
    /// @note A stream without non-blocking/timed-read support returns Unsupported.
    inline constexpr std::chrono::milliseconds kNoWait{0};

    /// @brief Default timeout used by best-effort terminal control queries.
    inline constexpr std::chrono::milliseconds kDefaultQueryTimeout{100};

    /// @brief Default maximum UTF-8 byte count returned by one text read.
    /// @note Text reads preserve complete code points and can return SizeLimitExceeded when the next code point does not fit.
    inline constexpr std::uint64_t kDefaultMaxReturnedTextBytes = IO::kDefaultBufferSize;

    /// @brief Default maximum UTF-8 byte count returned by one line read.
    /// @note The limit applies to the returned representation after the selected line-ending policy.
    inline constexpr std::uint64_t kDefaultMaxReturnedLineBytes = std::uint64_t{64} * 1024;

    /// @brief Terminal stream, styling, input, and result types.
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

        /// @brief Input representation selected for one managed terminal input owner.
        enum class InputDeliveryMode : std::uint8_t
        {
            /// @brief Deliver normalized logical key, paste, and resize events where supported.
            Events,

            /// @brief Deliver byte, UTF-8 text, and managed line input.
            Stream
        };

        /// @brief Policy for platform terminal control-key processing.
        enum class ControlKeyMode : std::uint8_t
        {
            /// @brief Preserve normal platform processing such as ordinary Ctrl+C interrupt behavior.
            NativeProcessing,

            /// @brief Request reportable control-key combinations as logical terminal input.
            ReportAsInput
        };

        /// @brief One valid Unicode scalar reported as a logical terminal key.
        struct CharacterKey
        {
            /// @brief Unicode scalar value. Terminal-produced values are never surrogate code points.
            char32_t value = U'\0';

            friend constexpr bool operator==(CharacterKey, CharacterKey) noexcept = default;
        };

        /// @brief Portable non-character key reported by terminal input.
        enum class NamedKey : std::uint8_t
        {
            Backspace, ///< Backspace key.
            Tab,       ///< Tab key. Shift+Tab is represented by the Shift modifier.
            Enter,     ///< Enter or Return key.
            Escape,    ///< Escape key.
            Insert,    ///< Insert key.
            Delete,    ///< Delete key.
            Home,      ///< Home key.
            End,       ///< End key.
            PageUp,    ///< Page Up key.
            PageDown,  ///< Page Down key.
            ArrowUp,   ///< Up-arrow key.
            ArrowDown, ///< Down-arrow key.
            ArrowLeft, ///< Left-arrow key.
            ArrowRight, ///< Right-arrow key.
            Begin,     ///< Begin or keypad-center navigation key where reportable.
            CapsLock,  ///< Caps Lock key transition where reportable.
            NumLock,   ///< Num Lock key transition where reportable.
            ScrollLock, ///< Scroll Lock key transition where reportable.
            PrintScreen, ///< Print Screen key where reportable.
            Pause,       ///< Pause/Break key where reportable.
            Menu         ///< Menu/context key where reportable.
        };

        /// @brief Numeric function key without an arbitrary public upper bound.
        struct FunctionKey
        {
            /// @brief One-based function-key number. Terminal-produced values are always nonzero.
            std::uint16_t number = 0;

            friend constexpr bool operator==(FunctionKey, FunctionKey) noexcept = default;
        };

        /// @brief Standalone logical modifier key where the backend can report modifier transitions.
        enum class ModifierKey : std::uint8_t
        {
            Shift,   ///< Shift modifier key.
            Control, ///< Control modifier key.
            Alt,     ///< Alt modifier key.
            Super,   ///< Super-style modifier key.
            Hyper,   ///< Hyper modifier key where reportable.
            Meta     ///< Meta modifier key where reportable.
        };

        /// @brief Logical media key where the backend can report media-key input.
        enum class MediaKey : std::uint8_t
        {
            Play,          ///< Start playback.
            Pause,         ///< Pause playback.
            PlayPause,     ///< Toggle playback/pause.
            Stop,          ///< Stop playback.
            NextTrack,     ///< Select the next track/item.
            PreviousTrack, ///< Select the previous track/item.
            FastForward,   ///< Fast-forward media.
            Rewind,        ///< Rewind media.
            Record,        ///< Start or toggle recording.
            VolumeUp,      ///< Increase output volume.
            VolumeDown,    ///< Decrease output volume.
            Mute           ///< Toggle or request mute.
        };

        /// @brief Modifier and lock state accompanying a logical key event.
        enum class KeyModifier : std::uint16_t
        {
            None = 0,             ///< No known modifier or lock state.
            Shift = 1U << 0U,     ///< Shift is active.
            Control = 1U << 1U,   ///< Control is active.
            Alt = 1U << 2U,       ///< Alt is active.
            Super = 1U << 3U,     ///< Super-style modifier is active.
            Hyper = 1U << 4U,     ///< Hyper is active where reportable.
            Meta = 1U << 5U,      ///< Meta is active where reportable.
            CapsLock = 1U << 6U,  ///< Caps Lock state is active where reportable.
            NumLock = 1U << 7U,   ///< Num Lock state is active where reportable.
            ScrollLock = 1U << 8U ///< Scroll Lock state is active where reportable.
        };

        /// @brief Combines modifier-state bits.
        [[nodiscard]] constexpr KeyModifier operator|(KeyModifier left, KeyModifier right) noexcept
        {
            return static_cast<KeyModifier>(static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right));
        }

        /// @brief Intersects modifier-state bits.
        [[nodiscard]] constexpr KeyModifier operator&(KeyModifier left, KeyModifier right) noexcept
        {
            return static_cast<KeyModifier>(static_cast<std::uint16_t>(left) & static_cast<std::uint16_t>(right));
        }

        /// @brief Adds modifier-state bits to an existing mask.
        constexpr KeyModifier &operator|=(KeyModifier &left, KeyModifier right) noexcept
        {
            left = left | right;
            return left;
        }

        /// @brief Returns whether every requested modifier bit is present.
        [[nodiscard]] constexpr bool hasModifier(KeyModifier modifiers, KeyModifier requested) noexcept
        {
            return (modifiers & requested) == requested;
        }

        /// @brief Logical phase of one reportable key transition.
        enum class KeyAction : std::uint8_t
        {
            Press,  ///< Initial logical key press.
            Repeat, ///< Auto-repeat occurrence where distinguishable.
            Release ///< Logical key release where reportable.
        };

        /// @brief Logical location of a key where the backend can distinguish it.
        enum class KeyLocation : std::uint8_t
        {
            Unknown,  ///< Location information is unavailable.
            Standard, ///< Ordinary non-side-specific key location.
            Left,     ///< Left-side modifier location.
            Right,    ///< Right-side modifier location.
            Numpad    ///< Numeric-keypad location.
        };

        /// @brief Portable logical terminal key representation.
        using Key = std::variant<CharacterKey, NamedKey, FunctionKey, ModifierKey, MediaKey>;

        /// @brief One normalized logical key input event.
        /// @details For standalone ModifierKey events, modifiers describes other active modifiers and does not duplicate key.
        struct KeyEvent
        {
            Key key;                                      ///< Logical key reported by the terminal.
            KeyModifier modifiers = KeyModifier::None;    ///< Active modifier/lock state known for this event.
            KeyAction action = KeyAction::Press;          ///< Press/repeat/release phase.
            KeyLocation location = KeyLocation::Unknown;  ///< Location, or Unknown when unavailable.
            std::uint32_t repeatCount = 1;                ///< Number of occurrences represented by a Repeat event.
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

            /// @brief Require the requested styling and fail with Unsupported when it is unavailable.
            Required
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
            /// @brief Normal black.
            Black,
            /// @brief Normal red.
            Red,
            /// @brief Normal green.
            Green,
            /// @brief Normal yellow.
            Yellow,
            /// @brief Normal blue.
            Blue,
            /// @brief Normal magenta.
            Magenta,
            /// @brief Normal cyan.
            Cyan,
            /// @brief Normal white.
            White,

            /// @brief Bright black, commonly rendered as gray.
            BrightBlack,
            /// @brief Bright red.
            BrightRed,
            /// @brief Bright green.
            BrightGreen,
            /// @brief Bright yellow.
            BrightYellow,
            /// @brief Bright blue.
            BrightBlue,
            /// @brief Bright magenta.
            BrightMagenta,
            /// @brief Bright cyan.
            BrightCyan,
            /// @brief Bright white.
            BrightWhite
        };

        /// @brief Portable terminal color request.
        struct Color
        {
            /// @brief Creates the terminal default color.
            Color() noexcept = default;

            /// @brief Returns the stored color representation.
            [[nodiscard]] ColorKind kind() const noexcept
            {
                return kind_;
            }

            /// @brief Returns the stored basic color.
            /// @note Meaningful only when kind() is ColorKind::Basic.
            [[nodiscard]] BasicColor basic() const noexcept
            {
                return basic_;
            }

            /// @brief Returns the stored red channel.
            /// @note Meaningful only when kind() is ColorKind::Rgb.
            [[nodiscard]] std::uint8_t red() const noexcept
            {
                return red_;
            }

            /// @brief Returns the stored green channel.
            /// @note Meaningful only when kind() is ColorKind::Rgb.
            [[nodiscard]] std::uint8_t green() const noexcept
            {
                return green_;
            }

            /// @brief Returns the stored blue channel.
            /// @note Meaningful only when kind() is ColorKind::Rgb.
            [[nodiscard]] std::uint8_t blue() const noexcept
            {
                return blue_;
            }

        private:
            friend GAMEWIP_TERMINAL_EXPORT Color GameWIP::Terminal::basicColor(Types::BasicColor color) noexcept;
            friend GAMEWIP_TERMINAL_EXPORT Color GameWIP::Terminal::rgbColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept;

            explicit Color(BasicColor color) noexcept;
            Color(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept;

            ColorKind kind_ = ColorKind::Default;
            BasicColor basic_ = BasicColor::White;
            std::uint8_t red_ = 0;
            std::uint8_t green_ = 0;
            std::uint8_t blue_ = 0;
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
        struct InputCapabilitiesResult
        {
            /// @brief Operation status.
            IO::Types::Status status;

            /// @brief Reported input capabilities.
            InputCapabilities capabilities;
        };

        /// @brief Result returned by output capability queries.
        struct OutputCapabilitiesResult
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

        /// @brief Recognized paste input delivered as one bounded owned UTF-8 payload.
        struct PasteEvent
        {
            /// @brief Valid UTF-8 paste payload assembled by the managed input backend.
            std::string text;
        };

        /// @brief Terminal-size change delivered through structured event input.
        struct ResizeEvent
        {
            /// @brief Resulting terminal dimensions in character cells.
            TerminalSize size;
        };

        /// @brief Tagged payload for one portable Terminal event.
        using EventData = std::variant<KeyEvent, PasteEvent, ResizeEvent>;

        /// @brief One normalized pull-based Terminal input event.
        /// @details Key and resize alternatives use only inline value storage. Paste owns bounded UTF-8 text.
        struct Event
        {
            EventData data; ///< Typed event payload.

            /// @brief Returns the payload when it has EventType, otherwise nullptr.
            template <typename EventType> [[nodiscard]] EventType *getIf() noexcept
            {
                return std::get_if<EventType>(&data);
            }

            /// @brief Returns the payload when it has EventType, otherwise nullptr.
            template <typename EventType> [[nodiscard]] const EventType *getIf() const noexcept
            {
                return std::get_if<EventType>(&data);
            }
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
            /// @brief Move toward smaller row coordinates.
            Up,
            /// @brief Move toward larger row coordinates.
            Down,
            /// @brief Move toward smaller column coordinates.
            Left,
            /// @brief Move toward larger column coordinates.
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
            /// @brief Scroll terminal contents upward.
            Up,
            /// @brief Scroll terminal contents downward.
            Down
        };

        /// @brief Options used by byte reads.
        struct ByteReadOptions
        {
            /// @brief Negative waits forever, zero does not wait, positive waits up to that duration.
            /// @note A finite timeout may return Unsupported when the input capabilities do not advertise timeout support.
            std::chrono::milliseconds timeout = kWaitForever;

            /// @brief Whether a successful read may return fewer bytes than requested.
            bool allowPartial = true;
        };

        /// @brief Options used by text reads.
        /// @details Text reads return one available UTF-8 chunk after input becomes available.
        struct TextReadOptions
        {
            /// @brief Negative waits forever, zero does not wait, positive waits up to that duration.
            /// @note A finite timeout may return Unsupported when the input capabilities do not advertise timeout support.
            std::chrono::milliseconds timeout = kWaitForever;

            /// @brief Maximum accepted byte count for one available UTF-8 text chunk.
            std::uint64_t maxReturnedBytes = kDefaultMaxReturnedTextBytes;
        };

        /// @brief Options used by line reads.
        /// @details Line reads do not expose allowPartial; they stop at a line ending, terminating outcome, truncation, or failure.
        struct LineReadOptions
        {
            /// @brief Negative waits forever, zero does not wait, positive waits up to that duration.
            /// @note A finite timeout may return Unsupported when the input capabilities do not advertise timeout support.
            std::chrono::milliseconds timeout = kWaitForever;

            /// @brief Maximum accepted byte count for the returned line representation.
            std::uint64_t maxReturnedBytes = kDefaultMaxReturnedLineBytes;

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
            /// @note A backend may answer directly without waiting for an input-stream response.
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
        /// @details bytesRead can preserve partial progress together with a terminating outcome or later failure.
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
        /// @details A non-empty text payload represents one completed UTF-8 chunk. Other outcomes describe why no chunk completed.
        struct TextReadResult
        {
            /// @brief Operation status.
            IO::Types::Status status;

            /// @brief Read outcome.
            ReadOutcome outcome = ReadOutcome::Completed;

            /// @brief UTF-8 text bytes read.
            std::string text;

            /// @brief True when maxReturnedBytes limited the returned text.
            bool wasTruncated = false;
        };

        /// @brief Result returned by line reads.
        /// @details line can contain an unterminated UTF-8 prefix together with EndOfStream, TimedOut, or WouldBlock.
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

            /// @brief True when maxReturnedBytes limited the returned line.
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
        /// @details Text and byte payloads are non-owning views. Copying a segment copies those views rather than the
        /// referenced storage. Caller-owned payload storage must remain valid and unchanged until writeSegments() returns.
        /// TextStyle is copied into styled segments.
        struct WriteSegment
        {
            /// @brief Returns the segment kind.
            [[nodiscard]] WriteSegmentKind kind() const noexcept
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
            [[nodiscard]] const TextStyle &style() const noexcept
            {
                return style_;
            }

        private:
            friend GAMEWIP_TERMINAL_EXPORT WriteSegment GameWIP::Terminal::textSegment(std::string_view text) noexcept;
            friend GAMEWIP_TERMINAL_EXPORT WriteSegment
            GameWIP::Terminal::styledTextSegment(std::string_view text, const Types::TextStyle &style) noexcept;
            friend GAMEWIP_TERMINAL_EXPORT WriteSegment GameWIP::Terminal::byteSegment(std::span<const std::byte> bytes) noexcept;

            WriteSegment(WriteSegmentKind kind, std::string_view text, std::span<const std::byte> bytes, const TextStyle &style) noexcept;

            WriteSegmentKind kind_ = WriteSegmentKind::Text;
            std::string_view text_;
            std::span<const std::byte> bytes_;
            TextStyle style_{};
        };
    } // namespace Types

    /// @brief Creates a terminal default color.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Color defaultColor() noexcept;

    /// @brief Creates an input mode from a preset.
    /// @param preset Preset to convert.
    /// @return The requested mode. Unknown preset values fall back to InteractiveLine.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::InputMode makeInputMode(Types::InputModePreset preset) noexcept;

    /// @brief Movable, non-copyable RAII helper that restores a complete previous backend terminal input mode.
    /// @details A failed setup produces an inactive scope carrying the setup status. Failed explicit restoration leaves the
    /// scope active for retry. Move assignment does not consume its source when restoring the destination's current state fails.
    class GAMEWIP_TERMINAL_EXPORT InputModeScope final
    {
    public:
        /// @brief Creates an inactive input mode scope.
        InputModeScope() noexcept;

        /// @brief Input-mode restoration responsibility cannot be copied.
        InputModeScope(const InputModeScope &) = delete;
        /// @brief Input-mode restoration responsibility cannot be copy-assigned.
        InputModeScope &operator=(const InputModeScope &) = delete;

        /// @brief Move-constructs the scope and transfers restoration responsibility.
        InputModeScope(InputModeScope &&other) noexcept;

        /// @brief Restores any destination-owned mode, then transfers restoration responsibility.
        /// @note If destination restoration fails, this scope remains active and other is not consumed.
        InputModeScope &operator=(InputModeScope &&other) noexcept;

        /// @brief Restores the complete previous backend input mode on a best-effort basis without throwing.
        ~InputModeScope() noexcept;

        /// @brief Returns whether this scope still owns restoration responsibility.
        [[nodiscard]] bool active() const noexcept;

        /// @brief Returns the last setup or restoration status tracked by this scope.
        [[nodiscard]] const IO::Types::Status &status() const noexcept;

        /// @brief Restores the complete previous backend input mode and reports restoration failure.
        [[nodiscard]] IO::Types::Status restore() noexcept;

        /// @brief Releases restoration responsibility without changing input mode.
        void release() noexcept;

    private:
        friend GAMEWIP_TERMINAL_EXPORT InputModeScope scopedInputMode(Types::InputStream stream, const Types::InputMode &mode) noexcept;

        Types::InputStream stream_ = Types::InputStream::Stdin;
        Types::InputMode previousMode_{};
        std::uint64_t previousNativeMode_ = 0;
        IO::Types::Status status_{};
        bool hasPreviousNativeMode_ = false;
        bool active_ = false;
    };

    /// @brief Movable, non-copyable RAII helper that leaves alternate screen mode when destroyed.
    /// @details Setup failure produces an inactive scope. Failed explicit leave remains active for retry. Nesting is coordinated
    /// per output stream; do not mix manual transitions with active scopes for that stream.
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
        friend GAMEWIP_TERMINAL_EXPORT AlternateScreenScope
        scopedAlternateScreen(Types::OutputStream stream, const Types::ControlOptions &options) noexcept;

        Types::OutputStream stream_ = Types::OutputStream::Stdout;
        Types::ControlOptions options_{};
        IO::Types::Status status_{};
        bool active_ = false;
    };

    /// @brief Movable, non-copyable RAII helper that restores cursor visibility when destroyed.
    /// @details Setup failure produces an inactive scope. Failed explicit restoration remains active for retry. Nesting is
    /// coordinated per output stream; do not mix manual visibility changes with active scopes for that stream.
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
        friend GAMEWIP_TERMINAL_EXPORT CursorHiddenScope
        scopedCursorHidden(Types::OutputStream stream, const Types::ControlOptions &options) noexcept;

        Types::OutputStream stream_ = Types::OutputStream::Stdout;
        Types::ControlOptions options_{};
        IO::Types::Status status_{};
        bool active_ = false;
    };

    /// @brief Reusable caller-owned plain-text buffer for batching one Terminal write.
    /// @details The object owns its string storage and is not internally synchronized. It does not validate UTF-8 while
    /// appending. Formatting and storage operations retain normal std::string/std::format exception behavior.
    class GAMEWIP_TERMINAL_EXPORT OutputBuffer final
    {
    public:
        /// @brief Creates an empty output buffer using a line ending for appendLine() and println().
        /// @throws std::invalid_argument when lineEnding is not a known Types::LineEnding value.
        explicit OutputBuffer(Types::LineEnding lineEnding = Types::LineEnding::Native);

        /// @brief Reserves text storage for future appends.
        /// @throws Any exception propagated by std::string::reserve().
        void reserve(std::size_t bytes);

        /// @brief Clears buffered text while retaining capacity.
        void clear() noexcept;

        /// @brief Returns whether the buffer is empty.
        [[nodiscard]] bool empty() const noexcept;

        /// @brief Returns buffered text size in bytes.
        [[nodiscard]] std::size_t size() const noexcept;

        /// @brief Returns a non-owning view of buffered text.
        /// @warning Mutating, moving, assigning, or destroying this buffer can invalidate the view.
        [[nodiscard]] std::string_view text() const noexcept;

        /// @brief Appends bytes expected to contain UTF-8 text.
        /// @throws Any exception propagated by std::string::append().
        void appendText(std::string_view utf8Text);

        /// @brief Appends bytes expected to contain UTF-8 text followed by the configured line ending.
        /// @throws Any exception propagated by std::string::append().
        void appendLine(std::string_view utf8Text = {});

        /// @brief Formats text and appends it to the buffer.
        /// @throws std::format_error or any exception propagated by formatting, custom formatters, or allocation.
        template <class... Args> void print(std::format_string<Args...> format, Args &&...args);

        /// @brief Formats text and appends it followed by the configured line ending.
        /// @throws std::format_error or any exception propagated by formatting, custom formatters, or allocation.
        template <class... Args> void println(std::format_string<Args...> format, Args &&...args);

        /// @brief Writes buffered text to stdout without clearing the buffer.
        [[nodiscard]] IO::Types::Status writeTo(const Types::TextWriteOptions &options = {}) const;

        /// @brief Writes buffered text to an output stream without clearing the buffer.
        [[nodiscard]] IO::Types::Status writeTo(Types::OutputStream stream, const Types::TextWriteOptions &options = {}) const;

        /// @brief Writes buffered text to stdout and clears it only when the write succeeds.
        /// @note The name describes buffer clearing; a backend flush is requested only when options.flushMode is not None.
        [[nodiscard]] IO::Types::Status flushTo(const Types::TextWriteOptions &options = {});

        /// @brief Writes buffered text to an output stream and clears it only when the write succeeds.
        /// @note The name describes buffer clearing; a backend flush is requested only when options.flushMode is not None.
        [[nodiscard]] IO::Types::Status flushTo(Types::OutputStream stream, const Types::TextWriteOptions &options = {});

    private:
        std::string text_;
        Types::LineEnding lineEnding_ = Types::LineEnding::Native;
    };

    /// @brief Gets a snapshot of capabilities for stdin.
    /// @return Status and capabilities observed for the current stdin endpoint. A successful Detached result is possible.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::InputCapabilitiesResult getInputCapabilities();

    /// @brief Gets capabilities for an input stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::InputCapabilitiesResult getInputCapabilities(Types::InputStream stream);

    /// @brief Observes a snapshot of currently active stdout capabilities without preparing the stream.
    /// @return Status and capabilities observed for the current stdout endpoint. Later endpoint changes can stale the snapshot.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::OutputCapabilitiesResult getOutputCapabilities();

    /// @brief Observes currently active capabilities without preparing the stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::OutputCapabilitiesResult getOutputCapabilities(Types::OutputStream stream);

    /// @brief Enables stdout support required by styling and terminal controls.
    /// @details Preparation is idempotent. Redirected streams need no setup, detached streams report NotOpen,
    /// and styled writes or controls prepare lazily when needed.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::OutputCapabilitiesResult prepareOutput();

    /// @brief Enables stream support required by styling and terminal controls.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::OutputCapabilitiesResult prepareOutput(Types::OutputStream stream);

    /// @brief Gets terminal size for stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::TerminalSizeResult getTerminalSize();

    /// @brief Gets terminal size for an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::TerminalSizeResult getTerminalSize(Types::OutputStream stream);

    /// @brief Checks whether input is available on stdin.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::InputAvailabilityResult getInputAvailability();

    /// @brief Checks whether input is available on an input stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::InputAvailabilityResult getInputAvailability(Types::InputStream stream);

    /// @brief Gets the current mode for stdin.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::InputModeResult getInputMode();

    /// @brief Gets the current mode for an input stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::InputModeResult getInputMode(Types::InputStream stream);

    /// @brief Sets the mode for stdin without discarding Terminal-buffered input.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status setInputMode(const Types::InputMode &mode);

    /// @brief Sets the mode for an input stream without discarding Terminal-buffered input.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status setInputMode(Types::InputStream stream, const Types::InputMode &mode);

    /// @brief Restores the backend/default mode for stdin.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status restoreDefaultInputMode();

    /// @brief Restores the backend/default mode for an input stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status restoreDefaultInputMode(Types::InputStream stream);

    /// @brief Temporarily sets stdin mode and returns a restoration scope.
    /// @return Active scope on success; inactive scope carrying setup failure in status() otherwise.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT InputModeScope scopedInputMode(const Types::InputMode &mode) noexcept;

    /// @brief Temporarily sets an input stream mode and returns a restoration scope.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT InputModeScope scopedInputMode(Types::InputStream stream, const Types::InputMode &mode) noexcept;

    /// @brief Reads one UTF-8 line from stdin.
    /// @return Status, stopping outcome, returned line, consumed ending, and truncation state. Partial line text may accompany
    /// EndOfStream, TimedOut, WouldBlock, or a later failure.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::LineReadResult readLine(const Types::LineReadOptions &options = {});

    /// @brief Reads one line from an input stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::LineReadResult readLine(Types::InputStream stream, const Types::LineReadOptions &options = {});

    /// @brief Reads one available complete UTF-8 text chunk from stdin.
    /// @return Status, stopping outcome, text, and truncation state. Size limits never split a valid UTF-8 code point.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::TextReadResult readText(const Types::TextReadOptions &options = {});

    /// @brief Reads one available UTF-8 text chunk from an input stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::TextReadResult readText(Types::InputStream stream, const Types::TextReadOptions &options = {});

    /// @brief Reads bytes from stdin into caller storage.
    /// @return Status, stopping outcome, and bytes copied. Partial progress may accompany a later failure or terminating outcome.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::ByteReadResult readBytes(
        std::span<std::byte> outputBuffer,
        const Types::ByteReadOptions &options = {});

    /// @brief Reads bytes from an input stream into caller storage.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::ByteReadResult readBytes(
        Types::InputStream stream,
        std::span<std::byte> outputBuffer,
        const Types::ByteReadOptions &options = {});

    /// @brief Writes UTF-8 text to stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status writeText(std::string_view utf8Text, const Types::TextWriteOptions &options = {});

    /// @brief Writes UTF-8 text to an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status writeText(
        Types::OutputStream stream,
        std::string_view utf8Text,
        const Types::TextWriteOptions &options = {});

    /// @brief Writes UTF-8 text followed by a line ending to stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status writeLine(std::string_view utf8Text = {}, const Types::LineWriteOptions &options = {});

    /// @brief Writes UTF-8 text followed by a line ending to an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status writeLine(
        Types::OutputStream stream,
        std::string_view utf8Text = {},
        const Types::LineWriteOptions &options = {});

    /// @brief Writes bytes to stdout where the endpoint supports raw byte output.
    /// @return Accepted-byte count and status. A requested flush can fail after the full byte count was accepted.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::WriteResult writeBytes(
        std::span<const std::byte> bytes,
        const Types::ByteWriteOptions &options = {});

    /// @brief Writes bytes to an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::WriteResult writeBytes(
        Types::OutputStream stream,
        std::span<const std::byte> bytes,
        const Types::ByteWriteOptions &options = {});

    /// @brief Writes text, styled text, and byte segments to stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status writeSegments(
        std::span<const Types::WriteSegment> segments,
        const Types::SegmentWriteOptions &options = {});

    /// @brief Writes text, styled text, and byte segments to an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status writeSegments(
        Types::OutputStream stream,
        std::span<const Types::WriteSegment> segments,
        const Types::SegmentWriteOptions &options = {});

    /// @brief Formats text and writes it to stdout.
    /// @details Formatting occurs before final stdout serialization. Formatting-stage failures are converted to IO statuses.
    template <class... Args> [[nodiscard]] IO::Types::Status print(std::format_string<Args...> format, Args &&...args);

    /// @brief Formats text and writes it to an output stream.
    template <class... Args> [[nodiscard]] IO::Types::Status print(Types::OutputStream stream, std::format_string<Args...> format, Args &&...args);

    /// @brief Formats text and writes it to stdout.
    template <class... Args>
    [[nodiscard]] IO::Types::Status print(const Types::TextWriteOptions &options, std::format_string<Args...> format, Args &&...args);

    /// @brief Formats text and writes it to an output stream.
    template <class... Args>
    [[nodiscard]] IO::Types::Status print(
        Types::OutputStream stream,
        const Types::TextWriteOptions &options,
        std::format_string<Args...> format,
        Args &&...args);

    /// @brief Formats text and writes it followed by a line ending to stdout.
    /// @details Formatting occurs before final stdout serialization. Formatting-stage failures are converted to IO statuses.
    template <class... Args> [[nodiscard]] IO::Types::Status println(std::format_string<Args...> format, Args &&...args);

    /// @brief Formats text and writes it followed by a line ending to an output stream.
    template <class... Args> [[nodiscard]] IO::Types::Status println(Types::OutputStream stream, std::format_string<Args...> format, Args &&...args);

    /// @brief Formats text and writes it followed by a line ending to stdout.
    template <class... Args>
    [[nodiscard]] IO::Types::Status println(const Types::LineWriteOptions &options, std::format_string<Args...> format, Args &&...args);

    /// @brief Formats text and writes it followed by a line ending to an output stream.
    template <class... Args>
    [[nodiscard]] IO::Types::Status println(
        Types::OutputStream stream,
        const Types::LineWriteOptions &options,
        std::format_string<Args...> format,
        Args &&...args);

    /// @brief Flushes stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status flush(IO::Types::FlushMode mode = IO::Types::FlushMode::Data);

    /// @brief Flushes an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status flush(Types::OutputStream stream, IO::Types::FlushMode mode = IO::Types::FlushMode::Data);

    /// @brief Resets style on stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status resetStyle(const Types::ControlOptions &options = {});

    /// @brief Resets style on an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status resetStyle(Types::OutputStream stream, const Types::ControlOptions &options = {});

    /// @brief Moves the cursor on stdout.
    /// @note A zero amount emits no control sequence but can still honor options.flushMode.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status moveCursor(
        Types::CursorMoveDirection direction,
        std::uint32_t amount = 1,
        const Types::ControlOptions &options = {});

    /// @brief Moves the cursor on an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status moveCursor(
        Types::OutputStream stream,
        Types::CursorMoveDirection direction,
        std::uint32_t amount = 1,
        const Types::ControlOptions &options = {});

    /// @brief Sets cursor position on stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status setCursorPosition(
        Types::CursorPosition position,
        const Types::ControlOptions &options = {});

    /// @brief Sets cursor position on an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status setCursorPosition(
        Types::OutputStream stream,
        Types::CursorPosition position,
        const Types::ControlOptions &options = {});

    /// @brief Queries cursor position on stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::CursorPositionResult getCursorPosition(const Types::CursorPositionQueryOptions &options = {});

    /// @brief Queries cursor position through an output stream and reads protocol responses from an input stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::CursorPositionResult getCursorPosition(
        Types::OutputStream outputStream,
        Types::InputStream responseStream,
        const Types::CursorPositionQueryOptions &options = {});

    /// @brief Saves cursor position on stdout.
    /// @note Saved position is backend state, not a guaranteed stack; a later save can replace it.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status saveCursorPosition(const Types::ControlOptions &options = {});

    /// @brief Saves cursor position on an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status saveCursorPosition(Types::OutputStream stream, const Types::ControlOptions &options = {});

    /// @brief Restores cursor position on stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status restoreCursorPosition(const Types::ControlOptions &options = {});

    /// @brief Restores cursor position on an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status restoreCursorPosition(
        Types::OutputStream stream,
        const Types::ControlOptions &options = {});

    /// @brief Sets cursor visibility on stdout.
    /// @warning Do not mix manual visibility changes with active CursorHiddenScope objects for stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status setCursorVisible(bool visible, const Types::ControlOptions &options = {});

    /// @brief Sets cursor visibility on an output stream.
    /// @warning Do not mix manual visibility changes with active CursorHiddenScope objects for the same stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status setCursorVisible(
        Types::OutputStream stream,
        bool visible,
        const Types::ControlOptions &options = {});

    /// @brief Hides the cursor on stdout until the returned scope restores it.
    /// @warning Do not use manual visibility changes while the returned scope is active.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT CursorHiddenScope scopedCursorHidden(const Types::ControlOptions &options = {}) noexcept;

    /// @brief Hides the cursor on an output stream until the returned scope restores it.
    /// @warning Do not use manual visibility changes for the same stream while the returned scope is active.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT CursorHiddenScope
    scopedCursorHidden(Types::OutputStream stream, const Types::ControlOptions &options = {}) noexcept;

    /// @brief Clears a screen or line region on stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status clear(
        Types::ClearTarget target = Types::ClearTarget::EntireScreen,
        const Types::ControlOptions &options = {});

    /// @brief Clears a screen or line region on an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status clear(
        Types::OutputStream stream,
        Types::ClearTarget target = Types::ClearTarget::EntireScreen,
        const Types::ControlOptions &options = {});

    /// @brief Scrolls stdout.
    /// @note A zero line count emits no control sequence but can still honor options.flushMode.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status scroll(
        Types::ScrollDirection direction,
        std::uint32_t lines = 1,
        const Types::ControlOptions &options = {});

    /// @brief Scrolls an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status scroll(
        Types::OutputStream stream,
        Types::ScrollDirection direction,
        std::uint32_t lines = 1,
        const Types::ControlOptions &options = {});

    /// @brief Enters alternate screen mode on stdout.
    /// @warning Do not mix manual transitions with active AlternateScreenScope objects for stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status enterAlternateScreen(const Types::ControlOptions &options = {});

    /// @brief Enters alternate screen mode on an output stream.
    /// @warning Do not mix manual transitions with active AlternateScreenScope objects for the same stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status enterAlternateScreen(
        Types::OutputStream stream,
        const Types::ControlOptions &options = {});

    /// @brief Leaves alternate screen mode on stdout.
    /// @warning Do not mix manual transitions with active AlternateScreenScope objects for stdout.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status leaveAlternateScreen(const Types::ControlOptions &options = {});

    /// @brief Leaves alternate screen mode on an output stream.
    /// @warning Do not mix manual transitions with active AlternateScreenScope objects for the same stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status leaveAlternateScreen(
        Types::OutputStream stream,
        const Types::ControlOptions &options = {});

    /// @brief Enters alternate screen mode on stdout until the returned scope leaves it.
    /// @warning Do not use manual alternate-screen transitions while the returned scope is active.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT AlternateScreenScope scopedAlternateScreen(const Types::ControlOptions &options = {}) noexcept;

    /// @brief Enters alternate screen mode on an output stream until the returned scope leaves it.
    /// @warning Do not use manual alternate-screen transitions for the same stream while the returned scope is active.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT AlternateScreenScope
    scopedAlternateScreen(Types::OutputStream stream, const Types::ControlOptions &options = {}) noexcept;

    /// @brief Sets the terminal title through stdout.
    /// @details Backend limits and sanitization apply; the current Win32 backend replaces C0 controls and DEL with spaces.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status setTitle(std::string_view utf8Title, const Types::ControlOptions &options = {});

    /// @brief Sets the terminal title through an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status setTitle(
        Types::OutputStream stream,
        std::string_view utf8Title,
        const Types::ControlOptions &options = {});

    /// @brief Emits the terminal bell control through stdout.
    /// @note Terminal or user settings decide whether the control produces an audible sound.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status ringBell(const Types::ControlOptions &options = {});

    /// @brief Rings the terminal bell through an output stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status ringBell(Types::OutputStream stream, const Types::ControlOptions &options = {});

    /// @cond INTERNAL_TERMINAL_DETAIL
    namespace Detail
    {
        /// @brief Exported ABI bridge used by the public print() templates.
        /// @warning Internal support symbol; consumers must call print() instead.
        [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status vprint(
            Types::OutputStream stream,
            const Types::TextWriteOptions &options,
            std::string_view format,
            std::format_args arguments);

        /// @brief Exported ABI bridge used by the public println() templates.
        /// @warning Internal support symbol; consumers must call println() instead.
        [[nodiscard]] GAMEWIP_TERMINAL_EXPORT IO::Types::Status vprintln(
            Types::OutputStream stream,
            const Types::LineWriteOptions &options,
            std::string_view format,
            std::format_args arguments);
    } // namespace Detail
    /// @endcond

    template <class... Args> void OutputBuffer::print(std::format_string<Args...> format, Args &&...args)
    {
        std::format_to(std::back_inserter(text_), format, std::forward<Args>(args)...);
    }

    template <class... Args> void OutputBuffer::println(std::format_string<Args...> format, Args &&...args)
    {
        print(format, std::forward<Args>(args)...);
        appendLine();
    }

    template <class... Args> IO::Types::Status print(std::format_string<Args...> format, Args &&...args)
    {
        return print(Types::OutputStream::Stdout, format, std::forward<Args>(args)...);
    }

    template <class... Args> IO::Types::Status print(Types::OutputStream stream, std::format_string<Args...> format, Args &&...args)
    {
        return print(stream, Types::TextWriteOptions{}, format, std::forward<Args>(args)...);
    }

    template <class... Args> IO::Types::Status print(const Types::TextWriteOptions &options, std::format_string<Args...> format, Args &&...args)
    {
        return print(Types::OutputStream::Stdout, options, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    IO::Types::Status print(Types::OutputStream stream, const Types::TextWriteOptions &options, std::format_string<Args...> format, Args &&...args)
    {
        return Detail::vprint(stream, options, format.get(), std::make_format_args(args...));
    }

    template <class... Args> IO::Types::Status println(std::format_string<Args...> format, Args &&...args)
    {
        return println(Types::OutputStream::Stdout, format, std::forward<Args>(args)...);
    }

    template <class... Args> IO::Types::Status println(Types::OutputStream stream, std::format_string<Args...> format, Args &&...args)
    {
        return println(stream, Types::LineWriteOptions{}, format, std::forward<Args>(args)...);
    }

    template <class... Args> IO::Types::Status println(const Types::LineWriteOptions &options, std::format_string<Args...> format, Args &&...args)
    {
        return println(Types::OutputStream::Stdout, options, format, std::forward<Args>(args)...);
    }

    template <class... Args>
    IO::Types::Status println(Types::OutputStream stream, const Types::LineWriteOptions &options, std::format_string<Args...> format, Args &&...args)
    {
        return Detail::vprintln(stream, options, format.get(), std::make_format_args(args...));
    }
} // namespace GameWIP::Terminal
