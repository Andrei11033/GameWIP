/// @file input.h
/// @brief Public Terminal input, capability, and structured-event contracts.

#pragma once

#include "terminal/types.h"
#include "terminal/terminal_export.h"
#include "io/status.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <variant>

namespace GameWIP::Terminal
{
    /// @brief Read timeout requesting a non-blocking attempt.
    /// @note A stream without non-blocking-read support returns Unsupported.
    inline constexpr std::chrono::milliseconds kNoWait{0};

    /// @brief Default maximum UTF-8 byte count returned by one text read.
    /// @note Text reads preserve complete code points and can return SizeLimitExceeded when the next code point does not fit.
    inline constexpr std::uint64_t kDefaultMaxReturnedTextBytes = std::uint64_t{64} * 1024;

    /// @brief Default maximum UTF-8 byte count returned by one line read.
    /// @note The limit applies to the returned representation after the selected line-ending policy.
    inline constexpr std::uint64_t kDefaultMaxReturnedLineBytes = std::uint64_t{64} * 1024;

    namespace Types
    {
        namespace Input
        {
            /// @brief Standard terminal input streams.
            enum class Stream
            {
                /// @brief Process standard input.
                Stdin
            };

            /// @brief Input representation selected for one managed terminal input owner.
            enum class DeliveryMode : std::uint8_t
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
                WouldBlock,

                /// @brief The caller-requested stop token cancelled the read.
                Cancelled
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

            /// @brief How readLine() reports consumed line endings for line reads.
            enum class LineEndingMode
            {
                /// @brief Do not include the consumed line ending in the returned line.
                Strip,

                /// @brief Preserve the consumed line ending when practical.
                Keep,

                /// @brief Return a trailing '\n' for any consumed line ending.
                NormalizeToLf
            };

            /// @brief Capabilities of the managed Terminal input abstraction for one standard input endpoint.
            struct Capabilities
            {
                /// @brief Detected stream endpoint kind.
                StreamKind kind = StreamKind::Detached;

                /// @brief True when valid UTF-8 text input is supported.
                bool supportsUtf8Text = false;

                /// @brief True when arbitrary byte input is supported.
                bool supportsByteInput = false;

                /// @brief True when line input is supported.
                bool supportsLineInput = false;

                /// @brief True when normalized structured event input is supported.
                bool supportsEventInput = false;

                /// @brief True when a zero-duration read can poll without a normal blocking wait.
                bool supportsNonBlockingReads = false;

                /// @brief True when positive finite read deadlines are supported.
                bool supportsFiniteTimeouts = false;

                /// @brief True when an in-progress blocking read can observe a caller stop request.
                bool supportsCancellation = false;

                /// @brief True when terminal resize changes can be delivered as structured events.
                bool supportsResizeEvents = false;

                /// @brief True when recognized paste input can be delivered as Types::Events::Paste.
                bool supportsPasteEvents = false;

                /// @brief True when key repeat events can be distinguished from initial presses.
                bool supportsKeyRepeatEvents = false;

                /// @brief True when key release events can be reported.
                bool supportsKeyReleaseEvents = false;

                /// @brief True when standalone modifier-key events can be reported.
                bool supportsStandaloneModifierEvents = false;

                /// @brief True when media-key events can be reported.
                bool supportsMediaKeyEvents = false;

                /// @brief True when key location is reported reliably where meaningful.
                bool supportsKeyLocation = false;

                /// @brief True when accompanying modifier/lock state is reported reliably.
                bool supportsModifierState = false;
            };

            /// @brief Result returned by input capability queries.
            struct CapabilitiesResult
            {
                /// @brief Operation status.
                IO::Types::Status status;

                /// @brief Reported input capabilities.
                Types::Input::Capabilities capabilities;
            };

            /// @brief Options used by byte reads.
            struct ByteOptions
            {
                /// @brief Total read deadline: nullopt waits indefinitely, zero polls, and positive values bound the complete operation.
                /// @note Negative values are InvalidArgument. Endpoint support remains authoritative.
                std::optional<std::chrono::milliseconds> timeout = std::nullopt;

                /// @brief Caller-controlled cancellation request observed where the endpoint supports cancellable blocking reads.
                std::stop_token stopToken{};

                /// @brief Whether a successful read may return fewer bytes than requested.
                bool allowPartial = true;
            };

            /// @brief Options used by text reads.
            /// @details Text reads return one available valid UTF-8 chunk after input becomes available.
            struct TextOptions
            {
                /// @brief Total read deadline: nullopt waits indefinitely, zero polls, and positive values bound the complete operation.
                /// @note Negative values are InvalidArgument. Endpoint support remains authoritative.
                std::optional<std::chrono::milliseconds> timeout = std::nullopt;

                /// @brief Caller-controlled cancellation request observed where the endpoint supports cancellable blocking reads.
                std::stop_token stopToken{};

