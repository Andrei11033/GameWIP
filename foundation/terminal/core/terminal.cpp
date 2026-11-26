/// @file terminal.cpp
/// @brief Core implementation for the Terminal library.

#include "terminal/input.h"
#include "terminal/output.h"
#include "terminal/internal/input.h"
#include "terminal/platform/backend.h"
#include "terminal/internal/results.h"
#include "terminal/internal/status.h"
#include "terminal/internal/scopes.h"
#include "terminal/internal/style.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace GameWIP::Terminal
{
    namespace
    {
        using ErrorCode = GameWIP::Status::Types::Code;

        /// ------------------------------------------------------------
        /// Output state and formatting storage
        /// ------------------------------------------------------------

        /// @brief Internal capability selector shared by terminal-control emitters.
        enum class ControlFeature
        {
            StyleReset,
            CursorMovement,
            CursorSaveRestore,
            CursorVisibility,
            Clear,
            Scroll,
            AlternateScreen,
            Title,
            Bell
        };

        /// @brief Per-output-stream serialization, reusable assembly storage, and nested scope depth.
        struct OutputState
        {
            // Stream synchronization
            std::mutex mutex;

            // Reusable output storage
            std::string assembly;
            std::string formatScratch;

            // Scoped terminal modes
            std::size_t cursorHiddenScopeDepth = 0;
            std::size_t alternateScreenScopeDepth = 0;
        };

        inline constexpr std::size_t kRetainedAssemblyLimit = std::size_t{64} * 1024;

        /// @brief Leases reusable format storage while custom formatters run without the stream lock.
        /// @details Formatting before final stream serialization permits formatter reentry: a nested Terminal write can
        /// complete before the outer record without deadlocking on the same OutputState mutex.
        class FormatScratchLease
        {
        public:
            explicit FormatScratchLease(OutputState &state)
                : state_(state)
            {
                std::lock_guard lock(state_.mutex);
                text_.swap(state_.formatScratch);
                text_.clear();
            }

            ~FormatScratchLease() noexcept
            {
                try
                {
                    text_.clear();

                    if(text_.capacity() > kRetainedAssemblyLimit)
                    {
                        std::string{}.swap(text_);
                    }

                    std::lock_guard lock(state_.mutex);

                    if(text_.capacity() > state_.formatScratch.capacity())
                    {
                        text_.swap(state_.formatScratch);
                    }
                }
                // NOLINTNEXTLINE(bugprone-empty-catch) -- Scratch retention is best-effort at a noexcept cleanup boundary.
                catch(...)
                {
                    // Scratch retention is an optimization; destruction must never terminate on cleanup failure.
                }
            }

            FormatScratchLease(const FormatScratchLease &) = delete;
            FormatScratchLease &operator=(const FormatScratchLease &) = delete;

            [[nodiscard]] std::string &text() noexcept
            {
                return text_;
            }

        private:
            OutputState &state_;
            std::string text_;
        };

        /// ------------------------------------------------------------
        /// Stream and payload validation
        /// ------------------------------------------------------------

        /// @brief Validates supported standard input stream enum values.
        [[nodiscard]] bool validInputStream(Types::Input::Stream stream) noexcept
        {
            return stream == Types::Input::Stream::Stdin;
        }

        /// @brief Validates supported standard output stream enum values.
        [[nodiscard]] bool validOutputStream(Types::Output::Stream stream) noexcept
        {
            return stream == Types::Output::Stream::Stdout || stream == Types::Output::Stream::Stderr;
        }

        /// @brief Validates line-ending enum values crossing the public boundary.
        [[nodiscard]] bool validLineEnding(Types::Output::LineEnding lineEnding) noexcept
        {
            switch(lineEnding)
            {
            case Types::Output::LineEnding::Native:
            case Types::Output::LineEnding::Lf:
            case Types::Output::LineEnding::CrLf:
                return true;
            }

            return false;
        }

        /// @brief Validates one public Terminal text payload before any output-side effect.
        [[nodiscard]] GameWIP::Status::Types::Status validateUtf8Text(std::string_view text) noexcept
        {
            return Unicode::Utf8::validate(text).outcome == Unicode::Types::ValidationOutcome::Valid ? GameWIP::Status::Types::Status{}
                                                                                                     : GameWIP::Terminal::Internal::backendStatus(ErrorCode::EncodingFailed);
        }

        /// @brief Builds an Unsupported status with optional diagnostic text.
        [[nodiscard]] GameWIP::Status::Types::Status unsupportedStatus(std::string_view message = {}) noexcept
        {
            return Internal::statusWithMessage(ErrorCode::Unsupported, message);
        }

        /// @brief Builds an InvalidArgument status with optional diagnostic text.
        [[nodiscard]] GameWIP::Status::Types::Status invalidArgumentStatus(std::string_view message = {}) noexcept
        {
            return Internal::statusWithMessage(ErrorCode::InvalidArgument, message);
        }

        /// @brief Returns shared process-lifetime state for stdout or stderr.
        /// @details Function-local statics avoid cross-translation-unit initialization ordering while the shared library
        /// still provides one coordination domain to every module that resolves this Terminal runtime.
        [[nodiscard]] OutputState &outputState(Types::Output::Stream stream) noexcept
        {
            static OutputState stdoutState;
            static OutputState stderrState;

            return stream == Types::Output::Stream::Stderr ? stderrState : stdoutState;
        }

        /// ------------------------------------------------------------
        /// Output record assembly
        /// ------------------------------------------------------------

        /// @brief Resolves a validated line-ending policy to emitted bytes.
        [[nodiscard]] std::string_view lineEndingText(Types::Output::LineEnding lineEnding) noexcept
        {
            switch(lineEnding)
            {
            case Types::Output::LineEnding::Native:
                return Platform::nativeLineEnding();
            case Types::Output::LineEnding::Lf:
                return "\n";
            case Types::Output::LineEnding::CrLf:
                return "\r\n";
            }

            return {};
        }

        /// @brief Clears assembly text and releases unusually large retained capacity.
        void releaseLargeAssembly(OutputState &state) noexcept
        {
            state.assembly.clear();

            if(state.assembly.capacity() > kRetainedAssemblyLimit)
            {
                std::string{}.swap(state.assembly);
            }
        }

        /// @brief Appends an unsigned decimal value without locale-dependent formatting.
        void appendUnsigned(std::string &text, std::uint64_t value)
        {
            std::array<char, 32> buffer{};
            const auto result = std::to_chars(buffer.data(), std::to_address(buffer.end()), value);
            text.append(buffer.data(), result.ptr);
        }

        /// @brief Applies an optional validated flush after a logical write.
        [[nodiscard]] GameWIP::Status::Types::Status flushIfRequested(Types::Output::Stream stream, IO::Types::FlushMode mode)
        {
            if(!IO::isValidFlushMode(mode))
            {
                return invalidArgumentStatus("Unknown IO flush mode.");
            }

            if(mode == IO::Types::FlushMode::None)
            {
                return GameWIP::Status::Types::Status{};
            }
            return Platform::flush(stream, mode);
        }

        /// @brief Writes and clears a stream's assembled record, then performs the requested flush.
        [[nodiscard]] GameWIP::Status::Types::Status writeAssembly(Types::Output::Stream stream, OutputState &state, IO::Types::FlushMode flushMode)
        {
            GameWIP::Status::Types::Status status = GameWIP::Status::Types::Status{};

            if(!state.assembly.empty())
            {
                status = Platform::writeText(stream, state.assembly);
            }

            if(status.ok())
            {
                status = flushIfRequested(stream, flushMode);
            }

            releaseLargeAssembly(state);
            return status;
        }

        /// ------------------------------------------------------------
        /// Text and byte records
        /// ------------------------------------------------------------

        /// @brief Implements one serialized styled text write without acquiring the stream mutex.
        [[nodiscard]] GameWIP::Status::Types::Status writeTextUnlocked(
            Types::Output::Stream stream,
            OutputState &state,
            std::string_view utf8Text,
            const Types::Output::TextOptions &options)
        {
            if(!IO::isValidFlushMode(options.flushMode))
            {
                return invalidArgumentStatus("Unknown IO flush mode.");
            }

            GameWIP::Status::Types::Status textStatus = validateUtf8Text(utf8Text);

            if(!textStatus.ok())
            {
                return textStatus;
            }

            const Internal::StylePlan plan = Internal::stylePlan(stream, options.styleMode, options.style);

            if(!plan.status.ok())
            {
                return plan.status;
            }

            if(!plan.emitStyle)
            {
                GameWIP::Status::Types::Status status = Platform::writeText(stream, utf8Text);
                return status.ok() ? flushIfRequested(stream, options.flushMode) : status;
            }

            const Internal::StyleSequence prefix = Internal::makeStyleSequence(plan.style);
            state.assembly.clear();
            state.assembly.append(prefix.bytes.data(), prefix.size);
            state.assembly.append(utf8Text);
            state.assembly.append("\x1b[0m");
            return writeAssembly(stream, state, options.flushMode);
        }

        /// @brief Implements one serialized styled text write followed by the selected line ending.
        [[nodiscard]] GameWIP::Status::Types::Status writeLineUnlocked(
            Types::Output::Stream stream,
            OutputState &state,
            std::string_view utf8Text,
            const Types::Output::LineOptions &options)
        {
            if(!validLineEnding(options.lineEnding))
            {
                return invalidArgumentStatus("Unknown terminal line ending.");
            }

            if(!IO::isValidFlushMode(options.flushMode))
            {
                return invalidArgumentStatus("Unknown IO flush mode.");
            }

            GameWIP::Status::Types::Status textStatus = validateUtf8Text(utf8Text);

            if(!textStatus.ok())
            {
                return textStatus;
            }

            const Internal::StylePlan plan = Internal::stylePlan(stream, options.styleMode, options.style);

            if(!plan.status.ok())
            {
                return plan.status;
            }

            state.assembly.clear();

            if(plan.emitStyle)
            {
                const Internal::StyleSequence prefix = Internal::makeStyleSequence(plan.style);
                state.assembly.append(prefix.bytes.data(), prefix.size);
            }
            state.assembly.append(utf8Text);

            if(plan.emitStyle)
            {
                state.assembly.append("\x1b[0m");
            }
            state.assembly.append(lineEndingText(options.lineEnding));
            return writeAssembly(stream, state, options.flushMode);
        }

        /// @brief Implements one serialized raw-byte write and optional flush.
        [[nodiscard]] Internal::ByteWriteResult writeBytesUnlocked(
            Types::Output::Stream stream,
            std::span<const std::byte> bytes,
            const Types::Output::ByteOptions &options)
        {
            if(!IO::isValidFlushMode(options.flushMode))
            {
                return Internal::failure<IO::Types::WriteProgress>(invalidArgumentStatus("Unknown IO flush mode."));
            }

            Internal::ByteWriteResult result = Platform::writeBytes(stream, bytes);

            if(!result.ok())
            {
                return result;
            }

            GameWIP::Status::Types::Status status = flushIfRequested(stream, options.flushMode);
            return status.ok() ? result : Internal::ByteWriteResult::failure(std::move(status), result.value());
        }

        /// ------------------------------------------------------------
        /// Segmented output
        /// ------------------------------------------------------------

        /// @brief Validates segmented-write kinds, styles, and flush options before emission.
        [[nodiscard]] GameWIP::Status::Types::Status validateSegments(
            std::span<const Types::Output::Segment> segments,
            Types::Style::Mode styleMode,
            const Types::Output::Capabilities &capabilities)
        {
            for(const Types::Output::Segment &segment : segments)
            {
                switch(segment.kind())
                {
                case Types::Output::SegmentKind::Text:
                    break;
                case Types::Output::SegmentKind::Bytes:
                    if(!capabilities.supportsByteOutput)
                    {
                        return unsupportedStatus("Terminal byte output is unsupported for this output stream.");
                    }
                    break;
                case Types::Output::SegmentKind::StyledText:
                {
                    const Internal::StylePlan plan = Internal::stylePlanForCapabilities(styleMode, segment.style(), capabilities.style);

                    if(!plan.status.ok())
                    {
                        return plan.status;
                    }

                    break;
                }
                default:
                    return invalidArgumentStatus("Unknown terminal write segment kind.");
                }
            }

            return GameWIP::Status::Types::Status{};
        }

        /// @brief Precomputed allocation, preparation, and style needs for one segmented record.
        struct SegmentRequirements
        {
            bool hasBytes = false;
            bool needsStyleCapabilities = false;
        };

        /// @brief Computes preparation and assembly requirements for one segmented record.
        [[nodiscard]] SegmentRequirements segmentRequirements(std::span<const Types::Output::Segment> segments, Types::Style::Mode styleMode) noexcept
        {
            SegmentRequirements requirements;

            for(const Types::Output::Segment &segment : segments)
            {
                if(segment.kind() == Types::Output::SegmentKind::Bytes)
                {
                    requirements.hasBytes = true;
                }
                else if(
                    styleMode != Types::Style::Mode::Never && segment.kind() == Types::Output::SegmentKind::StyledText &&
                    !Internal::isDefaultStyle(segment.style()))
                {
                    requirements.needsStyleCapabilities = true;
                }
            }

            return requirements;
        }

        /// @brief Validates, assembles, and emits one logical mixed text/style/byte record under the stream lock.
        /// @details Validation finishes before intentional emission begins, but the platform write itself is not
        /// transactional and can still emit a prefix before reporting failure.
        [[nodiscard]] GameWIP::Status::Types::Status writeSegmentsUnlocked(
            Types::Output::Stream stream,
            OutputState &state,
            std::span<const Types::Output::Segment> segments,
            const Types::Output::SegmentOptions &options)
        {
            // Validate write options and all text segments before preparing output.
            if(options.appendLineEnding && !validLineEnding(options.lineEnding))
            {
                return invalidArgumentStatus("Unknown terminal line ending.");
            }

            if(!IO::isValidFlushMode(options.flushMode))
            {
                return invalidArgumentStatus("Unknown IO flush mode.");
            }

            switch(options.styleMode)
            {
            case Types::Style::Mode::Never:
            case Types::Style::Mode::Auto:
            case Types::Style::Mode::Required:
                break;
            default:
                return invalidArgumentStatus("Unknown terminal style mode.");
            }

            for(const Types::Output::Segment &segment : segments)
            {
                if((segment.kind() == Types::Output::SegmentKind::Text || segment.kind() == Types::Output::SegmentKind::StyledText))
                {
                    GameWIP::Status::Types::Status textStatus = validateUtf8Text(segment.text());

                    if(!textStatus.ok())
                    {
                        return textStatus;
                    }
                }
            }

            // Resolve the capabilities needed for byte and styled segments.
            const SegmentRequirements requirements = segmentRequirements(segments, options.styleMode);
            Types::Output::Capabilities capabilities{};

            if(requirements.hasBytes || requirements.needsStyleCapabilities)
            {
                Types::Output::CapabilitiesResult capabilityResult = Platform::getOutputCapabilities(stream);

                if(!capabilityResult.ok())
                {
                    if(requirements.hasBytes)
                    {
                        return capabilityResult.status();
                    }

                    Types::Output::CapabilitiesResult prepared = Platform::prepareOutput(stream);

                    if(prepared.ok())
                    {
                        capabilities = prepared.value();
                    }
                    else if(options.styleMode == Types::Style::Mode::Required)
                    {
                        return prepared.status();
                    }
                }
                else
                {
                    capabilities = capabilityResult.value();

                    if(requirements.needsStyleCapabilities)
                    {
                        const bool allStylesSupported = std::all_of(
                            segments.begin(),
                            segments.end(),
                            [&capabilities, &options](const Types::Output::Segment &segment)
                            {
                                if(segment.kind() != Types::Output::SegmentKind::StyledText || Internal::isDefaultStyle(segment.style()))
                                {
                                    return true;
                                }

                                const Internal::StylePlan plan =
                                    Internal::stylePlanForCapabilities(options.styleMode, segment.style(), capabilities.style);
                                return plan.status.ok() && plan.emitStyle;
                            });

                        if(!allStylesSupported && capabilities.kind != Types::StreamKind::Redirected)
                        {
                            Types::Output::CapabilitiesResult prepared = Platform::prepareOutput(stream);

                            if(prepared.ok())
                            {
                                capabilities = prepared.value();
                            }
                            else if(options.styleMode == Types::Style::Mode::Required)
                            {
                                return prepared.status();
                            }
                        }
                    }
                }
            }

            // Finish semantic validation before any output is emitted.
            GameWIP::Status::Types::Status status = validateSegments(segments, options.styleMode, capabilities);

            if(!status.ok())
            {
                return status;
            }

            if(segments.size() == 1 && segments.front().kind() == Types::Output::SegmentKind::Text && !segments.front().text().empty() &&
               !options.appendLineEnding)
            {
                status = Platform::writeText(stream, segments.front().text());
                return status.ok() ? flushIfRequested(stream, options.flushMode) : status;
            }

            // Assemble text, styles, bytes, and the optional line ending.
            state.assembly.clear();

            for(const Types::Output::Segment &segment : segments)
            {
                switch(segment.kind())
                {
                case Types::Output::SegmentKind::Text:
                    state.assembly.append(segment.text());
                    break;
                case Types::Output::SegmentKind::StyledText:
                {
                    const Internal::StylePlan plan =
                        Internal::stylePlanForCapabilities(options.styleMode, segment.style(), capabilities.style);

                    if(plan.emitStyle)
                    {
                        const Internal::StyleSequence prefix = Internal::makeStyleSequence(plan.style);
                        state.assembly.append(prefix.bytes.data(), prefix.size);
                    }
                    state.assembly.append(segment.text());

                    if(plan.emitStyle)
                    {
                        state.assembly.append("\x1b[0m");
                    }
                    break;
                }
                case Types::Output::SegmentKind::Bytes:
                    if(!segment.bytes().empty())
                    {
                        state.assembly.append(reinterpret_cast<const char *>(segment.bytes().data()), segment.bytes().size());
                    }
                    break;
                }
            }

            if(options.appendLineEnding)
            {
                state.assembly.append(lineEndingText(options.lineEnding));
            }

            // Use the byte writer when the record contains binary segments.
            if(requirements.hasBytes)
            {
                const auto assembled = std::as_bytes(std::span{state.assembly});
                const Internal::ByteWriteResult result = Platform::writeBytes(stream, assembled);
                GameWIP::Status::Types::Status writeStatus = result.status();

                if(writeStatus.ok() && result.value().bytesWritten != assembled.size())
                {
                    writeStatus = GameWIP::Terminal::Internal::backendStatus(ErrorCode::PartialWrite);
                }

                if(writeStatus.ok())
                {
                    writeStatus = flushIfRequested(stream, options.flushMode);
                }
                releaseLargeAssembly(state);
                return writeStatus;
            }

            return writeAssembly(stream, state, options.flushMode);
        }

        /// ------------------------------------------------------------
        /// Terminal control sequences
        /// ------------------------------------------------------------

        /// @brief Maps one control operation to its required output capability.
        [[nodiscard]] bool controlFeatureSupported(const Types::Output::Capabilities &capabilities, ControlFeature feature) noexcept
        {
            switch(feature)
            {
            case ControlFeature::StyleReset:
                return Internal::anyStyleSupported(capabilities.style);
            case ControlFeature::CursorMovement:
                return capabilities.supportsCursorMovement;
            case ControlFeature::CursorSaveRestore:
                return capabilities.supportsCursorSaveRestore;
            case ControlFeature::CursorVisibility:
                return capabilities.supportsCursorVisibility;
            case ControlFeature::Clear:
                return capabilities.supportsClear;
            case ControlFeature::Scroll:
                return capabilities.supportsScroll;
            case ControlFeature::AlternateScreen:
                return capabilities.supportsAlternateScreen;
            case ControlFeature::Title:
                return capabilities.supportsTitle;
            case ControlFeature::Bell:
                return capabilities.supportsBell;
            }

            return false;
        }

        /// @brief Validates capability and emits one prebuilt control sequence under the stream lock.
        [[nodiscard]] GameWIP::Status::Types::Status writeControlSequenceUnlocked(
            Types::Output::Stream stream,
            std::string_view sequence,
            ControlFeature feature,
            std::string_view unsupportedMessage,
            IO::Types::FlushMode flushMode,
            bool *emitted = nullptr)
        {
            if(emitted != nullptr)
            {
                *emitted = false;
            }

            // Validate flushing and resolve the stream's current capabilities.
            if(!IO::isValidFlushMode(flushMode))
            {
                return invalidArgumentStatus("Unknown IO flush mode.");
            }

            Types::Output::CapabilitiesResult capabilities = Platform::getOutputCapabilities(stream);

            if(!capabilities.ok())
            {
                return capabilities.status();
            }

            // Prepare output when current capabilities cannot support this control.
            if(!controlFeatureSupported(capabilities.value(), feature))
            {
                capabilities = Platform::prepareOutput(stream);

                if(!capabilities.ok())
                {
                    return capabilities.status();
                }

                if(!controlFeatureSupported(capabilities.value(), feature))
                {
                    return unsupportedStatus(unsupportedMessage);
                }
            }

            // Emit the sequence and flush only after a successful write.
            GameWIP::Status::Types::Status status = Platform::writeText(stream, sequence);

            if(!status.ok())
            {
                return status;
            }

            if(emitted != nullptr)
            {
                *emitted = true;
            }

            return flushIfRequested(stream, flushMode);
        }

        /// @brief Emits a control sequence already assembled in reusable stream storage.
        [[nodiscard]] GameWIP::Status::Types::Status writeAssembledControlSequenceUnlocked(
            Types::Output::Stream stream,
            OutputState &state,
            ControlFeature feature,
            std::string_view unsupportedMessage,
            IO::Types::FlushMode flushMode)
        {
            GameWIP::Status::Types::Status status = writeControlSequenceUnlocked(stream, state.assembly, feature, unsupportedMessage, flushMode);
            releaseLargeAssembly(state);
            return status;
        }

        /// @brief Appends title text while removing control-sequence terminators and unsafe controls.
        void appendSanitizedTitle(std::string &output, std::string_view utf8Title)
        {
            for(const char character : utf8Title)
            {
                const auto byte = static_cast<unsigned char>(character);

                if(byte == 0x1b || byte == 0x07 || byte < 0x20 || byte == 0x7f)
                {
                    output.push_back(' ');
                }
                else
                {
                    output.push_back(character);
                }
            }
        }
    } // namespace

    namespace Internal
    {
        /// ------------------------------------------------------------
        /// Scoped output restoration
        /// ------------------------------------------------------------

        /// @brief Decrements nested cursor-hide depth and restores visibility at the outer boundary.
        [[nodiscard]] GameWIP::Status::Types::Status restoreCursorHiddenScope(
            Types::Output::Stream stream,
            const Types::Output::ControlOptions &options,
            bool &restored) noexcept
        {
            restored = false;
            try
            {
                OutputState &state = outputState(stream);
                std::lock_guard lock(state.mutex);

                if(state.cursorHiddenScopeDepth == 0)
                {
                    restored = true;
                    return GameWIP::Status::Types::Status{};
                }

                if(state.cursorHiddenScopeDepth > 1)
                {
                    --state.cursorHiddenScopeDepth;
                    restored = true;
                    return GameWIP::Status::Types::Status{};
                }
                bool emitted = false;
                GameWIP::Status::Types::Status status = writeControlSequenceUnlocked(
                    stream,
                    "\x1b[?25h",
                    ControlFeature::CursorVisibility,
                    "Terminal cursor visibility is unsupported for this output stream.",
                    options.flushMode,
                    &emitted);

                if(emitted)
                {
                    state.cursorHiddenScopeDepth = 0;
                    restored = true;
                }

                return status;
            }
            catch(...)
            {
                return Internal::exceptionStatus();
            }
        }

        /// @brief Decrements nested alternate-screen depth and leaves at the outer boundary.
        [[nodiscard]] GameWIP::Status::Types::Status leaveAlternateScreenScope(
            Types::Output::Stream stream,
            const Types::Output::ControlOptions &options,
            bool &restored) noexcept
        {
            restored = false;
            try
            {
                OutputState &state = outputState(stream);
                std::lock_guard lock(state.mutex);

                if(state.alternateScreenScopeDepth == 0)
                {
                    restored = true;
                    return GameWIP::Status::Types::Status{};
                }

                if(state.alternateScreenScopeDepth > 1)
                {
                    --state.alternateScreenScopeDepth;
                    restored = true;
                    return GameWIP::Status::Types::Status{};
                }
                bool emitted = false;
                GameWIP::Status::Types::Status status = writeControlSequenceUnlocked(
                    stream,
                    "\x1b[?1049l",
                    ControlFeature::AlternateScreen,
                    "Terminal alternate screen is unsupported for this output stream.",
                    options.flushMode,
                    &emitted);

                if(emitted)
                {
                    state.alternateScreenScopeDepth = 0;
                    restored = true;
                }

                return status;
            }
            catch(...)
            {
                return Internal::exceptionStatus();
            }
        }

        /// @brief Releases one cursor-hide nesting obligation after destructor restoration has failed.
        void abandonCursorHiddenScope(Types::Output::Stream stream) noexcept
        {
            try
            {
                OutputState &state = outputState(stream);
                std::lock_guard lock(state.mutex);

                if(state.cursorHiddenScopeDepth > 0)
                {
                    --state.cursorHiddenScopeDepth;
                }
            }
            catch(...)
            {
                // Destruction-time bookkeeping release is best effort and cannot be reported.
                return;
            }
        }

        /// @brief Releases one alternate-screen nesting obligation after destructor restoration has failed.
        void abandonAlternateScreenScope(Types::Output::Stream stream) noexcept
        {
            try
            {
                OutputState &state = outputState(stream);
                std::lock_guard lock(state.mutex);

                if(state.alternateScreenScopeDepth > 0)
                {
                    --state.alternateScreenScopeDepth;
                }
            }
            catch(...)
            {
                // Destruction-time bookkeeping release is best effort and cannot be reported.
                return;
            }
        }

    } // namespace Internal

    /// ------------------------------------------------------------
    /// Capabilities and geometry
    /// ------------------------------------------------------------

    Types::Input::CapabilitiesResult getInputCapabilities() noexcept
    {
        return getInputCapabilities(Types::Input::Stream::Stdin);
    }

    Types::Input::CapabilitiesResult getInputCapabilities(Types::Input::Stream stream) noexcept
    {
        try
        {
            if(!validInputStream(stream))
            {
                return Internal::failure<Types::Input::Capabilities>(invalidArgumentStatus("Unknown terminal input stream."));
            }
            std::lock_guard lock(Internal::inputIoMutex(stream));
            return Internal::result(Platform::getInputCapabilities(stream));
        }
        catch(...)
        {
            return Internal::failure<Types::Input::Capabilities>(Internal::exceptionStatus());
        }
    }

    Types::Output::CapabilitiesResult getOutputCapabilities() noexcept
    {
        return getOutputCapabilities(Types::Output::Stream::Stdout);
    }

    Types::Output::CapabilitiesResult getOutputCapabilities(Types::Output::Stream stream) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::failure<Types::Output::Capabilities>(invalidArgumentStatus("Unknown terminal output stream."));
            }
            std::lock_guard lock(outputState(stream).mutex);
            return Internal::result(Platform::getOutputCapabilities(stream));
        }
        catch(...)
        {
            return Internal::failure<Types::Output::Capabilities>(Internal::exceptionStatus());
        }
    }

    Types::Output::CapabilitiesResult prepareOutput() noexcept
    {
        return prepareOutput(Types::Output::Stream::Stdout);
    }

    Types::Output::CapabilitiesResult prepareOutput(Types::Output::Stream stream) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::failure<Types::Output::Capabilities>(invalidArgumentStatus("Unknown terminal output stream."));
            }
            std::lock_guard lock(outputState(stream).mutex);
            return Internal::result(Platform::prepareOutput(stream));
        }
        catch(...)
        {
            return Internal::failure<Types::Output::Capabilities>(Internal::exceptionStatus());
        }
    }

    Types::SizeResult getTerminalSize() noexcept
    {
        return getTerminalSize(Types::Output::Stream::Stdout);
    }

    Types::SizeResult getTerminalSize(Types::Output::Stream stream) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::failure<Types::Size>(invalidArgumentStatus("Unknown terminal output stream."));
            }
            std::lock_guard lock(outputState(stream).mutex);
            return Internal::result(Platform::getTerminalSize(stream));
        }
        catch(...)
        {
            return Internal::failure<Types::Size>(Internal::exceptionStatus());
        }
    }

    /// ------------------------------------------------------------
    /// Output
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> writeText(std::string_view utf8Text, const Types::Output::TextOptions &options) noexcept
    {
        return Internal::result(writeText(Types::Output::Stream::Stdout, utf8Text, options));
    }

    GameWIP::Status::Types::Result<void> writeText(Types::Output::Stream stream, std::string_view utf8Text, const Types::Output::TextOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::result(invalidArgumentStatus("Unknown terminal output stream."));
            }
            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);
            return Internal::result(writeTextUnlocked(stream, state, utf8Text, options));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> writeLine(std::string_view utf8Text, const Types::Output::LineOptions &options) noexcept
    {
        return Internal::result(writeLine(Types::Output::Stream::Stdout, utf8Text, options));
    }

    GameWIP::Status::Types::Result<void> writeLine(Types::Output::Stream stream, std::string_view utf8Text, const Types::Output::LineOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::result(invalidArgumentStatus("Unknown terminal output stream."));
            }
            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);
            return Internal::result(writeLineUnlocked(stream, state, utf8Text, options));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<IO::Types::WriteProgress> writeBytes(std::span<const std::byte> bytes, const Types::Output::ByteOptions &options) noexcept
    {
        return Internal::result(writeBytes(Types::Output::Stream::Stdout, bytes, options));
    }

    GameWIP::Status::Types::Result<IO::Types::WriteProgress> writeBytes(
        Types::Output::Stream stream,
        std::span<const std::byte> bytes,
        const Types::Output::ByteOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::failure<IO::Types::WriteProgress>(invalidArgumentStatus("Unknown terminal output stream."));
            }
            std::lock_guard lock(outputState(stream).mutex);
            return Internal::result(writeBytesUnlocked(stream, bytes, options));
        }
        catch(...)
        {
            return Internal::failure<IO::Types::WriteProgress>(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> writeSegments(std::span<const Types::Output::Segment> segments, const Types::Output::SegmentOptions &options) noexcept
    {
        return Internal::result(writeSegments(Types::Output::Stream::Stdout, segments, options));
    }

    GameWIP::Status::Types::Result<void> writeSegments(
        Types::Output::Stream stream,
        std::span<const Types::Output::Segment> segments,
        const Types::Output::SegmentOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::result(invalidArgumentStatus("Unknown terminal output stream."));
            }
            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);
            return Internal::result(writeSegmentsUnlocked(stream, state, segments, options));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    namespace Internal
    {
        GameWIP::Status::Types::Result<void> vprint(
            Types::Output::Stream stream,
            const Types::Output::TextOptions &options,
            std::string_view format,
            std::format_args arguments) noexcept
        {
            try
            {
                if(!validOutputStream(stream))
                {
                    return result(invalidArgumentStatus("Unknown terminal output stream."));
                }

                if(!IO::isValidFlushMode(options.flushMode))
                {
                    return result(invalidArgumentStatus("Unknown IO flush mode."));
                }
                OutputState &state = outputState(stream);
                FormatScratchLease scratch(state);
                std::vformat_to(std::back_inserter(scratch.text()), format, arguments);

                std::lock_guard lock(state.mutex);
                return result(writeTextUnlocked(stream, state, scratch.text(), options));
            }
            catch(...)
            {
                return result(Internal::exceptionStatus());
            }
        }

        GameWIP::Status::Types::Result<void> vprintln(
            Types::Output::Stream stream,
            const Types::Output::LineOptions &options,
            std::string_view format,
            std::format_args arguments) noexcept
        {
            try
            {
                if(!validOutputStream(stream))
                {
                    return result(invalidArgumentStatus("Unknown terminal output stream."));
                }

                if(!validLineEnding(options.lineEnding))
                {
                    return result(invalidArgumentStatus("Unknown terminal line ending."));
                }

                if(!IO::isValidFlushMode(options.flushMode))
                {
                    return result(invalidArgumentStatus("Unknown IO flush mode."));
                }
                OutputState &state = outputState(stream);
                FormatScratchLease scratch(state);
                std::vformat_to(std::back_inserter(scratch.text()), format, arguments);

                std::lock_guard lock(state.mutex);
                return result(writeLineUnlocked(stream, state, scratch.text(), options));
            }
            catch(...)
            {
                return result(Internal::exceptionStatus());
            }
        }
    } // namespace Internal

    GameWIP::Status::Types::Result<void> flush(IO::Types::FlushMode mode) noexcept
    {
        return Internal::result(flush(Types::Output::Stream::Stdout, mode));
    }

    GameWIP::Status::Types::Result<void> flush(Types::Output::Stream stream, IO::Types::FlushMode mode) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::result(invalidArgumentStatus("Unknown terminal output stream."));
            }

            if(!IO::isValidFlushMode(mode))
            {
                return Internal::result(invalidArgumentStatus("Unknown IO flush mode."));
            }
            std::lock_guard lock(outputState(stream).mutex);
            return Internal::result(Platform::flush(stream, mode));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    /// ------------------------------------------------------------
    /// Terminal controls
    /// ------------------------------------------------------------

    GameWIP::Status::Types::Result<void> resetStyle(const Types::Output::ControlOptions &options) noexcept
    {
        return Internal::result(resetStyle(Types::Output::Stream::Stdout, options));
    }

    GameWIP::Status::Types::Result<void> resetStyle(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::result(invalidArgumentStatus("Unknown terminal output stream."));
            }
            std::lock_guard lock(outputState(stream).mutex);
            return Internal::result(writeControlSequenceUnlocked(
                stream,
                "\x1b[0m",
                ControlFeature::StyleReset,
                "Terminal style reset is unsupported for this output stream.",
                options.flushMode));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> moveCursor(Types::Cursor::MoveDirection direction, std::uint32_t amount, const Types::Output::ControlOptions &options) noexcept
    {
        return Internal::result(moveCursor(Types::Output::Stream::Stdout, direction, amount, options));
    }

    GameWIP::Status::Types::Result<void> moveCursor(
        Types::Output::Stream stream,
        Types::Cursor::MoveDirection direction,
        std::uint32_t amount,
        const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::result(invalidArgumentStatus("Unknown terminal output stream."));
            }
            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);

            char command = 'A';

            switch(direction)
            {
            case Types::Cursor::MoveDirection::Up:
                command = 'A';
                break;
            case Types::Cursor::MoveDirection::Down:
                command = 'B';
                break;
            case Types::Cursor::MoveDirection::Left:
                command = 'D';
                break;
            case Types::Cursor::MoveDirection::Right:
                command = 'C';
                break;
            default:
                return Internal::result(invalidArgumentStatus("Unknown terminal cursor movement direction."));
            }

            if(amount == 0)
            {
                return Internal::result(flushIfRequested(stream, options.flushMode));
            }

            GameWIP::Status::Types::Status validationStatus = Platform::validateCursorMovement(stream, amount);

            if(!validationStatus.ok())
            {
                return Internal::result(validationStatus);
            }
            state.assembly.clear();
            state.assembly.append("\x1b[");
            appendUnsigned(state.assembly, amount);
            state.assembly.push_back(command);
            return Internal::result(writeAssembledControlSequenceUnlocked(
                stream,
                state,
                ControlFeature::CursorMovement,
                "Terminal cursor movement is unsupported for this output stream.",
                options.flushMode));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> setCursorPosition(Types::Cursor::Position position, const Types::Output::ControlOptions &options) noexcept
    {
        return Internal::result(setCursorPosition(Types::Output::Stream::Stdout, position, options));
    }

    GameWIP::Status::Types::Result<void> setCursorPosition(
        Types::Output::Stream stream,
        Types::Cursor::Position position,
        const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::result(invalidArgumentStatus("Unknown terminal output stream."));
            }

            GameWIP::Status::Types::Status validationStatus = Platform::validateCursorPosition(stream, position);

            if(!validationStatus.ok())
            {
                return Internal::result(validationStatus);
            }
            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);
            state.assembly.clear();
            state.assembly.append("\x1b[");
            appendUnsigned(state.assembly, static_cast<std::uint64_t>(position.row) + 1);
            state.assembly.push_back(';');
            appendUnsigned(state.assembly, static_cast<std::uint64_t>(position.column) + 1);
            state.assembly.push_back('H');

            return Internal::result(writeAssembledControlSequenceUnlocked(
                stream,
                state,
                ControlFeature::CursorMovement,
                "Terminal cursor positioning is unsupported for this output stream.",
                options.flushMode));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    Types::Cursor::PositionResult getCursorPosition(const Types::Cursor::QueryOptions &options) noexcept
    {
        return getCursorPosition(Types::Output::Stream::Stdout, Types::Input::Stream::Stdin, options);
    }

    Types::Cursor::PositionResult getCursorPosition(
        Types::Output::Stream outputStream,
        Types::Input::Stream responseStream,
        const Types::Cursor::QueryOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(outputStream) || !validInputStream(responseStream))
            {
                return Internal::failure<Types::Cursor::Position>(
                    invalidArgumentStatus("Unknown terminal stream selected for cursor position query."));
            }
            std::scoped_lock lock(outputState(outputStream).mutex, Internal::inputIoMutex(responseStream));

            GameWIP::Status::Types::Status status = flushIfRequested(outputStream, options.flushMode);

            if(!status.ok())
            {
                return Internal::failure<Types::Cursor::Position>(std::move(status));
            }
            return Internal::result(Platform::getCursorPosition(outputStream, responseStream, options));
        }
        catch(...)
        {
            return Internal::failure<Types::Cursor::Position>(Internal::exceptionStatus());
        }
    }

    Internal::PositionResult Internal::getLineRenderingCursorPosition(
        Types::Output::Stream outputStream,
        Types::Input::Stream inputStream) noexcept
    {
        try
        {
            if(!validOutputStream(outputStream) || !validInputStream(inputStream))
            {
                return Internal::failure<Types::Cursor::Position>(invalidArgumentStatus("Unknown terminal stream selected for line rendering."));
            }
            std::scoped_lock lock(outputState(outputStream).mutex, Internal::inputIoMutex(inputStream));
            return Platform::getLineRenderingCursorPosition(outputStream);
        }
        catch(...)
        {
            return Internal::failure<Types::Cursor::Position>(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Status Internal::setLineRenderingCursorPosition(Types::Output::Stream outputStream, Types::Cursor::Position position) noexcept
    {
        try
        {
            if(!validOutputStream(outputStream))
            {
                return invalidArgumentStatus("Unknown terminal output stream selected for line rendering.");
            }
            std::lock_guard lock(outputState(outputStream).mutex);
            return Platform::setLineRenderingCursorPosition(outputStream, position);
        }
        catch(...)
        {
            return Internal::exceptionStatus();
        }
    }

    GameWIP::Status::Types::Result<void> saveCursorPosition(const Types::Output::ControlOptions &options) noexcept
    {
        return Internal::result(saveCursorPosition(Types::Output::Stream::Stdout, options));
    }

    GameWIP::Status::Types::Result<void> saveCursorPosition(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::result(invalidArgumentStatus("Unknown terminal output stream."));
            }
            std::lock_guard lock(outputState(stream).mutex);
            return Internal::result(writeControlSequenceUnlocked(
                stream,
                "\x1b[s",
                ControlFeature::CursorSaveRestore,
                "Terminal cursor save is unsupported for this output stream.",
                options.flushMode));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> restoreCursorPosition(const Types::Output::ControlOptions &options) noexcept
    {
        return Internal::result(restoreCursorPosition(Types::Output::Stream::Stdout, options));
    }

    GameWIP::Status::Types::Result<void> restoreCursorPosition(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::result(invalidArgumentStatus("Unknown terminal output stream."));
            }
            std::lock_guard lock(outputState(stream).mutex);
            return Internal::result(writeControlSequenceUnlocked(
                stream,
                "\x1b[u",
                ControlFeature::CursorSaveRestore,
                "Terminal cursor restore is unsupported for this output stream.",
                options.flushMode));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> setCursorVisible(bool visible, const Types::Output::ControlOptions &options) noexcept
    {
        return Internal::result(setCursorVisible(Types::Output::Stream::Stdout, visible, options));
    }

    GameWIP::Status::Types::Result<void> setCursorVisible(Types::Output::Stream stream, bool visible, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::result(invalidArgumentStatus("Unknown terminal output stream."));
            }
            std::lock_guard lock(outputState(stream).mutex);
            return Internal::result(writeControlSequenceUnlocked(
                stream,
                visible ? "\x1b[?25h" : "\x1b[?25l",
                ControlFeature::CursorVisibility,
                "Terminal cursor visibility is unsupported for this output stream.",
                options.flushMode));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    CursorHiddenScope scopedCursorHidden(const Types::Output::ControlOptions &options) noexcept
    {
        return scopedCursorHidden(Types::Output::Stream::Stdout, options);
    }

    CursorHiddenScope scopedCursorHidden(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
    {
        CursorHiddenScope scope;
        scope.stream_ = stream;
        scope.options_ = options;

        if(!validOutputStream(stream))
        {
            scope.status_ = GameWIP::Terminal::Internal::backendStatus(ErrorCode::InvalidArgument);
            return scope;
        }

        if(!IO::isValidFlushMode(options.flushMode))
        {
            scope.status_ = GameWIP::Terminal::Internal::backendStatus(ErrorCode::InvalidArgument);
            return scope;
        }

        try
        {
            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);

            if(state.cursorHiddenScopeDepth == std::numeric_limits<std::size_t>::max())
            {
                scope.status_ = GameWIP::Terminal::Internal::backendStatus(ErrorCode::SizeLimitExceeded);
                return scope;
            }

            if(state.cursorHiddenScopeDepth == 0)
            {
                bool emitted = false;
                scope.status_ = writeControlSequenceUnlocked(
                    stream,
                    "\x1b[?25l",
                    ControlFeature::CursorVisibility,
                    "Terminal cursor visibility is unsupported for this output stream.",
                    options.flushMode,
                    &emitted);

                if(!emitted)
                {
                    return scope;
                }
            }

            ++state.cursorHiddenScopeDepth;
            scope.active_ = true;
        }
        catch(...)
        {
            scope.status_ = Internal::exceptionStatus();
        }

        return scope;
    }

    GameWIP::Status::Types::Result<void> clear(Types::Output::ClearTarget target, const Types::Output::ControlOptions &options) noexcept
    {
        return Internal::result(clear(Types::Output::Stream::Stdout, target, options));
    }

    GameWIP::Status::Types::Result<void> clear(Types::Output::Stream stream, Types::Output::ClearTarget target, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::result(invalidArgumentStatus("Unknown terminal output stream."));
            }
            std::lock_guard lock(outputState(stream).mutex);

            std::string_view sequence;

            switch(target)
            {
            case Types::Output::ClearTarget::EntireScreen:
                sequence = "\x1b[2J";
                break;
            case Types::Output::ClearTarget::ScreenBeforeCursor:
                sequence = "\x1b[1J";
                break;
            case Types::Output::ClearTarget::ScreenAfterCursor:
                sequence = "\x1b[0J";
                break;
            case Types::Output::ClearTarget::EntireScreenAndScrollback:
                sequence = "\x1b[3J";
                break;
            case Types::Output::ClearTarget::EntireLine:
                sequence = "\x1b[2K";
                break;
            case Types::Output::ClearTarget::LineBeforeCursor:
                sequence = "\x1b[1K";
                break;
            case Types::Output::ClearTarget::LineAfterCursor:
                sequence = "\x1b[0K";
                break;
            default:
                return Internal::result(invalidArgumentStatus("Unknown terminal clear target."));
            }

            return Internal::result(writeControlSequenceUnlocked(
                stream,
                sequence,
                ControlFeature::Clear,
                "Terminal clear controls are unsupported for this output stream.",
                options.flushMode));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> scroll(Types::Output::ScrollDirection direction, std::uint32_t lines, const Types::Output::ControlOptions &options) noexcept
    {
        return Internal::result(scroll(Types::Output::Stream::Stdout, direction, lines, options));
    }

    GameWIP::Status::Types::Result<void> scroll(
        Types::Output::Stream stream,
        Types::Output::ScrollDirection direction,
        std::uint32_t lines,
        const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::result(invalidArgumentStatus("Unknown terminal output stream."));
            }
            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);

            char command = 'S';

            switch(direction)
            {
            case Types::Output::ScrollDirection::Up:
                command = 'S';
                break;
            case Types::Output::ScrollDirection::Down:
                command = 'T';
                break;
            default:
                return Internal::result(invalidArgumentStatus("Unknown terminal scroll direction."));
            }

            if(lines == 0)
            {
                return Internal::result(flushIfRequested(stream, options.flushMode));
            }

            GameWIP::Status::Types::Status validationStatus = Platform::validateScroll(stream, lines);

            if(!validationStatus.ok())
            {
                return Internal::result(validationStatus);
            }
            state.assembly.clear();
            state.assembly.append("\x1b[");
            appendUnsigned(state.assembly, lines);
            state.assembly.push_back(command);
            return Internal::result(writeAssembledControlSequenceUnlocked(
                stream,
                state,
                ControlFeature::Scroll,
                "Terminal scrolling is unsupported for this output stream.",
                options.flushMode));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> enterAlternateScreen(const Types::Output::ControlOptions &options) noexcept
    {
        return Internal::result(enterAlternateScreen(Types::Output::Stream::Stdout, options));
    }

    GameWIP::Status::Types::Result<void> enterAlternateScreen(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::result(invalidArgumentStatus("Unknown terminal output stream."));
            }
            std::lock_guard lock(outputState(stream).mutex);
            return Internal::result(writeControlSequenceUnlocked(
                stream,
                "\x1b[?1049h",
                ControlFeature::AlternateScreen,
                "Terminal alternate screen is unsupported for this output stream.",
                options.flushMode));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> leaveAlternateScreen(const Types::Output::ControlOptions &options) noexcept
    {
        return Internal::result(leaveAlternateScreen(Types::Output::Stream::Stdout, options));
    }

    GameWIP::Status::Types::Result<void> leaveAlternateScreen(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::result(invalidArgumentStatus("Unknown terminal output stream."));
            }
            std::lock_guard lock(outputState(stream).mutex);
            return Internal::result(writeControlSequenceUnlocked(
                stream,
                "\x1b[?1049l",
                ControlFeature::AlternateScreen,
                "Terminal alternate screen is unsupported for this output stream.",
                options.flushMode));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    AlternateScreenScope scopedAlternateScreen(const Types::Output::ControlOptions &options) noexcept
    {
        return scopedAlternateScreen(Types::Output::Stream::Stdout, options);
    }

    AlternateScreenScope scopedAlternateScreen(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
    {
        AlternateScreenScope scope;
        scope.stream_ = stream;
        scope.options_ = options;

        if(!validOutputStream(stream))
        {
            scope.status_ = GameWIP::Terminal::Internal::backendStatus(ErrorCode::InvalidArgument);
            return scope;
        }

        if(!IO::isValidFlushMode(options.flushMode))
        {
            scope.status_ = GameWIP::Terminal::Internal::backendStatus(ErrorCode::InvalidArgument);
            return scope;
        }

        try
        {
            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);

            if(state.alternateScreenScopeDepth == std::numeric_limits<std::size_t>::max())
            {
                scope.status_ = GameWIP::Terminal::Internal::backendStatus(ErrorCode::SizeLimitExceeded);
                return scope;
            }

            if(state.alternateScreenScopeDepth == 0)
            {
                bool emitted = false;
                scope.status_ = writeControlSequenceUnlocked(
                    stream,
                    "\x1b[?1049h",
                    ControlFeature::AlternateScreen,
                    "Terminal alternate screen is unsupported for this output stream.",
                    options.flushMode,
                    &emitted);

                if(!emitted)
                {
                    return scope;
                }
            }

            ++state.alternateScreenScopeDepth;
            scope.active_ = true;
        }
        catch(...)
        {
            scope.status_ = Internal::exceptionStatus();
        }

        return scope;
    }

    GameWIP::Status::Types::Result<void> setTitle(std::string_view utf8Title, const Types::Output::ControlOptions &options) noexcept
    {
        return Internal::result(setTitle(Types::Output::Stream::Stdout, utf8Title, options));
    }

    GameWIP::Status::Types::Result<void> setTitle(Types::Output::Stream stream, std::string_view utf8Title, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::result(invalidArgumentStatus("Unknown terminal output stream."));
            }

            GameWIP::Status::Types::Status validationStatus = validateUtf8Text(utf8Title);

            if(!validationStatus.ok())
            {
                return Internal::result(validationStatus);
            }

            validationStatus = Platform::validateTitle(stream, utf8Title);

            if(!validationStatus.ok())
            {
                return Internal::result(validationStatus);
            }
            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);
            state.assembly.clear();
            state.assembly.append("\x1b]0;");
            appendSanitizedTitle(state.assembly, utf8Title);
            state.assembly.push_back('\x07');
            return Internal::result(writeAssembledControlSequenceUnlocked(
                stream,
                state,
                ControlFeature::Title,
                "Terminal title controls are unsupported for this output stream.",
                options.flushMode));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }

    GameWIP::Status::Types::Result<void> ringBell(const Types::Output::ControlOptions &options) noexcept
    {
        return Internal::result(ringBell(Types::Output::Stream::Stdout, options));
    }

    GameWIP::Status::Types::Result<void> ringBell(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if(!validOutputStream(stream))
            {
                return Internal::result(invalidArgumentStatus("Unknown terminal output stream."));
            }
            std::lock_guard lock(outputState(stream).mutex);
            return Internal::result(writeControlSequenceUnlocked(
                stream,
                "\a",
                ControlFeature::Bell,
                "Terminal bell output is unsupported for this output stream.",
                options.flushMode));
        }
        catch(...)
        {
            return Internal::result(Internal::exceptionStatus());
        }
    }
} // namespace GameWIP::Terminal
