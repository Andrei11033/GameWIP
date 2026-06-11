/// @file terminal.cpp
/// @brief Core implementation for the Terminal library.

#include "terminal/terminal.h"
#include "terminal/internal/terminal_platform.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <format>
#include <iterator>
#include <limits>
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

        struct StylePlan
        {
            IO::Types::Status status = IO::successStatus();
            bool emitStyle = false;
        };

        struct OutputState
        {
            std::mutex mutex;
            std::string assembly;
            std::size_t cursorHiddenScopeDepth = 0;
            std::size_t alternateScreenScopeDepth = 0;
        };

        struct StyleSequence
        {
            std::array<char, 96> bytes{};
            std::size_t size = 0;
        };

        inline constexpr std::size_t kRetainedAssemblyLimit = std::size_t{64} * 1024;

        [[nodiscard]] bool validInputStream(Types::InputStream stream) noexcept
        {
            return stream == Types::InputStream::Stdin;
        }

        [[nodiscard]] bool validOutputStream(Types::OutputStream stream) noexcept
        {
            return stream == Types::OutputStream::Stdout || stream == Types::OutputStream::Stderr;
        }

        [[nodiscard]] bool validLineEnding(Types::LineEnding lineEnding) noexcept
        {
            switch (lineEnding)
            {
            case Types::LineEnding::Native:
            case Types::LineEnding::Lf:
            case Types::LineEnding::CrLf:
                return true;
            }

            return false;
        }

        [[nodiscard]] bool validReadLineEndingMode(Types::ReadLineEndingMode mode) noexcept
        {
            switch (mode)
            {
            case Types::ReadLineEndingMode::Strip:
            case Types::ReadLineEndingMode::Keep:
            case Types::ReadLineEndingMode::NormalizeToLf:
                return true;
            }

            return false;
        }

        [[nodiscard]] IO::Types::Status unsupportedStatus(std::string message = {})
        {
            return IO::makeStatus(ErrorCode::Unsupported, 0, std::move(message));
        }

        [[nodiscard]] IO::Types::Status invalidArgumentStatus(std::string message = {})
        {
            return IO::makeStatus(ErrorCode::InvalidArgument, 0, std::move(message));
        }

        [[nodiscard]] bool isDefaultColor(const Types::Color &color) noexcept
        {
            return color.kind() == Types::ColorKind::Default;
        }

        [[nodiscard]] bool isDefaultStyle(const Types::TextStyle &style) noexcept
        {
            return isDefaultColor(style.foreground) && isDefaultColor(style.background) && !style.bold && !style.dim && !style.italic &&
                   !style.underline && !style.inverse && !style.strikethrough;
        }

        [[nodiscard]] bool colorSupported(const Types::Color &color, const Types::StyleCapabilities &capabilities) noexcept
        {
            switch (color.kind())
            {
            case Types::ColorKind::Default:
                return true;
            case Types::ColorKind::Basic:
                return capabilities.basicColor;
            case Types::ColorKind::Rgb:
                return capabilities.rgbColor;
            }

            return false;
        }

        [[nodiscard]] bool styleSupported(const Types::TextStyle &style, const Types::StyleCapabilities &capabilities) noexcept
        {
            return colorSupported(style.foreground, capabilities) && colorSupported(style.background, capabilities) &&
                   (!style.bold || capabilities.bold) && (!style.dim || capabilities.dim) && (!style.italic || capabilities.italic) &&
                   (!style.underline || capabilities.underline) && (!style.inverse || capabilities.inverse) &&
                   (!style.strikethrough || capabilities.strikethrough);
        }

        [[nodiscard]] bool anyStyleSupported(const Types::StyleCapabilities &capabilities) noexcept
        {
            return capabilities.basicColor || capabilities.rgbColor || capabilities.bold || capabilities.dim || capabilities.italic ||
                   capabilities.underline || capabilities.inverse || capabilities.strikethrough;
        }

        [[nodiscard]] OutputState &outputState(Types::OutputStream stream) noexcept
        {
            static OutputState stdoutState;
            static OutputState stderrState;

            return stream == Types::OutputStream::Stderr ? stderrState : stdoutState;
        }

        [[nodiscard]] std::mutex &inputMutex([[maybe_unused]] Types::InputStream stream) noexcept
        {
            static std::mutex stdinMutex;
            return stdinMutex;
        }

        [[nodiscard]] std::string_view lineEndingText(Types::LineEnding lineEnding) noexcept
        {
            switch (lineEnding)
            {
            case Types::LineEnding::Native:
                return Detail::Platform::nativeLineEnding();
            case Types::LineEnding::Lf:
                return "\n";
            case Types::LineEnding::CrLf:
                return "\r\n";
            }

            return {};
        }

        [[nodiscard]] int basicColorCode(Types::BasicColor color, bool foreground) noexcept
        {
            const int value = static_cast<int>(color);
            const bool bright = value >= 8;
            const int colorIndex = bright ? value - 8 : value;
            return (foreground ? (bright ? 90 : 30) : (bright ? 100 : 40)) + colorIndex;
        }

        [[nodiscard]] bool isKnownBasicColor(Types::BasicColor color) noexcept
        {
            switch (color)
            {
            case Types::BasicColor::Black:
            case Types::BasicColor::Red:
            case Types::BasicColor::Green:
            case Types::BasicColor::Yellow:
            case Types::BasicColor::Blue:
            case Types::BasicColor::Magenta:
            case Types::BasicColor::Cyan:
            case Types::BasicColor::White:
            case Types::BasicColor::BrightBlack:
            case Types::BasicColor::BrightRed:
            case Types::BasicColor::BrightGreen:
            case Types::BasicColor::BrightYellow:
            case Types::BasicColor::BrightBlue:
            case Types::BasicColor::BrightMagenta:
            case Types::BasicColor::BrightCyan:
            case Types::BasicColor::BrightWhite:
                return true;
            }

            return false;
        }

        void releaseLargeAssembly(OutputState &state) noexcept
        {
            state.assembly.clear();
            if (state.assembly.capacity() > kRetainedAssemblyLimit)
            {
                std::string{}.swap(state.assembly);
            }
        }

        void appendUnsigned(std::string &text, std::uint64_t value)
        {
            std::array<char, 32> buffer{};
            const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
            text.append(buffer.data(), result.ptr);
        }

        void appendSequenceText(StyleSequence &sequence, std::string_view text) noexcept
        {
            for (const char character : text)
            {
                sequence.bytes[sequence.size++] = character;
            }
        }

        void appendSequenceNumber(StyleSequence &sequence, std::uint32_t value) noexcept
        {
            const auto result = std::to_chars(sequence.bytes.data() + sequence.size, sequence.bytes.data() + sequence.bytes.size(), value);
            sequence.size = static_cast<std::size_t>(result.ptr - sequence.bytes.data());
        }

        void appendSgrParameter(StyleSequence &sequence, std::uint32_t parameter, bool &hasParameter) noexcept
        {
            if (hasParameter)
            {
                sequence.bytes[sequence.size++] = ';';
            }
            appendSequenceNumber(sequence, parameter);
            hasParameter = true;
        }

        void appendColorSgr(StyleSequence &sequence, const Types::Color &color, bool foreground, bool &hasParameter) noexcept
        {
            switch (color.kind())
            {
            case Types::ColorKind::Default:
                return;
            case Types::ColorKind::Basic:
                appendSgrParameter(sequence, static_cast<std::uint32_t>(basicColorCode(color.basic(), foreground)), hasParameter);
                return;
            case Types::ColorKind::Rgb:
                appendSgrParameter(sequence, foreground ? 38U : 48U, hasParameter);
                appendSgrParameter(sequence, 2, hasParameter);
                appendSgrParameter(sequence, color.red(), hasParameter);
                appendSgrParameter(sequence, color.green(), hasParameter);
                appendSgrParameter(sequence, color.blue(), hasParameter);
                return;
            }
        }

        [[nodiscard]] StyleSequence makeStyleSequence(const Types::TextStyle &style) noexcept
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

        [[nodiscard]] StylePlan stylePlanForCapabilities(
            Types::StyleMode mode,
            const Types::TextStyle &style,
            const Types::StyleCapabilities &capabilities)
        {
            switch (mode)
            {
            case Types::StyleMode::Never:
                return {};
            case Types::StyleMode::Auto:
            case Types::StyleMode::Required:
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

            if (mode == Types::StyleMode::Required)
            {
                return {.status = unsupportedStatus("Terminal output stream does not support the requested style.")};
            }

            return {};
        }

        [[nodiscard]] StylePlan stylePlan(Types::OutputStream stream, Types::StyleMode mode, const Types::TextStyle &style)
        {
            switch (mode)
            {
            case Types::StyleMode::Never:
                return {};
            case Types::StyleMode::Auto:
            case Types::StyleMode::Required:
                break;
            default:
                return {.status = invalidArgumentStatus("Unknown terminal style mode.")};
            }

            if (isDefaultStyle(style))
            {
                return {};
            }
            if (mode == Types::StyleMode::Never)
            {
                return {};
            }

            Types::OutputCapabilitiesResult capabilities = Detail::Platform::getOutputCapabilities(stream);
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

            if (mode == Types::StyleMode::Auto)
            {
                return {};
            }
            if (!capabilities.status.ok())
            {
                return {.status = capabilities.status};
            }
            return {.status = unsupportedStatus("Terminal output stream does not support the requested style.")};
        }

        [[nodiscard]] IO::Types::Status flushIfRequested(Types::OutputStream stream, IO::Types::FlushMode mode)
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

        [[nodiscard]] IO::Types::Status writeAssembly(Types::OutputStream stream, OutputState &state, IO::Types::FlushMode flushMode)
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

        [[nodiscard]] IO::Types::Status writeTextUnlocked(
            Types::OutputStream stream,
            OutputState &state,
            std::string_view utf8Text,
            const Types::TextWriteOptions &options)
        {
            if (!IO::isValidFlushMode(options.flushMode))
            {
                return invalidArgumentStatus("Unknown IO flush mode.");
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

        [[nodiscard]] IO::Types::Status writeLineUnlocked(
            Types::OutputStream stream,
            OutputState &state,
            std::string_view utf8Text,
            const Types::LineWriteOptions &options)
        {
            if (!validLineEnding(options.lineEnding))
            {
                return invalidArgumentStatus("Unknown terminal line ending.");
            }
            if (!IO::isValidFlushMode(options.flushMode))
            {
                return invalidArgumentStatus("Unknown IO flush mode.");
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

        [[nodiscard]] IO::Types::Status finishFormattedWriteUnlocked(
            Types::OutputStream stream,
            OutputState &state,
            Types::StyleMode styleMode,
            const Types::TextStyle &style,
            std::string_view lineEnding,
            IO::Types::FlushMode flushMode)
        {
            if (!IO::isValidFlushMode(flushMode))
            {
                releaseLargeAssembly(state);
                return invalidArgumentStatus("Unknown IO flush mode.");
            }

            const StylePlan plan = stylePlan(stream, styleMode, style);
            if (!plan.status.ok())
            {
                releaseLargeAssembly(state);
                return plan.status;
            }

            if (plan.emitStyle)
            {
                const StyleSequence prefix = makeStyleSequence(style);
                state.assembly.insert(0, prefix.bytes.data(), prefix.size);
                state.assembly.append("\x1b[0m");
            }
            state.assembly.append(lineEnding);
            return writeAssembly(stream, state, flushMode);
        }

        [[nodiscard]] IO::Types::WriteResult writeBytesUnlocked(
            Types::OutputStream stream,
            std::span<const std::byte> bytes,
            const Types::ByteWriteOptions &options)
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

        [[nodiscard]] IO::Types::Status validateSegments(
            std::span<const Types::WriteSegment> segments,
            Types::StyleMode styleMode,
            const Types::OutputCapabilities &capabilities)
        {
            for (const Types::WriteSegment &segment : segments)
            {
                switch (segment.kind())
                {
                case Types::WriteSegmentKind::Text:
                    break;
                case Types::WriteSegmentKind::Bytes:
                    if (!capabilities.supportsByteOutput)
                    {
                        return unsupportedStatus("Terminal byte output is unsupported for this output stream.");
                    }
                    break;
                case Types::WriteSegmentKind::StyledText:
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

        struct SegmentRequirements
        {
            bool hasBytes = false;
            bool needsStyleCapabilities = false;
        };

        [[nodiscard]] SegmentRequirements segmentRequirements(std::span<const Types::WriteSegment> segments, Types::StyleMode styleMode) noexcept
        {
            SegmentRequirements requirements;
            for (const Types::WriteSegment &segment : segments)
            {
                if (segment.kind() == Types::WriteSegmentKind::Bytes)
                {
                    requirements.hasBytes = true;
                }
                else if (
                    styleMode != Types::StyleMode::Never && segment.kind() == Types::WriteSegmentKind::StyledText && !isDefaultStyle(segment.style()))
                {
                    requirements.needsStyleCapabilities = true;
                }
            }
            return requirements;
        }

        [[nodiscard]] IO::Types::Status writeSegmentsUnlocked(
            Types::OutputStream stream,
            OutputState &state,
            std::span<const Types::WriteSegment> segments,
            const Types::SegmentWriteOptions &options)
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
            case Types::StyleMode::Never:
            case Types::StyleMode::Auto:
            case Types::StyleMode::Required:
                break;
            default:
                return invalidArgumentStatus("Unknown terminal style mode.");
            }

            const SegmentRequirements requirements = segmentRequirements(segments, options.styleMode);
            Types::OutputCapabilitiesResult capabilityResult;

            if (requirements.hasBytes || requirements.needsStyleCapabilities)
            {
                capabilityResult = Detail::Platform::getOutputCapabilities(stream);
                if (!capabilityResult.status.ok())
                {
                    if (requirements.hasBytes)
                    {
                        return capabilityResult.status;
                    }

                    Types::OutputCapabilitiesResult prepared = Detail::Platform::prepareOutput(stream);
                    if (prepared.status.ok())
                    {
                        capabilityResult = std::move(prepared);
                    }
                    else if (options.styleMode == Types::StyleMode::Required)
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
                        [&capabilityResult](const Types::WriteSegment &segment)
                        {
                            return segment.kind() != Types::WriteSegmentKind::StyledText || isDefaultStyle(segment.style()) ||
                                   styleSupported(segment.style(), capabilityResult.capabilities.style);
                        });
                    if (!allStylesSupported)
                    {
                        if (capabilityResult.capabilities.kind != Types::StreamKind::Redirected)
                        {
                            Types::OutputCapabilitiesResult prepared = Detail::Platform::prepareOutput(stream);
                            if (prepared.status.ok())
                            {
                                capabilityResult = std::move(prepared);
                            }
                            else if (options.styleMode == Types::StyleMode::Required)
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

            if (segments.size() == 1 && segments.front().kind() == Types::WriteSegmentKind::Text && !segments.front().text().empty() &&
                !options.appendLineEnding)
            {
                status = Detail::Platform::writeText(stream, segments.front().text());
                return status.ok() ? flushIfRequested(stream, options.flushMode) : status;
            }

            state.assembly.clear();
            for (const Types::WriteSegment &segment : segments)
            {
                switch (segment.kind())
                {
                case Types::WriteSegmentKind::Text:
                    state.assembly.append(segment.text());
                    break;
                case Types::WriteSegmentKind::StyledText:
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
                case Types::WriteSegmentKind::Bytes:
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

            return writeAssembly(stream, state, options.flushMode);
        }

        [[nodiscard]] bool controlFeatureSupported(const Types::OutputCapabilities &capabilities, ControlFeature feature) noexcept
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

        [[nodiscard]] IO::Types::Status writeControlSequenceUnlocked(
            Types::OutputStream stream,
            std::string_view sequence,
            ControlFeature feature,
            std::string_view unsupportedMessage,
            IO::Types::FlushMode flushMode)
        {
            if (!IO::isValidFlushMode(flushMode))
            {
                return invalidArgumentStatus("Unknown IO flush mode.");
            }

            Types::OutputCapabilitiesResult capabilities = Detail::Platform::getOutputCapabilities(stream);
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
                    return unsupportedStatus(std::string(unsupportedMessage));
                }
            }

            IO::Types::Status status = Detail::Platform::writeText(stream, sequence);
            if (!status.ok())
            {
                return status;
            }

            return flushIfRequested(stream, flushMode);
        }

        [[nodiscard]] IO::Types::Status writeAssembledControlSequenceUnlocked(
            Types::OutputStream stream,
            OutputState &state,
            ControlFeature feature,
            std::string_view unsupportedMessage,
            IO::Types::FlushMode flushMode)
        {
            IO::Types::Status status = writeControlSequenceUnlocked(stream, state.assembly, feature, unsupportedMessage, flushMode);
            releaseLargeAssembly(state);
            return status;
        }

        [[nodiscard]] IO::Types::Status restoreCursorHiddenScope(Types::OutputStream stream, const Types::ControlOptions &options) noexcept
        {
            try
            {
                OutputState &state = outputState(stream);
                std::lock_guard lock(state.mutex);

                if (state.cursorHiddenScopeDepth == 0)
                {
                    return IO::successStatus();
                }

                if (state.cursorHiddenScopeDepth > 1)
                {
                    --state.cursorHiddenScopeDepth;
                    return IO::successStatus();
                }

                IO::Types::Status status = writeControlSequenceUnlocked(
                    stream,
                    "\x1b[?25h",
                    ControlFeature::CursorVisibility,
                    "Terminal cursor visibility is unsupported for this output stream.",
                    options.flushMode);
                if (status.ok())
                {
                    state.cursorHiddenScopeDepth = 0;
                }
                return status;
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return IO::makeStatus(ErrorCode::NativeFailure);
            }
        }

        [[nodiscard]] IO::Types::Status restoreInputModeScope(
            Types::InputStream stream,
            const Types::InputMode &previousMode,
            std::uint64_t previousNativeMode,
            bool hasPreviousNativeMode) noexcept
        {
            try
            {
                std::lock_guard lock(inputMutex(stream));
                Detail::Platform::InputModeSnapshot snapshot;
                snapshot.mode = previousMode;
                snapshot.nativeMode = previousNativeMode;
                snapshot.hasNativeMode = hasPreviousNativeMode;
                return Detail::Platform::restoreInputMode(stream, snapshot);
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return IO::makeStatus(ErrorCode::NativeFailure);
            }
        }

        [[nodiscard]] IO::Types::Status leaveAlternateScreenScope(Types::OutputStream stream, const Types::ControlOptions &options) noexcept
        {
            try
            {
                OutputState &state = outputState(stream);
                std::lock_guard lock(state.mutex);

                if (state.alternateScreenScopeDepth == 0)
                {
                    return IO::successStatus();
                }

                if (state.alternateScreenScopeDepth > 1)
                {
                    --state.alternateScreenScopeDepth;
                    return IO::successStatus();
                }

                IO::Types::Status status = writeControlSequenceUnlocked(
                    stream,
                    "\x1b[?1049l",
                    ControlFeature::AlternateScreen,
                    "Terminal alternate screen is unsupported for this output stream.",
                    options.flushMode);
                if (status.ok())
                {
                    state.alternateScreenScopeDepth = 0;
                }
                return status;
            }
            catch (const std::bad_alloc &)
            {
                return IO::makeStatus(ErrorCode::OutOfMemory);
            }
            catch (...)
            {
                return IO::makeStatus(ErrorCode::NativeFailure);
            }
        }

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

    Types::Color defaultColor() noexcept
    {
        return {};
    }

    Types::Color::Color(Types::BasicColor color) noexcept
        : kind_(Types::ColorKind::Basic)
        , basic_(color)
    {
    }

    Types::Color::Color(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept
        : kind_(Types::ColorKind::Rgb)
        , red_(red)
        , green_(green)
        , blue_(blue)
    {
    }

    Types::Color basicColor(Types::BasicColor color) noexcept
    {
        if (!isKnownBasicColor(color))
        {
            return {};
        }

        return Types::Color(color);
    }

    Types::Color rgbColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept
    {
        return Types::Color(red, green, blue);
    }

    Types::InputMode makeInputMode(Types::InputModePreset preset) noexcept
    {
        switch (preset)
        {
        case Types::InputModePreset::InteractiveLine:
            return {.lineBuffered = true, .echoInput = true, .processControlKeys = true};
        case Types::InputModePreset::RawBytes:
            return {.lineBuffered = false, .echoInput = false, .processControlKeys = false};
        }

        return {.lineBuffered = true, .echoInput = true, .processControlKeys = true};
    }

    Types::WriteSegment::WriteSegment(
        Types::WriteSegmentKind kind,
        std::string_view text,
        std::span<const std::byte> bytes,
        const Types::TextStyle &style) noexcept
        : kind_(kind)
        , text_(text)
        , bytes_(bytes)
        , style_(style)
    {
    }

    Types::WriteSegment textSegment(std::string_view text) noexcept
    {
        return Types::WriteSegment(Types::WriteSegmentKind::Text, text, {}, {});
    }

    Types::WriteSegment styledTextSegment(std::string_view text, const Types::TextStyle &style) noexcept
    {
        return Types::WriteSegment(Types::WriteSegmentKind::StyledText, text, {}, style);
    }

    Types::WriteSegment byteSegment(std::span<const std::byte> bytes) noexcept
    {
        return Types::WriteSegment(Types::WriteSegmentKind::Bytes, {}, bytes, {});
    }

    InputModeScope::InputModeScope() noexcept = default;

    InputModeScope::InputModeScope(InputModeScope &&other) noexcept
        : stream_(other.stream_)
        , previousMode_(other.previousMode_)
        , previousNativeMode_(other.previousNativeMode_)
        , status_(std::move(other.status_))
        , hasPreviousNativeMode_(other.hasPreviousNativeMode_)
        , active_(other.active_)
    {
        other.active_ = false;
    }

    InputModeScope &InputModeScope::operator=(InputModeScope &&other) noexcept
    {
        if (this != &other)
        {
            if (active_)
            {
                static_cast<void>(restore());
                if (active_)
                {
                    return *this;
                }
            }
            stream_ = other.stream_;
            previousMode_ = other.previousMode_;
            previousNativeMode_ = other.previousNativeMode_;
            status_ = std::move(other.status_);
            hasPreviousNativeMode_ = other.hasPreviousNativeMode_;
            active_ = other.active_;
            other.active_ = false;
        }

        return *this;
    }

    InputModeScope::~InputModeScope() noexcept
    {
        static_cast<void>(restore());
    }

    bool InputModeScope::active() const noexcept
    {
        return active_;
    }

    const IO::Types::Status &InputModeScope::status() const noexcept
    {
        return status_;
    }

    IO::Types::Status InputModeScope::restore() noexcept
    {
        if (active_)
        {
            status_ = restoreInputModeScope(stream_, previousMode_, previousNativeMode_, hasPreviousNativeMode_);
            active_ = !status_.ok();
        }

        return status_;
    }

    void InputModeScope::release() noexcept
    {
        active_ = false;
    }

    AlternateScreenScope::AlternateScreenScope() noexcept = default;

    AlternateScreenScope::AlternateScreenScope(AlternateScreenScope &&other) noexcept
        : stream_(other.stream_)
        , options_(other.options_)
        , status_(std::move(other.status_))
        , active_(other.active_)
    {
        other.active_ = false;
    }

    AlternateScreenScope &AlternateScreenScope::operator=(AlternateScreenScope &&other) noexcept
    {
        if (this != &other)
        {
            if (active_)
            {
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
            other.active_ = false;
        }

        return *this;
    }

    AlternateScreenScope::~AlternateScreenScope() noexcept
    {
        static_cast<void>(leave());
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
            status_ = leaveAlternateScreenScope(stream_, options_);
            active_ = !status_.ok();
        }

        return status_;
    }

    CursorHiddenScope::CursorHiddenScope() noexcept = default;

    CursorHiddenScope::CursorHiddenScope(CursorHiddenScope &&other) noexcept
        : stream_(other.stream_)
        , options_(other.options_)
        , status_(std::move(other.status_))
        , active_(other.active_)
    {
        other.active_ = false;
    }

    CursorHiddenScope &CursorHiddenScope::operator=(CursorHiddenScope &&other) noexcept
    {
        if (this != &other)
        {
            if (active_)
            {
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
            other.active_ = false;
        }

        return *this;
    }

    CursorHiddenScope::~CursorHiddenScope() noexcept
    {
        static_cast<void>(restore());
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
            status_ = restoreCursorHiddenScope(stream_, options_);
            active_ = !status_.ok();
        }

        return status_;
    }

    OutputBuffer::OutputBuffer(Types::LineEnding lineEnding)
        : lineEnding_(lineEnding)
    {
        if (!validLineEnding(lineEnding))
        {
            throw std::invalid_argument("Unknown terminal line ending.");
        }
    }

    void OutputBuffer::reserve(std::size_t bytes)
    {
        text_.reserve(bytes);
    }

    void OutputBuffer::clear() noexcept
    {
        text_.clear();
    }

    bool OutputBuffer::empty() const noexcept
    {
        return text_.empty();
    }

    std::size_t OutputBuffer::size() const noexcept
    {
        return text_.size();
    }

    std::string_view OutputBuffer::text() const noexcept
    {
        return text_;
    }

    void OutputBuffer::appendText(std::string_view utf8Text)
    {
        text_.append(utf8Text);
    }

    void OutputBuffer::appendLine(std::string_view utf8Text)
    {
        text_.append(utf8Text);
        text_.append(lineEndingText(lineEnding_));
    }

    IO::Types::Status OutputBuffer::writeTo(const Types::TextWriteOptions &options) const
    {
        return writeTo(Types::OutputStream::Stdout, options);
    }

    IO::Types::Status OutputBuffer::writeTo(Types::OutputStream stream, const Types::TextWriteOptions &options) const
    {
        return Terminal::writeText(stream, text_, options);
    }

    IO::Types::Status OutputBuffer::flushTo(const Types::TextWriteOptions &options)
    {
        return flushTo(Types::OutputStream::Stdout, options);
    }

    IO::Types::Status OutputBuffer::flushTo(Types::OutputStream stream, const Types::TextWriteOptions &options)
    {
        IO::Types::Status status = writeTo(stream, options);
        if (status.ok())
        {
            clear();
        }

        return status;
    }

    Types::InputCapabilitiesResult getInputCapabilities()
    {
        return getInputCapabilities(Types::InputStream::Stdin);
    }

    Types::InputCapabilitiesResult getInputCapabilities(Types::InputStream stream)
    {
        if (!validInputStream(stream))
        {
            return {.status = invalidArgumentStatus("Unknown terminal input stream."), .capabilities = {}};
        }

        std::lock_guard lock(inputMutex(stream));
        return Detail::Platform::getInputCapabilities(stream);
    }

    Types::OutputCapabilitiesResult getOutputCapabilities()
    {
        return getOutputCapabilities(Types::OutputStream::Stdout);
    }

    Types::OutputCapabilitiesResult getOutputCapabilities(Types::OutputStream stream)
    {
        if (!validOutputStream(stream))
        {
            return {.status = invalidArgumentStatus("Unknown terminal output stream."), .capabilities = {}};
        }

        std::lock_guard lock(outputState(stream).mutex);
        return Detail::Platform::getOutputCapabilities(stream);
    }

    Types::OutputCapabilitiesResult prepareOutput()
    {
        return prepareOutput(Types::OutputStream::Stdout);
    }

    Types::OutputCapabilitiesResult prepareOutput(Types::OutputStream stream)
    {
        if (!validOutputStream(stream))
        {
            return {.status = invalidArgumentStatus("Unknown terminal output stream."), .capabilities = {}};
        }

        std::lock_guard lock(outputState(stream).mutex);
        return Detail::Platform::prepareOutput(stream);
    }

    Types::TerminalSizeResult getTerminalSize()
    {
        return getTerminalSize(Types::OutputStream::Stdout);
    }

    Types::TerminalSizeResult getTerminalSize(Types::OutputStream stream)
    {
        if (!validOutputStream(stream))
        {
            return {.status = invalidArgumentStatus("Unknown terminal output stream."), .size = {}};
        }

        std::lock_guard lock(outputState(stream).mutex);
        return Detail::Platform::getTerminalSize(stream);
    }

    Types::InputAvailabilityResult getInputAvailability()
    {
        return getInputAvailability(Types::InputStream::Stdin);
    }

    Types::InputAvailabilityResult getInputAvailability(Types::InputStream stream)
    {
        if (!validInputStream(stream))
        {
            return {.status = invalidArgumentStatus("Unknown terminal input stream."), .available = false, .estimatedBytes = 0};
        }

        std::lock_guard lock(inputMutex(stream));
        return Detail::Platform::getInputAvailability(stream);
    }

    Types::InputModeResult getInputMode()
    {
        return getInputMode(Types::InputStream::Stdin);
    }

    Types::InputModeResult getInputMode(Types::InputStream stream)
    {
        if (!validInputStream(stream))
        {
            return {.status = invalidArgumentStatus("Unknown terminal input stream."), .mode = {}};
        }

        std::lock_guard lock(inputMutex(stream));
        return Detail::Platform::getInputMode(stream);
    }

    IO::Types::Status setInputMode(const Types::InputMode &mode)
    {
        return setInputMode(Types::InputStream::Stdin, mode);
    }

    IO::Types::Status setInputMode(Types::InputStream stream, const Types::InputMode &mode)
    {
        if (!validInputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal input stream.");
        }

        std::lock_guard lock(inputMutex(stream));
        return Detail::Platform::setInputMode(stream, mode);
    }

    IO::Types::Status restoreDefaultInputMode()
    {
        return restoreDefaultInputMode(Types::InputStream::Stdin);
    }

    IO::Types::Status restoreDefaultInputMode(Types::InputStream stream)
    {
        if (!validInputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal input stream.");
        }

        std::lock_guard lock(inputMutex(stream));
        return Detail::Platform::restoreDefaultInputMode(stream);
    }

    InputModeScope scopedInputMode(const Types::InputMode &mode) noexcept
    {
        return scopedInputMode(Types::InputStream::Stdin, mode);
    }

    InputModeScope scopedInputMode(Types::InputStream stream, const Types::InputMode &mode) noexcept
    {
        InputModeScope scope;
        scope.stream_ = stream;
        if (!validInputStream(stream))
        {
            scope.status_ = IO::makeStatus(ErrorCode::InvalidArgument);
            return scope;
        }

        try
        {
            std::lock_guard lock(inputMutex(stream));
            const Detail::Platform::InputModeSnapshotResult previousMode = Detail::Platform::captureInputMode(stream);
            scope.status_ = previousMode.status;
            if (!scope.status_.ok())
            {
                return scope;
            }

            scope.previousMode_ = previousMode.snapshot.mode;
            scope.previousNativeMode_ = previousMode.snapshot.nativeMode;
            scope.hasPreviousNativeMode_ = previousMode.snapshot.hasNativeMode;
            scope.status_ = Detail::Platform::setInputMode(stream, mode);
            scope.active_ = scope.status_.ok();
        }
        catch (const std::bad_alloc &)
        {
            scope.status_ = IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            scope.status_ = IO::makeStatus(ErrorCode::NativeFailure);
        }
        return scope;
    }

    Types::LineReadResult readLine(const Types::LineReadOptions &options)
    {
        return readLine(Types::InputStream::Stdin, options);
    }

    Types::LineReadResult readLine(Types::InputStream stream, const Types::LineReadOptions &options)
    {
        if (!validInputStream(stream))
        {
            return {
                .status = invalidArgumentStatus("Unknown terminal input stream."),
                .outcome = Types::ReadOutcome::Completed,
                .line = {},
                .consumedLineEnding = Types::ConsumedLineEnding::None,
                .wasTruncated = false};
        }
        if (!validReadLineEndingMode(options.lineEndingMode))
        {
            return {
                .status = invalidArgumentStatus("Unknown terminal line-ending read mode."),
                .outcome = Types::ReadOutcome::Completed,
                .line = {},
                .consumedLineEnding = Types::ConsumedLineEnding::None,
                .wasTruncated = false};
        }
        if (options.maxReturnedBytes == 0)
        {
            return {
                .status = invalidArgumentStatus("Terminal line read maxReturnedBytes must be greater than zero."),
                .outcome = Types::ReadOutcome::Completed,
                .line = {},
                .consumedLineEnding = Types::ConsumedLineEnding::None,
                .wasTruncated = false};
        }

        std::lock_guard lock(inputMutex(stream));
        return Detail::Platform::readLine(stream, options);
    }

    Types::TextReadResult readText(const Types::TextReadOptions &options)
    {
        return readText(Types::InputStream::Stdin, options);
    }

    Types::TextReadResult readText(Types::InputStream stream, const Types::TextReadOptions &options)
    {
        if (!validInputStream(stream))
        {
            return {
                .status = invalidArgumentStatus("Unknown terminal input stream."),
                .outcome = Types::ReadOutcome::Completed,
                .text = {},
                .wasTruncated = false};
        }
        if (options.maxReturnedBytes == 0)
        {
            return {
                .status = invalidArgumentStatus("Terminal text read maxReturnedBytes must be greater than zero."),
                .outcome = Types::ReadOutcome::Completed,
                .text = {},
                .wasTruncated = false};
        }

        std::lock_guard lock(inputMutex(stream));
        return Detail::Platform::readText(stream, options);
    }

    Types::ByteReadResult readBytes(std::span<std::byte> outputBuffer, const Types::ByteReadOptions &options)
    {
        return readBytes(Types::InputStream::Stdin, outputBuffer, options);
    }

    Types::ByteReadResult readBytes(Types::InputStream stream, std::span<std::byte> outputBuffer, const Types::ByteReadOptions &options)
    {
        if (!validInputStream(stream))
        {
            return {.status = invalidArgumentStatus("Unknown terminal input stream."), .outcome = Types::ReadOutcome::Completed, .bytesRead = 0};
        }

        std::lock_guard lock(inputMutex(stream));
        return Detail::Platform::readBytes(stream, outputBuffer, options);
    }

    IO::Types::Status writeText(std::string_view utf8Text, const Types::TextWriteOptions &options)
    {
        return writeText(Types::OutputStream::Stdout, utf8Text, options);
    }

    IO::Types::Status writeText(Types::OutputStream stream, std::string_view utf8Text, const Types::TextWriteOptions &options)
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        OutputState &state = outputState(stream);
        std::lock_guard lock(state.mutex);
        return writeTextUnlocked(stream, state, utf8Text, options);
    }

    IO::Types::Status writeLine(std::string_view utf8Text, const Types::LineWriteOptions &options)
    {
        return writeLine(Types::OutputStream::Stdout, utf8Text, options);
    }

    IO::Types::Status writeLine(Types::OutputStream stream, std::string_view utf8Text, const Types::LineWriteOptions &options)
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        OutputState &state = outputState(stream);
        std::lock_guard lock(state.mutex);
        return writeLineUnlocked(stream, state, utf8Text, options);
    }

    IO::Types::WriteResult writeBytes(std::span<const std::byte> bytes, const Types::ByteWriteOptions &options)
    {
        return writeBytes(Types::OutputStream::Stdout, bytes, options);
    }

    IO::Types::WriteResult writeBytes(Types::OutputStream stream, std::span<const std::byte> bytes, const Types::ByteWriteOptions &options)
    {
        if (!validOutputStream(stream))
        {
            return {.status = invalidArgumentStatus("Unknown terminal output stream."), .bytesWritten = 0};
        }

        std::lock_guard lock(outputState(stream).mutex);
        return writeBytesUnlocked(stream, bytes, options);
    }

    IO::Types::Status writeSegments(std::span<const Types::WriteSegment> segments, const Types::SegmentWriteOptions &options)
    {
        return writeSegments(Types::OutputStream::Stdout, segments, options);
    }

    IO::Types::Status writeSegments(
        Types::OutputStream stream,
        std::span<const Types::WriteSegment> segments,
        const Types::SegmentWriteOptions &options)
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        OutputState &state = outputState(stream);
        std::lock_guard lock(state.mutex);
        return writeSegmentsUnlocked(stream, state, segments, options);
    }

    namespace Detail
    {
        IO::Types::Status vprint(
            Types::OutputStream stream,
            const Types::TextWriteOptions &options,
            std::string_view format,
            std::format_args arguments)
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
            std::lock_guard lock(state.mutex);
            state.assembly.clear();
            try
            {
                std::vformat_to(std::back_inserter(state.assembly), format, arguments);
            }
            catch (...)
            {
                releaseLargeAssembly(state);
                throw;
            }

            return finishFormattedWriteUnlocked(stream, state, options.styleMode, options.style, {}, options.flushMode);
        }

        IO::Types::Status vprintln(
            Types::OutputStream stream,
            const Types::LineWriteOptions &options,
            std::string_view format,
            std::format_args arguments)
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
            std::lock_guard lock(state.mutex);
            state.assembly.clear();
            try
            {
                std::vformat_to(std::back_inserter(state.assembly), format, arguments);
            }
            catch (...)
            {
                releaseLargeAssembly(state);
                throw;
            }

            return finishFormattedWriteUnlocked(
                stream,
                state,
                options.styleMode,
                options.style,
                lineEndingText(options.lineEnding),
                options.flushMode);
        }
    } // namespace Detail

    IO::Types::Status flush(IO::Types::FlushMode mode)
    {
        return flush(Types::OutputStream::Stdout, mode);
    }

    IO::Types::Status flush(Types::OutputStream stream, IO::Types::FlushMode mode)
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

    IO::Types::Status resetStyle(const Types::ControlOptions &options)
    {
        return resetStyle(Types::OutputStream::Stdout, options);
    }

    IO::Types::Status resetStyle(Types::OutputStream stream, const Types::ControlOptions &options)
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

    IO::Types::Status moveCursor(Types::CursorMoveDirection direction, std::uint32_t amount, const Types::ControlOptions &options)
    {
        return moveCursor(Types::OutputStream::Stdout, direction, amount, options);
    }

    IO::Types::Status moveCursor(
        Types::OutputStream stream,
        Types::CursorMoveDirection direction,
        std::uint32_t amount,
        const Types::ControlOptions &options)
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
        case Types::CursorMoveDirection::Up:
            command = 'A';
            break;
        case Types::CursorMoveDirection::Down:
            command = 'B';
            break;
        case Types::CursorMoveDirection::Left:
            command = 'D';
            break;
        case Types::CursorMoveDirection::Right:
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

    IO::Types::Status setCursorPosition(Types::CursorPosition position, const Types::ControlOptions &options)
    {
        return setCursorPosition(Types::OutputStream::Stdout, position, options);
    }

    IO::Types::Status setCursorPosition(Types::OutputStream stream, Types::CursorPosition position, const Types::ControlOptions &options)
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

    Types::CursorPositionResult getCursorPosition(const Types::CursorPositionQueryOptions &options)
    {
        return getCursorPosition(Types::OutputStream::Stdout, Types::InputStream::Stdin, options);
    }

    Types::CursorPositionResult getCursorPosition(
        Types::OutputStream outputStream,
        Types::InputStream responseStream,
        const Types::CursorPositionQueryOptions &options)
    {
        if (!validOutputStream(outputStream) || !validInputStream(responseStream))
        {
            return {.status = invalidArgumentStatus("Unknown terminal stream selected for cursor position query."), .position = {}};
        }

        std::scoped_lock lock(outputState(outputStream).mutex, inputMutex(responseStream));

        IO::Types::Status status = flushIfRequested(outputStream, options.flushMode);
        if (!status.ok())
        {
            return {.status = status, .position = {}};
        }

        return Detail::Platform::getCursorPosition(outputStream, responseStream, options);
    }

    IO::Types::Status saveCursorPosition(const Types::ControlOptions &options)
    {
        return saveCursorPosition(Types::OutputStream::Stdout, options);
    }

    IO::Types::Status saveCursorPosition(Types::OutputStream stream, const Types::ControlOptions &options)
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

    IO::Types::Status restoreCursorPosition(const Types::ControlOptions &options)
    {
        return restoreCursorPosition(Types::OutputStream::Stdout, options);
    }

    IO::Types::Status restoreCursorPosition(Types::OutputStream stream, const Types::ControlOptions &options)
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

    IO::Types::Status setCursorVisible(bool visible, const Types::ControlOptions &options)
    {
        return setCursorVisible(Types::OutputStream::Stdout, visible, options);
    }

    IO::Types::Status setCursorVisible(Types::OutputStream stream, bool visible, const Types::ControlOptions &options)
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

    CursorHiddenScope scopedCursorHidden(const Types::ControlOptions &options) noexcept
    {
        return scopedCursorHidden(Types::OutputStream::Stdout, options);
    }

    CursorHiddenScope scopedCursorHidden(Types::OutputStream stream, const Types::ControlOptions &options) noexcept
    {
        CursorHiddenScope scope;
        scope.stream_ = stream;
        scope.options_ = options;
        if (!validOutputStream(stream))
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
                scope.status_ = writeControlSequenceUnlocked(
                    stream,
                    "\x1b[?25l",
                    ControlFeature::CursorVisibility,
                    "Terminal cursor visibility is unsupported for this output stream.",
                    options.flushMode);
                if (!scope.status_.ok())
                {
                    return scope;
                }
            }

            ++state.cursorHiddenScopeDepth;
            scope.active_ = true;
        }
        catch (const std::bad_alloc &)
        {
            scope.status_ = IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            scope.status_ = IO::makeStatus(ErrorCode::NativeFailure);
        }
        return scope;
    }

    IO::Types::Status clear(Types::ClearTarget target, const Types::ControlOptions &options)
    {
        return clear(Types::OutputStream::Stdout, target, options);
    }

    IO::Types::Status clear(Types::OutputStream stream, Types::ClearTarget target, const Types::ControlOptions &options)
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }

        std::lock_guard lock(outputState(stream).mutex);

        std::string_view sequence;
        switch (target)
        {
        case Types::ClearTarget::EntireScreen:
            sequence = "\x1b[2J";
            break;
        case Types::ClearTarget::ScreenBeforeCursor:
            sequence = "\x1b[1J";
            break;
        case Types::ClearTarget::ScreenAfterCursor:
            sequence = "\x1b[0J";
            break;
        case Types::ClearTarget::EntireScreenAndScrollback:
            sequence = "\x1b[3J";
            break;
        case Types::ClearTarget::EntireLine:
            sequence = "\x1b[2K";
            break;
        case Types::ClearTarget::LineBeforeCursor:
            sequence = "\x1b[1K";
            break;
        case Types::ClearTarget::LineAfterCursor:
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

    IO::Types::Status scroll(Types::ScrollDirection direction, std::uint32_t lines, const Types::ControlOptions &options)
    {
        return scroll(Types::OutputStream::Stdout, direction, lines, options);
    }

    IO::Types::Status scroll(Types::OutputStream stream, Types::ScrollDirection direction, std::uint32_t lines, const Types::ControlOptions &options)
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
        case Types::ScrollDirection::Up:
            command = 'S';
            break;
        case Types::ScrollDirection::Down:
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

    IO::Types::Status enterAlternateScreen(const Types::ControlOptions &options)
    {
        return enterAlternateScreen(Types::OutputStream::Stdout, options);
    }

    IO::Types::Status enterAlternateScreen(Types::OutputStream stream, const Types::ControlOptions &options)
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

    IO::Types::Status leaveAlternateScreen(const Types::ControlOptions &options)
    {
        return leaveAlternateScreen(Types::OutputStream::Stdout, options);
    }

    IO::Types::Status leaveAlternateScreen(Types::OutputStream stream, const Types::ControlOptions &options)
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

    AlternateScreenScope scopedAlternateScreen(const Types::ControlOptions &options) noexcept
    {
        return scopedAlternateScreen(Types::OutputStream::Stdout, options);
    }

    AlternateScreenScope scopedAlternateScreen(Types::OutputStream stream, const Types::ControlOptions &options) noexcept
    {
        AlternateScreenScope scope;
        scope.stream_ = stream;
        scope.options_ = options;
        if (!validOutputStream(stream))
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
                scope.status_ = writeControlSequenceUnlocked(
                    stream,
                    "\x1b[?1049h",
                    ControlFeature::AlternateScreen,
                    "Terminal alternate screen is unsupported for this output stream.",
                    options.flushMode);
                if (!scope.status_.ok())
                {
                    return scope;
                }
            }

            ++state.alternateScreenScopeDepth;
            scope.active_ = true;
        }
        catch (const std::bad_alloc &)
        {
            scope.status_ = IO::makeStatus(ErrorCode::OutOfMemory);
        }
        catch (...)
        {
            scope.status_ = IO::makeStatus(ErrorCode::NativeFailure);
        }
        return scope;
    }

    IO::Types::Status setTitle(std::string_view utf8Title, const Types::ControlOptions &options)
    {
        return setTitle(Types::OutputStream::Stdout, utf8Title, options);
    }

    IO::Types::Status setTitle(Types::OutputStream stream, std::string_view utf8Title, const Types::ControlOptions &options)
    {
        if (!validOutputStream(stream))
        {
            return invalidArgumentStatus("Unknown terminal output stream.");
        }
        IO::Types::Status validationStatus = Detail::Platform::validateTitle(stream, utf8Title);
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

    IO::Types::Status ringBell(const Types::ControlOptions &options)
    {
        return ringBell(Types::OutputStream::Stdout, options);
    }

    IO::Types::Status ringBell(Types::OutputStream stream, const Types::ControlOptions &options)
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
} // namespace GameWIP::Terminal