                /// @brief Maximum accepted byte count for one available UTF-8 text chunk.
                std::uint64_t maxReturnedBytes = kDefaultMaxReturnedTextBytes;
            };

            /// @brief Options used by line reads.
            /// @details Line reads do not expose allowPartial; they stop at a line ending, terminating outcome, truncation, or failure.
            struct LineOptions
            {
                /// @brief Total read deadline: nullopt waits indefinitely, zero polls, and positive values bound the complete operation.
                /// @note Negative values are InvalidArgument. Endpoint support remains authoritative.
                std::optional<std::chrono::milliseconds> timeout = std::nullopt;

                /// @brief Caller-controlled cancellation request observed where the endpoint supports cancellable blocking reads.
                std::stop_token stopToken{};

                /// @brief Maximum accepted byte count for the returned line representation.
                std::uint64_t maxReturnedBytes = kDefaultMaxReturnedLineBytes;

                /// @brief Whether an interactive terminal read manages visible echo and editing feedback on the bound output stream.
                /// @note Redirected input ignores this option because no terminal line discipline is active.
                bool echo = true;

                /// @brief How a consumed line ending is represented.
                Types::Input::LineEndingMode lineEndingMode = Types::Input::LineEndingMode::Strip;
            };

            /// @brief Options used by structured event reads.
            struct EventOptions
            {
                /// @brief Total read deadline: nullopt waits indefinitely, zero polls, and positive values bound the complete operation.
                /// @note Negative values are InvalidArgument. Endpoint support remains authoritative.
                std::optional<std::chrono::milliseconds> timeout = std::nullopt;

                /// @brief Caller-controlled cancellation request observed where the endpoint supports cancellable blocking reads.
                std::stop_token stopToken{};
            };

            /// @brief Result returned by byte reads.
            /// @details bytesRead can preserve partial progress together with a terminating outcome or later failure.
            struct ByteResult
            {
                /// @brief Operation status.
                IO::Types::Status status;

                /// @brief Read outcome.
                Types::Input::ReadOutcome outcome = Types::Input::ReadOutcome::Completed;

                /// @brief Number of bytes copied into the caller buffer.
                std::size_t bytesRead = 0;
            };

            /// @brief Result returned by text reads.
            /// @details A non-empty text payload represents one completed UTF-8 chunk. Other outcomes describe why no chunk completed.
            struct TextResult
            {
                /// @brief Operation status.
                IO::Types::Status status;

                /// @brief Read outcome.
                Types::Input::ReadOutcome outcome = Types::Input::ReadOutcome::Completed;

                /// @brief UTF-8 text bytes read.
                std::string text;

                /// @brief True when maxReturnedBytes limited the returned text.
                bool wasTruncated = false;
            };

            /// @brief Result returned by line reads.
            /// @details line can contain an unterminated valid UTF-8 prefix together with EndOfStream, TimedOut, WouldBlock, or Cancelled.
            struct LineResult
            {
                /// @brief Operation status.
                IO::Types::Status status;

                /// @brief Read outcome.
                Types::Input::ReadOutcome outcome = Types::Input::ReadOutcome::Completed;

                /// @brief UTF-8 line text.
                std::string line;

                /// @brief Consumed line ending.
                Types::Input::ConsumedLineEnding consumedLineEnding = Types::Input::ConsumedLineEnding::None;

                /// @brief True when maxReturnedBytes limited the returned line.
                bool wasTruncated = false;
            };

        } // namespace Input


        namespace Events
        {
            /// @brief One valid Unicode scalar reported as a logical terminal key.
            struct CharacterKey
            {
                /// @brief Unicode scalar value. Terminal-produced values are never surrogate code points.
                char32_t value = U'\0';

                /// @brief Compares Unicode scalar values.
                friend constexpr bool operator==(CharacterKey, CharacterKey) noexcept = default;
            };

            /// @brief Portable non-character key reported by terminal input.
            enum class NamedKey : std::uint8_t
            {
                Backspace,   ///< Backspace key.
                Tab,         ///< Tab key. Shift+Tab is represented by the Shift modifier.
                Enter,       ///< Enter or Return key.
                Escape,      ///< Escape key.
                Insert,      ///< Insert key.
                Delete,      ///< Delete key.
                Home,        ///< Home key.
                End,         ///< End key.
                PageUp,      ///< Page Up key.
                PageDown,    ///< Page Down key.
                ArrowUp,     ///< Up-arrow key.
                ArrowDown,   ///< Down-arrow key.
                ArrowLeft,   ///< Left-arrow key.
                ArrowRight,  ///< Right-arrow key.
                Begin,       ///< Begin or keypad-center navigation key where reportable.
                CapsLock,    ///< Caps Lock key transition where reportable.
                NumLock,     ///< Num Lock key transition where reportable.
                ScrollLock,  ///< Scroll Lock key transition where reportable.
                PrintScreen, ///< Print Screen key where reportable.
                Pause,       ///< Pause/Break key where reportable.
                Menu         ///< Menu/context key where reportable.
            };

