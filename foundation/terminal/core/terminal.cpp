/// @file terminal.cpp
/// @brief Core implementation for the Terminal library.

#include "terminal/terminal.h"
#include "terminal/internal/terminal_input.h"
#include "terminal/internal/terminal_platform.h"
#include "unicode/unicode.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <exception>
#include <format>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace GameWIP::Terminal
{
    namespace
    {
        using ErrorCode = IO::Types::ErrorCode;

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

        /// @brief Validated decision describing whether one write emits style sequences.
        struct StylePlan
        {
            IO::Types::Status status = IO::successStatus();
            bool emitStyle = false;
        };

        /// @brief Per-output-stream serialization, reusable assembly storage, and nested scope depth.
        struct OutputState
        {
            std::mutex mutex;
            std::string assembly;
            std::string formatScratch;
            std::size_t cursorHiddenScopeDepth = 0;
            std::size_t alternateScreenScopeDepth = 0;
        };

        /// @brief Fixed-capacity SGR sequence assembled without heap allocation.
        struct StyleSequence
        {
            std::array<char, 96> bytes{};
            std::size_t size = 0;
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
                    if (text_.capacity() > kRetainedAssemblyLimit)
                    {
                        std::string{}.swap(text_);
                    }

                    std::lock_guard lock(state_.mutex);
                    if (text_.capacity() > state_.formatScratch.capacity())
                    {
                        text_.swap(state_.formatScratch);
                    }
                }
                catch (...)
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

        /// @brief Validates the currently supported standard input stream enum.
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
            switch (lineEnding)
            {
            case Types::Output::LineEnding::Native:
            case Types::Output::LineEnding::Lf:
            case Types::Output::LineEnding::CrLf:
                return true;
            }

            return false;
        }

        /// @brief Builds a diagnostic status without allowing diagnostic allocation to escape a checked boundary.
        [[nodiscard]] IO::Types::Status statusWithMessage(ErrorCode code, std::string_view message) noexcept
        {
            if (message.empty())
            {
                return IO::makeStatus(code);
            }

            try
            {
                return IO::makeStatus(code, 0, std::string(message));
            }
            catch (...)
            {
                return IO::makeStatus(code);
            }
        }

        /// @brief Copies a stored status without letting optional diagnostic allocation escape a noexcept boundary.
        [[nodiscard]] IO::Types::Status copyStatus(const IO::Types::Status &status) noexcept
        {
            if (status.message.empty())
            {
                return IO::makeStatus(status.code, status.nativeCode);
            }

            try
            {
                return IO::makeStatus(status.code, status.nativeCode, status.message);
            }
            catch (...)
            {
                return IO::makeStatus(status.code, status.nativeCode);
            }
        }

        /// @brief Maps an exception raised by Terminal-owned checked work to a portable status.
        [[nodiscard]] IO::Types::Status exceptionStatus() noexcept
        {
            try
            {
                throw;
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (const std::length_error &)
            {
                return IO::makeStatus(ErrorCode::SizeLimitExceeded);
            }
            catch (const std::format_error &)
            {
                return IO::makeStatus(ErrorCode::InvalidArgument);
            }
            catch (...)
            {
                return IO::makeStatus(ErrorCode::Unknown);
            }
        }

        /// @brief Validates one public Terminal text payload before any output-side effect.
        [[nodiscard]] IO::Types::Status validateUtf8Text(std::string_view text) noexcept
        {
            return Unicode::Utf8::validate(text).outcome == Unicode::Types::ValidationOutcome::Valid ? IO::successStatus()
                                                                                                     : IO::makeStatus(ErrorCode::EncodingFailed);
        }

        /// @brief Builds an Unsupported status with optional diagnostic text.
        [[nodiscard]] IO::Types::Status unsupportedStatus(std::string_view message = {}) noexcept
        {
            return statusWithMessage(ErrorCode::Unsupported, message);
        }

        /// @brief Builds an InvalidArgument status with optional diagnostic text.
        [[nodiscard]] IO::Types::Status invalidArgumentStatus(std::string_view message = {}) noexcept
        {
            return statusWithMessage(ErrorCode::InvalidArgument, message);
        }

        /// @brief Returns whether a color requests the terminal default.
        [[nodiscard]] bool isDefaultColor(const Types::Style::Color &color) noexcept
        {
            return color.kind() == Types::Style::ColorKind::Default;
        }

        /// @brief Returns whether a style needs no SGR output.
        [[nodiscard]] bool isDefaultStyle(const Types::Style::Request &style) noexcept
        {
            return isDefaultColor(style.foreground) && isDefaultColor(style.background) && !style.bold && !style.dim && !style.italic &&
                   !style.underline && !style.inverse && !style.strikethrough;
        }

        /// @brief Checks one color request against observed stream capabilities.
        [[nodiscard]] bool colorSupported(const Types::Style::Color &color, const Types::Style::Capabilities &capabilities) noexcept
        {
            switch (color.kind())
            {
            case Types::Style::ColorKind::Default:
                return true;
            case Types::Style::ColorKind::Basic:
                return capabilities.basicColor;
            case Types::Style::ColorKind::Rgb:
                return capabilities.rgbColor;
            }

            return false;
        }

        /// @brief Checks every requested style attribute against stream capabilities.
        [[nodiscard]] bool styleSupported(const Types::Style::Request &style, const Types::Style::Capabilities &capabilities) noexcept
        {
            return colorSupported(style.foreground, capabilities) && colorSupported(style.background, capabilities) &&
                   (!style.bold || capabilities.bold) && (!style.dim || capabilities.dim) && (!style.italic || capabilities.italic) &&
                   (!style.underline || capabilities.underline) && (!style.inverse || capabilities.inverse) &&
                   (!style.strikethrough || capabilities.strikethrough);
        }

        /// @brief Returns whether a stream supports at least one style feature.
        [[nodiscard]] bool anyStyleSupported(const Types::Style::Capabilities &capabilities) noexcept
        {
            return capabilities.basicColor || capabilities.rgbColor || capabilities.bold || capabilities.dim || capabilities.italic ||
                   capabilities.underline || capabilities.inverse || capabilities.strikethrough;
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

        /// @brief Resolves a validated line-ending policy to emitted bytes.
        [[nodiscard]] std::string_view lineEndingText(Types::Output::LineEnding lineEnding) noexcept
        {
            switch (lineEnding)
            {
            case Types::Output::LineEnding::Native:
                return Detail::Platform::nativeLineEnding();
            case Types::Output::LineEnding::Lf:
                return "\n";
            case Types::Output::LineEnding::CrLf:
                return "\r\n";
            }

            return {};
        }

        /// @brief Converts a basic color to its foreground or background SGR code.
        [[nodiscard]] int basicColorCode(Types::Style::BasicColor color, bool foreground) noexcept
        {
            const int value = static_cast<int>(color);
            const bool bright = value >= 8;
            const int colorIndex = bright ? value - 8 : value;
            return (foreground ? (bright ? 90 : 30) : (bright ? 100 : 40)) + colorIndex;
        }

        /// @brief Clears assembly text and releases unusually large retained capacity.
        void releaseLargeAssembly(OutputState &state) noexcept
        {
            state.assembly.clear();
            if (state.assembly.capacity() > kRetainedAssemblyLimit)
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

        /// @brief Appends known-bounded literal bytes to a style sequence.
        void appendSequenceText(StyleSequence &sequence, std::string_view text) noexcept
        {
            for (const char character : text)
            {
                sequence.bytes[sequence.size++] = character;
            }
        }

        /// @brief Appends one bounded decimal parameter to a style sequence.
        void appendSequenceNumber(StyleSequence &sequence, std::uint32_t value) noexcept
        {
            const std::span available = std::span{sequence.bytes}.subspan(sequence.size);
            const auto result = std::to_chars(available.data(), std::to_address(available.end()), value);
            sequence.size += static_cast<std::size_t>(std::distance(available.data(), result.ptr));
        }

        /// @brief Appends one semicolon-delimited SGR parameter.
        void appendSgrParameter(StyleSequence &sequence, std::uint32_t parameter, bool &hasParameter) noexcept
        {
            if (hasParameter)
            {
                sequence.bytes[sequence.size++] = ';';
            }
            appendSequenceNumber(sequence, parameter);
            hasParameter = true;
        }

        /// @brief Appends basic or RGB SGR parameters for one color channel.
        void appendColorSgr(StyleSequence &sequence, const Types::Style::Color &color, bool foreground, bool &hasParameter) noexcept
        {
            switch (color.kind())
            {
            case Types::Style::ColorKind::Default:
                return;
            case Types::Style::ColorKind::Basic:
                appendSgrParameter(sequence, static_cast<std::uint32_t>(basicColorCode(color.basic(), foreground)), hasParameter);
                return;
            case Types::Style::ColorKind::Rgb:
                appendSgrParameter(sequence, foreground ? 38U : 48U, hasParameter);
                appendSgrParameter(sequence, 2, hasParameter);
                appendSgrParameter(sequence, color.red(), hasParameter);
                appendSgrParameter(sequence, color.green(), hasParameter);
                appendSgrParameter(sequence, color.blue(), hasParameter);
                return;
            }
        }

        /// @brief Builds the complete SGR prefix for a previously validated style.
        [[nodiscard]] StyleSequence makeStyleSequence(const Types::Style::Request &style) noexcept
        {
            StyleSequence sequence;
            appendSequenceText(sequence, "\x1b[");
            bool hasParameter = false;

            if (style.bold)
            {
                appendSgrParameter(sequence, 1, hasParameter);
            }
            if (style.dim)
            {
                appendSgrParameter(sequence, 2, hasParameter);
            }
            if (style.italic)
            {
                appendSgrParameter(sequence, 3, hasParameter);
            }
            if (style.underline)
            {
                appendSgrParameter(sequence, 4, hasParameter);
            }
            if (style.inverse)
            {
                appendSgrParameter(sequence, 7, hasParameter);
            }
            if (style.strikethrough)
            {
                appendSgrParameter(sequence, 9, hasParameter);
            }

            appendColorSgr(sequence, style.foreground, true, hasParameter);
            appendColorSgr(sequence, style.background, false, hasParameter);

            if (!hasParameter)
            {
                sequence.size = 0;
                return sequence;
            }

            sequence.bytes[sequence.size++] = 'm';
            return sequence;
        }

        /// @brief Resolves style-mode fallback or failure against known capabilities.
        [[nodiscard]] StylePlan stylePlanForCapabilities(
            Types::Style::Mode mode,
            const Types::Style::Request &style,
            const Types::Style::Capabilities &capabilities)
        {
            switch (mode)
            {
            case Types::Style::Mode::Never:
                return {};
            case Types::Style::Mode::Auto:
            case Types::Style::Mode::Required:
                break;
            default:
                return {.status = invalidArgumentStatus("Unknown terminal style mode.")};
            }

            if (isDefaultStyle(style))
            {
                return {};
            }

            if (styleSupported(style, capabilities))
            {
                return {.status = IO::successStatus(), .emitStyle = true};
            }

            if (mode == Types::Style::Mode::Required)
            {
                return {.status = unsupportedStatus("Terminal output stream does not support the requested style.")};
            }

            return {};
        }

        /// @brief Prepares output when needed and resolves the style plan for one stream.
        [[nodiscard]] StylePlan stylePlan(Types::Output::Stream stream, Types::Style::Mode mode, const Types::Style::Request &style)
        {
            switch (mode)
            {
            case Types::Style::Mode::Never:
                return {};
            case Types::Style::Mode::Auto:
            case Types::Style::Mode::Required:
                break;
            default:
                return {.status = invalidArgumentStatus("Unknown terminal style mode.")};
            }

            if (isDefaultStyle(style))
            {
                return {};
            }
            if (mode == Types::Style::Mode::Never)
            {
                return {};
            }

            Types::Output::CapabilitiesResult capabilities = Detail::Platform::getOutputCapabilities(stream);
            if (capabilities.status.ok())
            {
                StylePlan plan = stylePlanForCapabilities(mode, style, capabilities.capabilities.style);
                if (plan.status.ok() && plan.emitStyle)
                {
                    return plan;
                }
                if (capabilities.capabilities.kind == Types::StreamKind::Redirected)
                {
                    return plan;
                }
            }

            capabilities = Detail::Platform::prepareOutput(stream);
            if (capabilities.status.ok())
            {
                StylePlan plan = stylePlanForCapabilities(mode, style, capabilities.capabilities.style);
                if (plan.status.ok() && plan.emitStyle)
                {
                    return plan;
                }
            }

            if (mode == Types::Style::Mode::Auto)
            {
                return {};
            }
            if (!capabilities.status.ok())
            {
                return {.status = capabilities.status};
            }
            return {.status = unsupportedStatus("Terminal output stream does not support the requested style.")};
        }

        /// @brief Applies an optional validated flush after a logical write.
        [[nodiscard]] IO::Types::Status flushIfRequested(Types::Output::Stream stream, IO::Types::FlushMode mode)
        {
            if (!IO::isValidFlushMode(mode))
            {
                return invalidArgumentStatus("Unknown IO flush mode.");
            }

            if (mode == IO::Types::FlushMode::None)
            {
                return IO::successStatus();
            }

            return Detail::Platform::flush(stream, mode);
        }

        /// @brief Writes and clears a stream's assembled record, then performs the requested flush.
        [[nodiscard]] IO::Types::Status writeAssembly(Types::Output::Stream stream, OutputState &state, IO::Types::FlushMode flushMode)
        {
            IO::Types::Status status = IO::successStatus();
            if (!state.assembly.empty())
            {
                status = Detail::Platform::writeText(stream, state.assembly);
            }
            if (status.ok())
            {
                status = flushIfRequested(stream, flushMode);
            }

            releaseLargeAssembly(state);
            return status;
        }

        /// @brief Implements one serialized styled text write without acquiring the stream mutex.
        [[nodiscard]] IO::Types::Status writeTextUnlocked(
            Types::Output::Stream stream,
            OutputState &state,
            std::string_view utf8Text,
            const Types::Output::TextOptions &options)
        {
            if (!IO::isValidFlushMode(options.flushMode))
            {
                return invalidArgumentStatus("Unknown IO flush mode.");
            }

            IO::Types::Status textStatus = validateUtf8Text(utf8Text);
            if (!textStatus.ok())
            {
                return textStatus;
            }

            const StylePlan plan = stylePlan(stream, options.styleMode, options.style);
            if (!plan.status.ok())
            {
                return plan.status;
            }

            if (!plan.emitStyle)
            {
                IO::Types::Status status = Detail::Platform::writeText(stream, utf8Text);
                return status.ok() ? flushIfRequested(stream, options.flushMode) : status;
            }

            const StyleSequence prefix = makeStyleSequence(options.style);
            state.assembly.clear();
            state.assembly.append(prefix.bytes.data(), prefix.size);
            state.assembly.append(utf8Text);
            state.assembly.append("\x1b[0m");
            return writeAssembly(stream, state, options.flushMode);
        }

        /// @brief Implements one serialized styled text write followed by the selected line ending.
        [[nodiscard]] IO::Types::Status writeLineUnlocked(
            Types::Output::Stream stream,
            OutputState &state,
            std::string_view utf8Text,
            const Types::Output::LineOptions &options)
        {
            if (!validLineEnding(options.lineEnding))
            {
                return invalidArgumentStatus("Unknown terminal line ending.");
            }
            if (!IO::isValidFlushMode(options.flushMode))
            {
                return invalidArgumentStatus("Unknown IO flush mode.");
            }

            IO::Types::Status textStatus = validateUtf8Text(utf8Text);
            if (!textStatus.ok())
            {
                return textStatus;
            }

            const StylePlan plan = stylePlan(stream, options.styleMode, options.style);
            if (!plan.status.ok())
            {
                return plan.status;
            }

            state.assembly.clear();
            if (plan.emitStyle)
            {
                const StyleSequence prefix = makeStyleSequence(options.style);
                state.assembly.append(prefix.bytes.data(), prefix.size);
            }
            state.assembly.append(utf8Text);
            if (plan.emitStyle)
            {
                state.assembly.append("\x1b[0m");
            }
            state.assembly.append(lineEndingText(options.lineEnding));
            return writeAssembly(stream, state, options.flushMode);
        }

        /// @brief Implements one serialized raw-byte write and optional flush.
        [[nodiscard]] IO::Types::WriteResult writeBytesUnlocked(
            Types::Output::Stream stream,
            std::span<const std::byte> bytes,
            const Types::Output::ByteOptions &options)
        {
            if (!IO::isValidFlushMode(options.flushMode))
            {
                return {.status = invalidArgumentStatus("Unknown IO flush mode."), .bytesWritten = 0};
            }

            IO::Types::WriteResult result = Detail::Platform::writeBytes(stream, bytes);
            if (!result.status.ok())
            {
                return result;
            }

            result.status = flushIfRequested(stream, options.flushMode);
            return result;
        }

        /// @brief Validates segmented-write kinds, styles, and flush options before emission.
        [[nodiscard]] IO::Types::Status validateSegments(
            std::span<const Types::Output::Segment> segments,
            Types::Style::Mode styleMode,
            const Types::Output::Capabilities &capabilities)
        {
            for (const Types::Output::Segment &segment : segments)
            {
                switch (segment.kind())
                {
                case Types::Output::SegmentKind::Text:
                    break;
                case Types::Output::SegmentKind::Bytes:
                    if (!capabilities.supportsByteOutput)
                    {
                        return unsupportedStatus("Terminal byte output is unsupported for this output stream.");
                    }
                    break;
                case Types::Output::SegmentKind::StyledText:
                {
                    const StylePlan plan = stylePlanForCapabilities(styleMode, segment.style(), capabilities.style);
                    if (!plan.status.ok())
                    {
                        return plan.status;
                    }
                    break;
                }
                default:
                    return invalidArgumentStatus("Unknown terminal write segment kind.");
                }
            }

            return IO::successStatus();
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
            for (const Types::Output::Segment &segment : segments)
            {
                if (segment.kind() == Types::Output::SegmentKind::Bytes)
                {
                    requirements.hasBytes = true;
                }
                else if (
                    styleMode != Types::Style::Mode::Never && segment.kind() == Types::Output::SegmentKind::StyledText &&
                    !isDefaultStyle(segment.style()))
                {
                    requirements.needsStyleCapabilities = true;
                }
            }
            return requirements;
        }

        /// @brief Validates, assembles, and emits one logical mixed text/style/byte record under the stream lock.
        /// @details Validation finishes before intentional emission begins, but the platform write itself is not
        /// transactional and can still emit a prefix before reporting failure.
        [[nodiscard]] IO::Types::Status writeSegmentsUnlocked(
            Types::Output::Stream stream,
            OutputState &state,
            std::span<const Types::Output::Segment> segments,
            const Types::Output::SegmentOptions &options)
        {
            if (options.appendLineEnding && !validLineEnding(options.lineEnding))
            {
                return invalidArgumentStatus("Unknown terminal line ending.");
            }
            if (!IO::isValidFlushMode(options.flushMode))
            {
                return invalidArgumentStatus("Unknown IO flush mode.");
            }

            switch (options.styleMode)
            {
            case Types::Style::Mode::Never:
            case Types::Style::Mode::Auto:
            case Types::Style::Mode::Required:
                break;
            default:
                return invalidArgumentStatus("Unknown terminal style mode.");
            }

            for (const Types::Output::Segment &segment : segments)
            {
                if ((segment.kind() == Types::Output::SegmentKind::Text || segment.kind() == Types::Output::SegmentKind::StyledText))
                {
                    IO::Types::Status textStatus = validateUtf8Text(segment.text());
                    if (!textStatus.ok())
                    {
                        return textStatus;
                    }
                }
            }

            const SegmentRequirements requirements = segmentRequirements(segments, options.styleMode);
            Types::Output::CapabilitiesResult capabilityResult;

            if (requirements.hasBytes || requirements.needsStyleCapabilities)
            {
                capabilityResult = Detail::Platform::getOutputCapabilities(stream);
                if (!capabilityResult.status.ok())
                {
                    if (requirements.hasBytes)
                    {
                        return capabilityResult.status;
                    }

                    Types::Output::CapabilitiesResult prepared = Detail::Platform::prepareOutput(stream);
                    if (prepared.status.ok())
                    {
                        capabilityResult = std::move(prepared);
                    }
                    else if (options.styleMode == Types::Style::Mode::Required)
                    {
                        return prepared.status;
                    }
                    else
                    {
                        capabilityResult = {};
                    }
                }
                else if (requirements.needsStyleCapabilities)
                {
                    const bool allStylesSupported = std::all_of(
                        segments.begin(),
                        segments.end(),
                        [&capabilityResult](const Types::Output::Segment &segment)
                        {
                            return segment.kind() != Types::Output::SegmentKind::StyledText || isDefaultStyle(segment.style()) ||
                                   styleSupported(segment.style(), capabilityResult.capabilities.style);
                        });
                    if (!allStylesSupported)
                    {
                        if (capabilityResult.capabilities.kind != Types::StreamKind::Redirected)
                        {
                            Types::Output::CapabilitiesResult prepared = Detail::Platform::prepareOutput(stream);
                            if (prepared.status.ok())
                            {
                                capabilityResult = std::move(prepared);
                            }
                            else if (options.styleMode == Types::Style::Mode::Required)
                            {
                                return prepared.status;
                            }
                        }
                    }
                }
            }

            IO::Types::Status status = validateSegments(segments, options.styleMode, capabilityResult.capabilities);
            if (!status.ok())
            {
                return status;
            }

            if (segments.size() == 1 && segments.front().kind() == Types::Output::SegmentKind::Text && !segments.front().text().empty() &&
                !options.appendLineEnding)
            {
                status = Detail::Platform::writeText(stream, segments.front().text());
                return status.ok() ? flushIfRequested(stream, options.flushMode) : status;
            }

            state.assembly.clear();
            for (const Types::Output::Segment &segment : segments)
            {
                switch (segment.kind())
                {
                case Types::Output::SegmentKind::Text:
                    state.assembly.append(segment.text());
                    break;
                case Types::Output::SegmentKind::StyledText:
                {
                    const StylePlan plan = stylePlanForCapabilities(options.styleMode, segment.style(), capabilityResult.capabilities.style);
                    if (plan.emitStyle)
                    {
                        const StyleSequence prefix = makeStyleSequence(segment.style());
                        state.assembly.append(prefix.bytes.data(), prefix.size);
                    }
                    state.assembly.append(segment.text());
                    if (plan.emitStyle)
                    {
                        state.assembly.append("\x1b[0m");
                    }
                    break;
                }
                case Types::Output::SegmentKind::Bytes:
                    if (!segment.bytes().empty())
                    {
                        state.assembly.append(reinterpret_cast<const char *>(segment.bytes().data()), segment.bytes().size());
                    }
                    break;
                }
            }

            if (options.appendLineEnding)
            {
                state.assembly.append(lineEndingText(options.lineEnding));
            }

            if (requirements.hasBytes)
            {
                const auto assembled = std::as_bytes(std::span{state.assembly});
                IO::Types::WriteResult result = Detail::Platform::writeBytes(stream, assembled);
                IO::Types::Status writeStatus = std::move(result.status);
                if (writeStatus.ok() && result.bytesWritten != assembled.size())
                {
                    writeStatus = IO::makeStatus(ErrorCode::PartialWrite);
                }
                if (writeStatus.ok())
                {
                    writeStatus = flushIfRequested(stream, options.flushMode);
                }
                releaseLargeAssembly(state);
                return writeStatus;
            }

            return writeAssembly(stream, state, options.flushMode);
        }

        /// @brief Maps one control operation to its required output capability.
        [[nodiscard]] bool controlFeatureSupported(const Types::Output::Capabilities &capabilities, ControlFeature feature) noexcept
        {
            switch (feature)
            {
            case ControlFeature::StyleReset:
                return anyStyleSupported(capabilities.style);
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
        [[nodiscard]] IO::Types::Status writeControlSequenceUnlocked(
            Types::Output::Stream stream,
            std::string_view sequence,
            ControlFeature feature,
            std::string_view unsupportedMessage,
            IO::Types::FlushMode flushMode,
            bool *emitted = nullptr)
        {
            if (emitted != nullptr)
            {
                *emitted = false;
            }
            if (!IO::isValidFlushMode(flushMode))
            {
                return invalidArgumentStatus("Unknown IO flush mode.");
            }

            Types::Output::CapabilitiesResult capabilities = Detail::Platform::getOutputCapabilities(stream);
            if (!capabilities.status.ok())
            {
                return capabilities.status;
            }

            if (!controlFeatureSupported(capabilities.capabilities, feature))
            {
                capabilities = Detail::Platform::prepareOutput(stream);
                if (!capabilities.status.ok())
                {
                    return capabilities.status;
                }
                if (!controlFeatureSupported(capabilities.capabilities, feature))
                {
                    return unsupportedStatus(unsupportedMessage);
                }
            }

            IO::Types::Status status = Detail::Platform::writeText(stream, sequence);
            if (!status.ok())
            {
                return status;
            }

            if (emitted != nullptr)
            {
                *emitted = true;
            }

            return flushIfRequested(stream, flushMode);
        }

        /// @brief Emits a control sequence already assembled in reusable stream storage.
        [[nodiscard]] IO::Types::Status writeAssembledControlSequenceUnlocked(
            Types::Output::Stream stream,
            OutputState &state,
            ControlFeature feature,
            std::string_view unsupportedMessage,
            IO::Types::FlushMode flushMode)
        {
            IO::Types::Status status = writeControlSequenceUnlocked(stream, state.assembly, feature, unsupportedMessage, flushMode);
            releaseLargeAssembly(state);
            return status;
        }

        /// @brief Decrements nested cursor-hide depth and restores visibility at the outer boundary.
        [[nodiscard]] IO::Types::Status restoreCursorHiddenScope(
            Types::Output::Stream stream,
            const Types::Output::ControlOptions &options,
            bool &restored) noexcept
        {
            restored = false;
            try
            {
                OutputState &state = outputState(stream);
                std::lock_guard lock(state.mutex);

                if (state.cursorHiddenScopeDepth == 0)
                {
                    restored = true;
                    return IO::successStatus();
                }

                if (state.cursorHiddenScopeDepth > 1)
                {
                    --state.cursorHiddenScopeDepth;
                    restored = true;
                    return IO::successStatus();
                }

                bool emitted = false;
                IO::Types::Status status = writeControlSequenceUnlocked(
                    stream,
                    "\x1b[?25h",
                    ControlFeature::CursorVisibility,
                    "Terminal cursor visibility is unsupported for this output stream.",
                    options.flushMode,
                    &emitted);
                if (emitted)
                {
                    state.cursorHiddenScopeDepth = 0;
                    restored = true;
                }
                return status;
            }
            catch (...)
            {
                return exceptionStatus();
            }
        }

        /// @brief Decrements nested alternate-screen depth and leaves at the outer boundary.
        [[nodiscard]] IO::Types::Status leaveAlternateScreenScope(
            Types::Output::Stream stream,
            const Types::Output::ControlOptions &options,
            bool &restored) noexcept
        {
            restored = false;
            try
            {
                OutputState &state = outputState(stream);
                std::lock_guard lock(state.mutex);

                if (state.alternateScreenScopeDepth == 0)
                {
                    restored = true;
                    return IO::successStatus();
                }

                if (state.alternateScreenScopeDepth > 1)
                {
                    --state.alternateScreenScopeDepth;
                    restored = true;
                    return IO::successStatus();
                }

                bool emitted = false;
                IO::Types::Status status = writeControlSequenceUnlocked(
                    stream,
                    "\x1b[?1049l",
                    ControlFeature::AlternateScreen,
                    "Terminal alternate screen is unsupported for this output stream.",
                    options.flushMode,
                    &emitted);
                if (emitted)
                {
                    state.alternateScreenScopeDepth = 0;
                    restored = true;
                }
                return status;
            }
            catch (...)
            {
                return exceptionStatus();
            }
        }

        /// @brief Releases one cursor-hide nesting obligation after destructor restoration has failed.
        void abandonCursorHiddenScope(Types::Output::Stream stream) noexcept
        {
            try
            {
                OutputState &state = outputState(stream);
                std::lock_guard lock(state.mutex);
                if (state.cursorHiddenScopeDepth > 0)
                {
                    --state.cursorHiddenScopeDepth;
                }
            }
            catch (...)
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
                if (state.alternateScreenScopeDepth > 0)
                {
                    --state.alternateScreenScopeDepth;
                }
            }
            catch (...)
            {
                // Destruction-time bookkeeping release is best effort and cannot be reported.
                return;
            }
        }

        /// @brief Appends title text while removing control-sequence terminators and unsafe controls.
        void appendSanitizedTitle(std::string &output, std::string_view utf8Title)
        {
            for (const char character : utf8Title)
            {
                const auto byte = static_cast<unsigned char>(character);
                if (byte == 0x1b || byte == 0x07 || byte < 0x20 || byte == 0x7f)
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

    Types::Output::Segment::Segment(
        Types::Output::SegmentKind kind,
        std::string_view text,
        std::span<const std::byte> bytes,
        const Types::Style::Request &style) noexcept
        : kind_(kind)
        , text_(text)
        , bytes_(bytes)
        , style_(style)
    {
    }

    Types::Output::Segment textSegment(std::string_view text) noexcept
    {
        return Types::Output::Segment(Types::Output::SegmentKind::Text, text, {}, {});
    }

    Types::Output::Segment styledTextSegment(std::string_view text, const Types::Style::Request &style) noexcept
    {
        return Types::Output::Segment(Types::Output::SegmentKind::StyledText, text, {}, style);
    }

    Types::Output::Segment byteSegment(std::span<const std::byte> bytes) noexcept
    {
        return Types::Output::Segment(Types::Output::SegmentKind::Bytes, {}, bytes, {});
    }

    AlternateScreenScope::AlternateScreenScope() noexcept = default;

    AlternateScreenScope::AlternateScreenScope(AlternateScreenScope &&other) noexcept
        : stream_(other.stream_)
        , options_(other.options_)
        , status_(std::move(other.status_))
        , active_(other.active_)
        , restorationEmitted_(other.restorationEmitted_)
        , restoreOnDestruction_(other.restoreOnDestruction_)
    {
        other.active_ = false;
        other.restorationEmitted_ = false;
        other.restoreOnDestruction_ = true;
    }

    AlternateScreenScope &AlternateScreenScope::operator=(AlternateScreenScope &&other) noexcept
    {
        if (this != &other)
        {
            if (active_)
            {
                // Preserve ownership on failure instead of silently losing an alternate-screen leave obligation.
                static_cast<void>(leave());
                if (active_)
                {
                    return *this;
                }
            }
            stream_ = other.stream_;
            options_ = other.options_;
            status_ = std::move(other.status_);
            active_ = other.active_;
            restorationEmitted_ = other.restorationEmitted_;
            restoreOnDestruction_ = other.restoreOnDestruction_;
            other.active_ = false;
            other.restorationEmitted_ = false;
            other.restoreOnDestruction_ = true;
        }

        return *this;
    }

    AlternateScreenScope::~AlternateScreenScope() noexcept
    {
        if (restoreOnDestruction_)
        {
            static_cast<void>(leave());
        }
        if (active_)
        {
            abandonAlternateScreenScope(stream_);
            active_ = false;
            restorationEmitted_ = false;
        }
    }

    bool AlternateScreenScope::active() const noexcept
    {
        return active_;
    }

    const IO::Types::Status &AlternateScreenScope::status() const noexcept
    {
        return status_;
    }

    IO::Types::Status AlternateScreenScope::leave() noexcept
    {
        if (active_)
        {
            if (restorationEmitted_)
            {
                status_ = Terminal::flush(stream_, options_.flushMode);
                if (status_.ok())
                {
                    active_ = false;
                    restorationEmitted_ = false;
                }
            }
            else
            {
                bool restored = false;
                status_ = leaveAlternateScreenScope(stream_, options_, restored);
                if (restored)
                {
                    if (status_.ok())
                    {
                        active_ = false;
                    }
                    else
                    {
                        restorationEmitted_ = true;
                    }
                }
            }
        }

        return copyStatus(status_);
    }

    CursorHiddenScope::CursorHiddenScope() noexcept = default;

    CursorHiddenScope::CursorHiddenScope(CursorHiddenScope &&other) noexcept
        : stream_(other.stream_)
        , options_(other.options_)
        , status_(std::move(other.status_))
        , active_(other.active_)
        , restorationEmitted_(other.restorationEmitted_)
        , restoreOnDestruction_(other.restoreOnDestruction_)
    {
        other.active_ = false;
        other.restorationEmitted_ = false;
        other.restoreOnDestruction_ = true;
    }

    CursorHiddenScope &CursorHiddenScope::operator=(CursorHiddenScope &&other) noexcept
    {
        if (this != &other)
        {
            if (active_)
            {
                // Preserve ownership on failure instead of silently losing a cursor-visibility restoration obligation.
                static_cast<void>(restore());
                if (active_)
                {
                    return *this;
                }
            }
            stream_ = other.stream_;
            options_ = other.options_;
            status_ = std::move(other.status_);
            active_ = other.active_;
            restorationEmitted_ = other.restorationEmitted_;
            restoreOnDestruction_ = other.restoreOnDestruction_;
            other.active_ = false;
            other.restorationEmitted_ = false;
            other.restoreOnDestruction_ = true;
        }

        return *this;
    }

    CursorHiddenScope::~CursorHiddenScope() noexcept
    {
        if (restoreOnDestruction_)
        {
            static_cast<void>(restore());
        }
        if (active_)
        {
            abandonCursorHiddenScope(stream_);
            active_ = false;
            restorationEmitted_ = false;
        }
    }

    bool CursorHiddenScope::active() const noexcept
    {
        return active_;
    }

    const IO::Types::Status &CursorHiddenScope::status() const noexcept
    {
        return status_;
    }

    IO::Types::Status CursorHiddenScope::restore() noexcept
    {
        if (active_)
        {
            if (restorationEmitted_)
            {
                status_ = Terminal::flush(stream_, options_.flushMode);
                if (status_.ok())
                {
                    active_ = false;
                    restorationEmitted_ = false;
                }
            }
            else
            {
                bool restored = false;
                status_ = restoreCursorHiddenScope(stream_, options_, restored);
                if (restored)
                {
                    if (status_.ok())
                    {
                        active_ = false;
                    }
                    else
                    {
                        restorationEmitted_ = true;
                    }
                }
            }
        }

        return copyStatus(status_);
    }

    Types::Input::CapabilitiesResult getInputCapabilities() noexcept
    {
        return getInputCapabilities(Types::Input::Stream::Stdin);
    }

    Types::Input::CapabilitiesResult getInputCapabilities(Types::Input::Stream stream) noexcept
    {
        try
        {
            if (!validInputStream(stream))
            {
                return {.status = invalidArgumentStatus("Unknown terminal input stream."), .capabilities = {}};
            }

            std::lock_guard lock(Detail::inputIoMutex(stream));
            return Detail::Platform::getInputCapabilities(stream);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .capabilities = {}};
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
            if (!validOutputStream(stream))
            {
                return {.status = invalidArgumentStatus("Unknown terminal output stream."), .capabilities = {}};
            }

            std::lock_guard lock(outputState(stream).mutex);
            return Detail::Platform::getOutputCapabilities(stream);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .capabilities = {}};
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
            if (!validOutputStream(stream))
            {
                return {.status = invalidArgumentStatus("Unknown terminal output stream."), .capabilities = {}};
            }

            std::lock_guard lock(outputState(stream).mutex);
            return Detail::Platform::prepareOutput(stream);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .capabilities = {}};
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
            if (!validOutputStream(stream))
            {
                return {.status = invalidArgumentStatus("Unknown terminal output stream."), .size = {}};
            }

            std::lock_guard lock(outputState(stream).mutex);
            return Detail::Platform::getTerminalSize(stream);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .size = {}};
        }
    }

    IO::Types::Status writeText(std::string_view utf8Text, const Types::Output::TextOptions &options) noexcept
    {
        return writeText(Types::Output::Stream::Stdout, utf8Text, options);
    }

    IO::Types::Status writeText(Types::Output::Stream stream, std::string_view utf8Text, const Types::Output::TextOptions &options) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }

            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);
            return writeTextUnlocked(stream, state, utf8Text, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status writeLine(std::string_view utf8Text, const Types::Output::LineOptions &options) noexcept
    {
        return writeLine(Types::Output::Stream::Stdout, utf8Text, options);
    }

    IO::Types::Status writeLine(Types::Output::Stream stream, std::string_view utf8Text, const Types::Output::LineOptions &options) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }

            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);
            return writeLineUnlocked(stream, state, utf8Text, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::WriteResult writeBytes(std::span<const std::byte> bytes, const Types::Output::ByteOptions &options) noexcept
    {
        return writeBytes(Types::Output::Stream::Stdout, bytes, options);
    }

    IO::Types::WriteResult writeBytes(
        Types::Output::Stream stream,
        std::span<const std::byte> bytes,
        const Types::Output::ByteOptions &options) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return {.status = invalidArgumentStatus("Unknown terminal output stream."), .bytesWritten = 0};
            }

            std::lock_guard lock(outputState(stream).mutex);
            return writeBytesUnlocked(stream, bytes, options);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .bytesWritten = 0};
        }
    }

    IO::Types::Status writeSegments(std::span<const Types::Output::Segment> segments, const Types::Output::SegmentOptions &options) noexcept
    {
        return writeSegments(Types::Output::Stream::Stdout, segments, options);
    }

    IO::Types::Status writeSegments(
        Types::Output::Stream stream,
        std::span<const Types::Output::Segment> segments,
        const Types::Output::SegmentOptions &options) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }

            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);
            return writeSegmentsUnlocked(stream, state, segments, options);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    namespace Detail
    {
        IO::Types::Status vprint(
            Types::Output::Stream stream,
            const Types::Output::TextOptions &options,
            std::string_view format,
            std::format_args arguments) noexcept
        {
            try
            {
                if (!validOutputStream(stream))
                {
                    return invalidArgumentStatus("Unknown terminal output stream.");
                }
                if (!IO::isValidFlushMode(options.flushMode))
                {
                    return invalidArgumentStatus("Unknown IO flush mode.");
                }

                OutputState &state = outputState(stream);
                FormatScratchLease scratch(state);
                std::vformat_to(std::back_inserter(scratch.text()), format, arguments);

                std::lock_guard lock(state.mutex);
                return writeTextUnlocked(stream, state, scratch.text(), options);
            }
            catch (...)
            {
                return exceptionStatus();
            }
        }

        IO::Types::Status vprintln(
            Types::Output::Stream stream,
            const Types::Output::LineOptions &options,
            std::string_view format,
            std::format_args arguments) noexcept
        {
            try
            {
                if (!validOutputStream(stream))
                {
                    return invalidArgumentStatus("Unknown terminal output stream.");
                }
                if (!validLineEnding(options.lineEnding))
                {
                    return invalidArgumentStatus("Unknown terminal line ending.");
                }
                if (!IO::isValidFlushMode(options.flushMode))
                {
                    return invalidArgumentStatus("Unknown IO flush mode.");
                }

                OutputState &state = outputState(stream);
                FormatScratchLease scratch(state);
                std::vformat_to(std::back_inserter(scratch.text()), format, arguments);

                std::lock_guard lock(state.mutex);
                return writeLineUnlocked(stream, state, scratch.text(), options);
            }
            catch (...)
            {
                return exceptionStatus();
            }
        }
    } // namespace Detail

    IO::Types::Status flush(IO::Types::FlushMode mode) noexcept
    {
        return flush(Types::Output::Stream::Stdout, mode);
    }

    IO::Types::Status flush(Types::Output::Stream stream, IO::Types::FlushMode mode) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }
            if (!IO::isValidFlushMode(mode))
            {
                return invalidArgumentStatus("Unknown IO flush mode.");
            }

            std::lock_guard lock(outputState(stream).mutex);
            return Detail::Platform::flush(stream, mode);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status resetStyle(const Types::Output::ControlOptions &options) noexcept
    {
        return resetStyle(Types::Output::Stream::Stdout, options);
    }

    IO::Types::Status resetStyle(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }

            std::lock_guard lock(outputState(stream).mutex);
            return writeControlSequenceUnlocked(
                stream,
                "\x1b[0m",
                ControlFeature::StyleReset,
                "Terminal style reset is unsupported for this output stream.",
                options.flushMode);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status moveCursor(Types::Cursor::MoveDirection direction, std::uint32_t amount, const Types::Output::ControlOptions &options) noexcept
    {
        return moveCursor(Types::Output::Stream::Stdout, direction, amount, options);
    }

    IO::Types::Status moveCursor(
        Types::Output::Stream stream,
        Types::Cursor::MoveDirection direction,
        std::uint32_t amount,
        const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }

            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);

            char command = 'A';
            switch (direction)
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
                return invalidArgumentStatus("Unknown terminal cursor movement direction.");
            }

            if (amount == 0)
            {
                return flushIfRequested(stream, options.flushMode);
            }
            IO::Types::Status validationStatus = Detail::Platform::validateCursorMovement(stream, amount);
            if (!validationStatus.ok())
            {
                return validationStatus;
            }

            state.assembly.clear();
            state.assembly.append("\x1b[");
            appendUnsigned(state.assembly, amount);
            state.assembly.push_back(command);
            return writeAssembledControlSequenceUnlocked(
                stream,
                state,
                ControlFeature::CursorMovement,
                "Terminal cursor movement is unsupported for this output stream.",
                options.flushMode);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status setCursorPosition(Types::Cursor::Position position, const Types::Output::ControlOptions &options) noexcept
    {
        return setCursorPosition(Types::Output::Stream::Stdout, position, options);
    }

    IO::Types::Status setCursorPosition(
        Types::Output::Stream stream,
        Types::Cursor::Position position,
        const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }
            IO::Types::Status validationStatus = Detail::Platform::validateCursorPosition(stream, position);
            if (!validationStatus.ok())
            {
                return validationStatus;
            }

            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);
            state.assembly.clear();
            state.assembly.append("\x1b[");
            appendUnsigned(state.assembly, static_cast<std::uint64_t>(position.row) + 1);
            state.assembly.push_back(';');
            appendUnsigned(state.assembly, static_cast<std::uint64_t>(position.column) + 1);
            state.assembly.push_back('H');

            return writeAssembledControlSequenceUnlocked(
                stream,
                state,
                ControlFeature::CursorMovement,
                "Terminal cursor positioning is unsupported for this output stream.",
                options.flushMode);
        }
        catch (...)
        {
            return exceptionStatus();
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
            if (!validOutputStream(outputStream) || !validInputStream(responseStream))
            {
                return {.status = invalidArgumentStatus("Unknown terminal stream selected for cursor position query."), .position = {}};
            }

            std::scoped_lock lock(outputState(outputStream).mutex, Detail::inputIoMutex(responseStream));

            IO::Types::Status status = flushIfRequested(outputStream, options.flushMode);
            if (!status.ok())
            {
                return {.status = status, .position = {}};
            }

            return Detail::Platform::getCursorPosition(outputStream, responseStream, options);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .position = {}};
        }
    }

    Types::Cursor::PositionResult Detail::getLineRenderingCursorPosition(
        Types::Output::Stream outputStream,
        Types::Input::Stream inputStream) noexcept
    {
        try
        {
            if (!validOutputStream(outputStream) || !validInputStream(inputStream))
            {
                return {.status = invalidArgumentStatus("Unknown terminal stream selected for line rendering."), .position = {}};
            }

            std::scoped_lock lock(outputState(outputStream).mutex, Detail::inputIoMutex(inputStream));
            return Detail::Platform::getLineRenderingCursorPosition(outputStream);
        }
        catch (...)
        {
            return {.status = exceptionStatus(), .position = {}};
        }
    }

    IO::Types::Status Detail::setLineRenderingCursorPosition(Types::Output::Stream outputStream, Types::Cursor::Position position) noexcept
    {
        try
        {
            if (!validOutputStream(outputStream))
            {
                return invalidArgumentStatus("Unknown terminal output stream selected for line rendering.");
            }

            std::lock_guard lock(outputState(outputStream).mutex);
            return Detail::Platform::setLineRenderingCursorPosition(outputStream, position);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status saveCursorPosition(const Types::Output::ControlOptions &options) noexcept
    {
        return saveCursorPosition(Types::Output::Stream::Stdout, options);
    }

    IO::Types::Status saveCursorPosition(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }

            std::lock_guard lock(outputState(stream).mutex);
            return writeControlSequenceUnlocked(
                stream,
                "\x1b[s",
                ControlFeature::CursorSaveRestore,
                "Terminal cursor save is unsupported for this output stream.",
                options.flushMode);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status restoreCursorPosition(const Types::Output::ControlOptions &options) noexcept
    {
        return restoreCursorPosition(Types::Output::Stream::Stdout, options);
    }

    IO::Types::Status restoreCursorPosition(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }

            std::lock_guard lock(outputState(stream).mutex);
            return writeControlSequenceUnlocked(
                stream,
                "\x1b[u",
                ControlFeature::CursorSaveRestore,
                "Terminal cursor restore is unsupported for this output stream.",
                options.flushMode);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status setCursorVisible(bool visible, const Types::Output::ControlOptions &options) noexcept
    {
        return setCursorVisible(Types::Output::Stream::Stdout, visible, options);
    }

    IO::Types::Status setCursorVisible(Types::Output::Stream stream, bool visible, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }

            std::lock_guard lock(outputState(stream).mutex);
            return writeControlSequenceUnlocked(
                stream,
                visible ? "\x1b[?25h" : "\x1b[?25l",
                ControlFeature::CursorVisibility,
                "Terminal cursor visibility is unsupported for this output stream.",
                options.flushMode);
        }
        catch (...)
        {
            return exceptionStatus();
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
        if (!validOutputStream(stream))
        {
            scope.status_ = IO::makeStatus(ErrorCode::InvalidArgument);
            return scope;
        }
        if (!IO::isValidFlushMode(options.flushMode))
        {
            scope.status_ = IO::makeStatus(ErrorCode::InvalidArgument);
            return scope;
        }

        try
        {
            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);
            if (state.cursorHiddenScopeDepth == std::numeric_limits<std::size_t>::max())
            {
                scope.status_ = IO::makeStatus(ErrorCode::SizeLimitExceeded);
                return scope;
            }

            if (state.cursorHiddenScopeDepth == 0)
            {
                bool emitted = false;
                scope.status_ = writeControlSequenceUnlocked(
                    stream,
                    "\x1b[?25l",
                    ControlFeature::CursorVisibility,
                    "Terminal cursor visibility is unsupported for this output stream.",
                    options.flushMode,
                    &emitted);
                if (!emitted)
                {
                    return scope;
                }
            }

            ++state.cursorHiddenScopeDepth;
            scope.active_ = true;
        }
        catch (...)
        {
            scope.status_ = exceptionStatus();
        }
        return scope;
    }

    IO::Types::Status clear(Types::Output::ClearTarget target, const Types::Output::ControlOptions &options) noexcept
    {
        return clear(Types::Output::Stream::Stdout, target, options);
    }

    IO::Types::Status clear(Types::Output::Stream stream, Types::Output::ClearTarget target, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }

            std::lock_guard lock(outputState(stream).mutex);

            std::string_view sequence;
            switch (target)
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
                return invalidArgumentStatus("Unknown terminal clear target.");
            }

            return writeControlSequenceUnlocked(
                stream,
                sequence,
                ControlFeature::Clear,
                "Terminal clear controls are unsupported for this output stream.",
                options.flushMode);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status scroll(Types::Output::ScrollDirection direction, std::uint32_t lines, const Types::Output::ControlOptions &options) noexcept
    {
        return scroll(Types::Output::Stream::Stdout, direction, lines, options);
    }

    IO::Types::Status scroll(
        Types::Output::Stream stream,
        Types::Output::ScrollDirection direction,
        std::uint32_t lines,
        const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }

            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);

            char command = 'S';
            switch (direction)
            {
            case Types::Output::ScrollDirection::Up:
                command = 'S';
                break;
            case Types::Output::ScrollDirection::Down:
                command = 'T';
                break;
            default:
                return invalidArgumentStatus("Unknown terminal scroll direction.");
            }

            if (lines == 0)
            {
                return flushIfRequested(stream, options.flushMode);
            }
            IO::Types::Status validationStatus = Detail::Platform::validateScroll(stream, lines);
            if (!validationStatus.ok())
            {
                return validationStatus;
            }

            state.assembly.clear();
            state.assembly.append("\x1b[");
            appendUnsigned(state.assembly, lines);
            state.assembly.push_back(command);
            return writeAssembledControlSequenceUnlocked(
                stream,
                state,
                ControlFeature::Scroll,
                "Terminal scrolling is unsupported for this output stream.",
                options.flushMode);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status enterAlternateScreen(const Types::Output::ControlOptions &options) noexcept
    {
        return enterAlternateScreen(Types::Output::Stream::Stdout, options);
    }

    IO::Types::Status enterAlternateScreen(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }

            std::lock_guard lock(outputState(stream).mutex);
            return writeControlSequenceUnlocked(
                stream,
                "\x1b[?1049h",
                ControlFeature::AlternateScreen,
                "Terminal alternate screen is unsupported for this output stream.",
                options.flushMode);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status leaveAlternateScreen(const Types::Output::ControlOptions &options) noexcept
    {
        return leaveAlternateScreen(Types::Output::Stream::Stdout, options);
    }

    IO::Types::Status leaveAlternateScreen(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }

            std::lock_guard lock(outputState(stream).mutex);
            return writeControlSequenceUnlocked(
                stream,
                "\x1b[?1049l",
                ControlFeature::AlternateScreen,
                "Terminal alternate screen is unsupported for this output stream.",
                options.flushMode);
        }
        catch (...)
        {
            return exceptionStatus();
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
        if (!validOutputStream(stream))
        {
            scope.status_ = IO::makeStatus(ErrorCode::InvalidArgument);
            return scope;
        }
        if (!IO::isValidFlushMode(options.flushMode))
        {
            scope.status_ = IO::makeStatus(ErrorCode::InvalidArgument);
            return scope;
        }

        try
        {
            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);
            if (state.alternateScreenScopeDepth == std::numeric_limits<std::size_t>::max())
            {
                scope.status_ = IO::makeStatus(ErrorCode::SizeLimitExceeded);
                return scope;
            }

            if (state.alternateScreenScopeDepth == 0)
            {
                bool emitted = false;
                scope.status_ = writeControlSequenceUnlocked(
                    stream,
                    "\x1b[?1049h",
                    ControlFeature::AlternateScreen,
                    "Terminal alternate screen is unsupported for this output stream.",
                    options.flushMode,
                    &emitted);
                if (!emitted)
                {
                    return scope;
                }
            }

            ++state.alternateScreenScopeDepth;
            scope.active_ = true;
        }
        catch (...)
        {
            scope.status_ = exceptionStatus();
        }
        return scope;
    }

    IO::Types::Status setTitle(std::string_view utf8Title, const Types::Output::ControlOptions &options) noexcept
    {
        return setTitle(Types::Output::Stream::Stdout, utf8Title, options);
    }

    IO::Types::Status setTitle(Types::Output::Stream stream, std::string_view utf8Title, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }
            IO::Types::Status validationStatus = validateUtf8Text(utf8Title);
            if (!validationStatus.ok())
            {
                return validationStatus;
            }
            validationStatus = Detail::Platform::validateTitle(stream, utf8Title);
            if (!validationStatus.ok())
            {
                return validationStatus;
            }

            OutputState &state = outputState(stream);
            std::lock_guard lock(state.mutex);
            state.assembly.clear();
            state.assembly.append("\x1b]0;");
            appendSanitizedTitle(state.assembly, utf8Title);
            state.assembly.push_back('\x07');
            return writeAssembledControlSequenceUnlocked(
                stream,
                state,
                ControlFeature::Title,
                "Terminal title controls are unsupported for this output stream.",
                options.flushMode);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }

    IO::Types::Status ringBell(const Types::Output::ControlOptions &options) noexcept
    {
        return ringBell(Types::Output::Stream::Stdout, options);
    }

    IO::Types::Status ringBell(Types::Output::Stream stream, const Types::Output::ControlOptions &options) noexcept
    {
        try
        {
            if (!validOutputStream(stream))
            {
                return invalidArgumentStatus("Unknown terminal output stream.");
            }

            std::lock_guard lock(outputState(stream).mutex);
            return writeControlSequenceUnlocked(
                stream,
                "\a",
                ControlFeature::Bell,
                "Terminal bell output is unsupported for this output stream.",
                options.flushMode);
        }
        catch (...)
        {
            return exceptionStatus();
        }
    }
} // namespace GameWIP::Terminal