            /// @brief Numeric function key without an arbitrary public upper bound.
            struct FunctionKey
            {
                /// @brief One-based function-key number. Terminal-produced values are always nonzero.
                std::uint16_t number = 0;

                /// @brief Compares function-key numbers.
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
            using KeyValue = std::variant<
                Types::Events::CharacterKey,
                Types::Events::NamedKey,
                Types::Events::FunctionKey,
                Types::Events::ModifierKey,
                Types::Events::MediaKey>;

            /// @brief One normalized logical key input event.
            /// @details For standalone Types::Events::ModifierKey events, modifiers describes other active modifiers and does not duplicate key.
            struct Key
            {
                Types::Events::KeyValue key;                                               ///< Logical key reported by the terminal.
                Types::Events::KeyModifier modifiers = Types::Events::KeyModifier::None;   ///< Active modifier/lock state known for this event.
                Types::Events::KeyAction action = Types::Events::KeyAction::Press;         ///< Press/repeat/release phase.
                Types::Events::KeyLocation location = Types::Events::KeyLocation::Unknown; ///< Location, or Unknown when unavailable.
                std::uint32_t repeatCount = 1;                                             ///< Number of occurrences represented by a Repeat event.
            };

            /// @brief Recognized paste input delivered as one bounded owned UTF-8 payload.
            struct Paste
            {
                /// @brief Valid UTF-8 paste payload assembled by the managed input backend.
                std::string text;
            };

            /// @brief Terminal-size change delivered through structured event input.
            struct Resize
            {
                /// @brief Resulting terminal dimensions in character cells.
                Types::Size size;
            };

            /// @brief Tagged payload for one portable Terminal event.
            using Payload = std::variant<Types::Events::Key, Types::Events::Paste, Types::Events::Resize>;

        } // namespace Events

        /// @brief One normalized pull-based Terminal input event.
        /// @details Types::Events::KeyValue and resize alternatives use only inline value storage. Paste owns bounded UTF-8 text.
        struct Event
        {
            Types::Events::Payload data; ///< Typed event payload.

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


        namespace Input
        {
            /// @brief Result returned by structured event reads.
            struct EventResult
            {
                /// @brief Operation status.
                IO::Types::Status status;

                /// @brief Read outcome.
                Types::Input::ReadOutcome outcome = Types::Input::ReadOutcome::Completed;

                /// @brief Event produced by a completed event read.
                /// @details Empty for non-completed outcomes and failures.
                std::optional<Event> event;
            };

        } // namespace Input

    } // namespace Types

    /// @brief Returns a snapshot of capabilities for stdin.
    /// @return Status and capabilities observed for the current stdin endpoint. A successful Detached result is possible.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Input::CapabilitiesResult getInputCapabilities() noexcept;

    /// @brief Returns capabilities for an input stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Input::CapabilitiesResult getInputCapabilities(Types::Input::Stream stream) noexcept;

    /// @name Input
    /// @{

    /// @brief Reads one structured input event from stdin through temporary managed ownership.
    /// @return Status, stopping outcome, and optional event payload.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Input::EventResult readEvent(const Types::Input::EventOptions &options = {}) noexcept;

    /// @brief Reads one structured input event from an input stream through temporary managed ownership.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Input::EventResult readEvent(
        Types::Input::Stream stream,
        const Types::Input::EventOptions &options = {}) noexcept;

    /// @brief Reads one UTF-8 line from stdin.
    /// @return Status, stopping outcome, returned line, consumed ending, and truncation state. Partial line text may accompany
    /// EndOfStream, TimedOut, WouldBlock, Cancelled, or a later failure.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Input::LineResult readLine(const Types::Input::LineOptions &options = {}) noexcept;

    /// @brief Reads one line from an input stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Input::LineResult readLine(
        Types::Input::Stream stream,
        const Types::Input::LineOptions &options = {}) noexcept;

    /// @brief Reads one available complete UTF-8 text chunk from stdin.
    /// @return Status, stopping outcome, text, and truncation state. Size limits never split a valid UTF-8 code point.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Input::TextResult readText(const Types::Input::TextOptions &options = {}) noexcept;

    /// @brief Reads one available UTF-8 text chunk from an input stream.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Input::TextResult readText(
        Types::Input::Stream stream,
        const Types::Input::TextOptions &options = {}) noexcept;

    /// @brief Reads bytes from stdin into caller storage.
    /// @return Status, stopping outcome, and bytes copied. Partial progress may accompany a later failure or terminating outcome.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Input::ByteResult readBytes(
        std::span<std::byte> outputBuffer,
        const Types::Input::ByteOptions &options = {}) noexcept;

    /// @brief Reads bytes from an input stream into caller storage.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Input::ByteResult readBytes(
        Types::Input::Stream stream,
        std::span<std::byte> outputBuffer,
        const Types::Input::ByteOptions &options = {}) noexcept;

    /// @}
} // namespace GameWIP::Terminal
