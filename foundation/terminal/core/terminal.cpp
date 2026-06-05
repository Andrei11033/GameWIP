/// @file terminal.cpp
/// @brief Core implementation for the GameWIP Terminal library.

#include "terminal/terminal.h"
#include "terminal/internal/terminal_platform.h"

#include <algorithm>
#include <cstdint>
#include <mutex>
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
            return color.kind == Types::ColorKind::Default;
        }

        [[nodiscard]] bool isDefaultStyle(const Types::TextStyle &style) noexcept
        {
            return isDefaultColor(style.foreground) && isDefaultColor(style.background) && !style.bold && !style.dim && !style.italic &&
                   !style.underline && !style.inverse && !style.strikethrough;
        }

        [[nodiscard]] bool colorSupported(const Types::Color &color, const Types::StyleCapabilities &capabilities) noexcept
        {
            switch (color.kind)
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

        [[nodiscard]] std::mutex &outputMutex(Types::OutputStream stream) noexcept
        {
            static std::mutex stdoutMutex;
            static std::mutex stderrMutex;

            return stream == Types::OutputStream::Stderr ? stderrMutex : stdoutMutex;
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

            return "\n";
        }

        [[nodiscard]] int basicColorCode(Types::BasicColor color, bool foreground) noexcept
        {
            const int value = static_cast<int>(color);
            const bool bright = value >= 8;
            const int colorIndex = bright ? value - 8 : value;
            return (foreground ? (bright ? 90 : 30) : (bright ? 100 : 40)) + colorIndex;
        }

        void appendSgrParameter(std::string &parameters, std::string_view parameter)
        {
            if (!parameters.empty())
            {
                parameters.push_back(';');
            }

            parameters.append(parameter);
        }

        void appendColorSgr(std::string &parameters, const Types::Color &color, bool foreground)
        {
            switch (color.kind)
            {
            case Types::ColorKind::Default:
                break;
            case Types::ColorKind::Basic:
                appendSgrParameter(parameters, std::to_string(basicColorCode(color.basic, foreground)));
                break;
            case Types::ColorKind::Rgb:
                appendSgrParameter(parameters, foreground ? "38" : "48");
                appendSgrParameter(parameters, "2");
                appendSgrParameter(parameters, std::to_string(color.red));
                appendSgrParameter(parameters, std::to_string(color.green));
                appendSgrParameter(parameters, std::to_string(color.blue));
                break;
            }
        }

        [[nodiscard]] std::string makeStyleSequence(const Types::TextStyle &style)
        {
            std::string parameters;

            if (style.bold)
            {
                appendSgrParameter(parameters, "1");
            }
            if (style.dim)
            {
                appendSgrParameter(parameters, "2");
            }
            if (style.italic)
            {
                appendSgrParameter(parameters, "3");
            }
            if (style.underline)
            {
                appendSgrParameter(parameters, "4");
            }
            if (style.inverse)
            {
                appendSgrParameter(parameters, "7");
            }
            if (style.strikethrough)
            {
                appendSgrParameter(parameters, "9");
            }

            appendColorSgr(parameters, style.foreground, true);
            appendColorSgr(parameters, style.background, false);

            if (parameters.empty())
            {
                return {};
            }

            return "\x1b[" + parameters + "m";
        }

        [[nodiscard]] std::string makeCountedCsi(std::uint32_t amount, char command)
        {
            return "\x1b[" + std::to_string(amount) + command;
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
            case Types::StyleMode::Always:
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

            if (mode == Types::StyleMode::Always)
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
            case Types::StyleMode::Always:
                break;
            default:
                return {.status = invalidArgumentStatus("Unknown terminal style mode.")};
            }

            if (isDefaultStyle(style))
            {
                return {};
            }

            const Types::OutputCapabilityResult capabilities = Detail::Platform::getOutputCapabilities(stream);
            if (!capabilities.status.ok())
            {
                return {.status = capabilities.status};
            }

            return stylePlanForCapabilities(mode, style, capabilities.capabilities.style);
        }

        [[nodiscard]] IO::Types::Status appendLineEndingIfRequested(Types::OutputStream stream, bool appendLineEnding, Types::LineEnding lineEnding)
        {
            if (!appendLineEnding)
            {
                return IO::successStatus();
            }

            return Detail::Platform::writeText(stream, lineEndingText(lineEnding));
        }

        [[nodiscard]] IO::Types::Status writeLineEnding(Types::OutputStream stream, Types::LineEnding lineEnding)
        {
            return Detail::Platform::writeText(stream, lineEndingText(lineEnding));
        }

        [[nodiscard]] IO::Types::Status flushIfRequested(Types::OutputStream stream, IO::Types::FlushMode mode)
        {
            if (mode == IO::Types::FlushMode::None)
            {
                return IO::successStatus();
            }

            return Detail::Platform::flush(stream, mode);
        }

        [[nodiscard]] IO::Types::Status writeStyledTextOnlyUnlocked(
            Types::OutputStream stream,
            std::string_view utf8Text,
            Types::StyleMode styleMode,
            const Types::TextStyle &style,
            const Types::OutputCapabilities *knownCapabilities = nullptr)
        {
            const StylePlan plan = knownCapabilities != nullptr ? stylePlanForCapabilities(styleMode, style, knownCapabilities->style)
                                                                : stylePlan(stream, styleMode, style);
            if (!plan.status.ok())
            {
                return plan.status;
            }

            if (!plan.emitStyle)
            {
                return Detail::Platform::writeText(stream, utf8Text);
            }

            IO::Types::Status status = Detail::Platform::writeText(stream, makeStyleSequence(style));
            if (!status.ok())
            {
                return status;
            }

            status = Detail::Platform::writeText(stream, utf8Text);
            if (!status.ok())
            {
                static_cast<void>(Detail::Platform::writeText(stream, "\x1b[0m"));
                return status;
            }

            IO::Types::Status resetStatus = Detail::Platform::writeText(stream, "\x1b[0m");
            if (!resetStatus.ok() && resetStatus.message.empty())
            {
                resetStatus.message = "Terminal style reset failed after text output.";
            }

            return resetStatus;
        }

        [[nodiscard]] IO::Types::Status writeTextUnlocked(
            Types::OutputStream stream,
            std::string_view utf8Text,
            const Types::TextWriteOptions &options)
        {
            IO::Types::Status status = writeStyledTextOnlyUnlocked(stream, utf8Text, options.styleMode, options.style);
            if (!status.ok())
            {
                return status;
            }

            return flushIfRequested(stream, options.flushMode);
        }

        [[nodiscard]] IO::Types::Status writeLineUnlocked(
            Types::OutputStream stream,
            std::string_view utf8Text,
            const Types::LineWriteOptions &options)
        {
            switch (options.styleMode)
            {
            case Types::StyleMode::Never:
            case Types::StyleMode::Auto:
            case Types::StyleMode::Always:
                break;
            default:
                return invalidArgumentStatus("Unknown terminal style mode.");
            }

            const std::string_view ending = lineEndingText(options.lineEnding);
            if (options.styleMode == Types::StyleMode::Never || isDefaultStyle(options.style))
            {
                std::string line;
                line.reserve(utf8Text.size() + ending.size());
                line.append(utf8Text);
                line.append(ending);

                IO::Types::Status status = Detail::Platform::writeText(stream, line);
                if (!status.ok())
                {
                    return status;
                }

                return flushIfRequested(stream, options.flushMode);
            }

            IO::Types::Status status = writeStyledTextOnlyUnlocked(stream, utf8Text, options.styleMode, options.style);
            if (!status.ok())
            {
                return status;
            }

            status = writeLineEnding(stream, options.lineEnding);
            if (!status.ok())
            {
                return status;
            }

            return flushIfRequested(stream, options.flushMode);
        }

        [[nodiscard]] IO::Types::WriteResult writeBytesUnlocked(
            Types::OutputStream stream,
            std::span<const std::byte> bytes,
            const Types::ByteWriteOptions &options)
        {
            IO::Types::WriteResult result = Detail::Platform::writeBytes(stream, bytes);
            if (!result.status.ok())
            {
                return result;
            }

            result.status = flushIfRequested(stream, options.flushMode);
            return result;
        }

        [[nodiscard]] IO::Types::Status validateSegments(
            Types::OutputStream stream,
            std::span<const Types::WriteSegment> segments,
            Types::StyleMode styleMode,
            const Types::OutputCapabilities *knownCapabilities)
        {
            for (const Types::WriteSegment &segment : segments)
            {
                switch (segment.kind)
                {
                case Types::WriteSegmentKind::Text:
                case Types::WriteSegmentKind::Bytes:
                    break;
                case Types::WriteSegmentKind::StyledText:
                {
                    const StylePlan plan = knownCapabilities != nullptr ? stylePlanForCapabilities(styleMode, segment.style, knownCapabilities->style)
                                                                        : stylePlan(stream, styleMode, segment.style);
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

        [[nodiscard]] bool segmentsNeedStyleCapabilities(std::span<const Types::WriteSegment> segments, Types::StyleMode styleMode) noexcept
        {
            if (styleMode == Types::StyleMode::Never)
            {
                return false;
            }

            return std::any_of(
                segments.begin(),
                segments.end(),
                [](const Types::WriteSegment &segment) noexcept
                {
                    return segment.kind == Types::WriteSegmentKind::StyledText && !isDefaultStyle(segment.style);
                });
        }

        [[nodiscard]] IO::Types::Status writeSegmentsUnlocked(
            Types::OutputStream stream,
            std::span<const Types::WriteSegment> segments,
            const Types::SegmentWriteOptions &options)
        {
            Types::OutputCapabilities knownCapabilities;
            const Types::OutputCapabilities *knownCapabilitiesPtr = nullptr;
            if (segmentsNeedStyleCapabilities(segments, options.styleMode))
            {
                const Types::OutputCapabilityResult capabilities = Detail::Platform::getOutputCapabilities(stream);
                if (!capabilities.status.ok())
                {
                    return capabilities.status;
                }

                knownCapabilities = capabilities.capabilities;
                knownCapabilitiesPtr = &knownCapabilities;
            }

            IO::Types::Status status = validateSegments(stream, segments, options.styleMode, knownCapabilitiesPtr);
            if (!status.ok())
            {
                return status;
            }

            for (const Types::WriteSegment &segment : segments)
            {
                switch (segment.kind)
                {
                case Types::WriteSegmentKind::Text:
                    status = Detail::Platform::writeText(stream, segment.text);
                    if (!status.ok())
                    {
                        return status;
                    }
                    break;
                case Types::WriteSegmentKind::StyledText:
                    status = writeStyledTextOnlyUnlocked(stream, segment.text, options.styleMode, segment.style, knownCapabilitiesPtr);
                    if (!status.ok())
                    {
                        return status;
                    }
                    break;
                case Types::WriteSegmentKind::Bytes:
                {
                    const IO::Types::WriteResult writeResult = Detail::Platform::writeBytes(stream, segment.bytes);
                    if (!writeResult.status.ok())
                    {
                        return writeResult.status;
                    }
                    break;
                }
                }
            }

            status = appendLineEndingIfRequested(stream, options.appendLineEnding, options.lineEnding);
            if (!status.ok())
            {
                return status;
            }

            return flushIfRequested(stream, options.flushMode);
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
            std::string unsupportedMessage,
            IO::Types::FlushMode flushMode)
        {
            const Types::OutputCapabilityResult capabilities = Detail::Platform::getOutputCapabilities(stream);
            if (!capabilities.status.ok())
            {
                return capabilities.status;
            }

            if (!controlFeatureSupported(capabilities.capabilities, feature))
            {
                return unsupportedStatus(std::move(unsupportedMessage));
            }

            IO::Types::Status status = Detail::Platform::writeText(stream, sequence);
            if (!status.ok())
            {
                return status;
            }

            return flushIfRequested(stream, flushMode);
        }

        [[nodiscard]] std::string sanitizeTitle(std::string_view utf8Title)
        {
            std::string title;
            title.reserve(utf8Title.size());

            for (const char character : utf8Title)
            {
                const auto byte = static_cast<unsigned char>(character);
                if (byte == 0x1b || byte == 0x07 || byte < 0x20 || byte == 0x7f)
                {
                    title.push_back(' ');
                }
                else
                {
                    title.push_back(character);
                }
            }

            return title;
        }
    } // namespace

    Types::Color defaultColor() noexcept
    {
        return {};
    }

    Types::Color basicColor(Types::BasicColor color) noexcept
    {
        return {.kind = Types::ColorKind::Basic, .basic = color};
    }

    Types::Color rgbColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept
    {
        return {.kind = Types::ColorKind::Rgb, .red = red, .green = green, .blue = blue};
    }

    Types::InputMode makeInputMode(Types::InputModePreset preset) noexcept
    {
        switch (preset)
        {
        case Types::InputModePreset::Default:
        case Types::InputModePreset::InteractiveLine:
            return {.lineBuffered = true, .echoInput = true, .processControlKeys = true};
        case Types::InputModePreset::RawBytes:
            return {.lineBuffered = false, .echoInput = false, .processControlKeys = false};
        }

        return {};
    }

    Types::WriteSegment textSegment(std::string_view text) noexcept
    {
        return {.kind = Types::WriteSegmentKind::Text, .text = text};
    }

    Types::WriteSegment styledSegment(std::string_view text, const Types::TextStyle &style) noexcept
    {
        return {.kind = Types::WriteSegmentKind::StyledText, .text = text, .style = style};
    }

    Types::WriteSegment byteSegment(std::span<const std::byte> bytes) noexcept
    {
        return {.kind = Types::WriteSegmentKind::Bytes, .bytes = bytes};
    }

    InputModeScope::InputModeScope() noexcept = default;

    InputModeScope::InputModeScope(InputModeScope &&other) noexcept
        : stream_(other.stream_)
        , previousMode_(other.previousMode_)
        , status_(std::move(other.status_))
        , active_(other.active_)
    {
        other.active_ = false;
    }

    InputModeScope &InputModeScope::operator=(InputModeScope &&other) noexcept
    {
        if (this != &other)
        {
            static_cast<void>(restore());
            stream_ = other.stream_;
            previousMode_ = other.previousMode_;
            status_ = std::move(other.status_);
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
            status_ = setInputMode(stream_, previousMode_);
            active_ = false;
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
            static_cast<void>(leave());
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
            status_ = leaveAlternateScreen(stream_, options_);
            active_ = false;
        }

        return status_;
    }

    void AlternateScreenScope::release() noexcept
    {
        active_ = false;
    }

    CursorVisibilityScope::CursorVisibilityScope() noexcept = default;

    CursorVisibilityScope::CursorVisibilityScope(CursorVisibilityScope &&other) noexcept
        : stream_(other.stream_)
        , options_(other.options_)
        , status_(std::move(other.status_))
        , active_(other.active_)
    {
        other.active_ = false;
    }

    CursorVisibilityScope &CursorVisibilityScope::operator=(CursorVisibilityScope &&other) noexcept
    {
        if (this != &other)
        {
            static_cast<void>(restore());
            stream_ = other.stream_;
            options_ = other.options_;
            status_ = std::move(other.status_);
            active_ = other.active_;
            other.active_ = false;
        }

        return *this;
    }

    CursorVisibilityScope::~CursorVisibilityScope() noexcept
    {
        static_cast<void>(restore());
    }

    bool CursorVisibilityScope::active() const noexcept
    {
        return active_;
    }

    const IO::Types::Status &CursorVisibilityScope::status() const noexcept
    {
        return status_;
    }

    IO::Types::Status CursorVisibilityScope::restore() noexcept
    {
        if (active_)
        {
            status_ = setCursorVisible(stream_, true, options_);
            active_ = false;
        }

        return status_;
    }

    void CursorVisibilityScope::release() noexcept
    {
        active_ = false;
    }

    Reader::Reader(Types::InputStream defaultStream) noexcept
        : defaultStream_(defaultStream)
    {
    }

    Types::InputStream Reader::defaultStream() const noexcept
    {
        return defaultStream_;
    }

    void Reader::setDefaultStream(Types::InputStream stream) noexcept
    {
        defaultStream_ = stream;
    }

    Types::InputCapabilityResult Reader::getCapabilities() const
    {
        return getInputCapabilities(defaultStream_);
    }

    Types::InputCapabilityResult Reader::getCapabilities(Types::InputStream stream) const
    {
        return getInputCapabilities(stream);
    }

    Types::InputAvailabilityResult Reader::getInputAvailability() const
    {
        return Terminal::getInputAvailability(defaultStream_);
    }

    Types::InputAvailabilityResult Reader::getInputAvailability(Types::InputStream stream) const
    {
        return Terminal::getInputAvailability(stream);
    }

    Types::InputModeResult Reader::getInputMode() const
    {
        return Terminal::getInputMode(defaultStream_);
    }

    Types::InputModeResult Reader::getInputMode(Types::InputStream stream) const
    {
        return Terminal::getInputMode(stream);
    }

    IO::Types::Status Reader::setInputMode(const Types::InputMode &mode) const
    {
        return Terminal::setInputMode(defaultStream_, mode);
    }

    IO::Types::Status Reader::setInputMode(Types::InputStream stream, const Types::InputMode &mode) const
    {
        return Terminal::setInputMode(stream, mode);
    }

    IO::Types::Status Reader::restoreDefaultInputMode() const
    {
        return Terminal::restoreDefaultInputMode(defaultStream_);
    }

    IO::Types::Status Reader::restoreDefaultInputMode(Types::InputStream stream) const
    {
        return Terminal::restoreDefaultInputMode(stream);
    }

    InputModeScope Reader::scopedInputMode(const Types::InputMode &mode) const noexcept
    {
        return Terminal::scopedInputMode(defaultStream_, mode);
    }

    InputModeScope Reader::scopedInputMode(Types::InputStream stream, const Types::InputMode &mode) const noexcept
    {
        return Terminal::scopedInputMode(stream, mode);
    }

    Types::LineReadResult Reader::readLine(const Types::LineReadOptions &options) const
    {
        return Terminal::readLine(defaultStream_, options);
    }

    Types::TextReadResult Reader::readText(const Types::TextReadOptions &options) const
    {
        return Terminal::readText(defaultStream_, options);
    }

    Types::ByteReadResult Reader::readBytes(std::span<std::byte> outputBuffer, const Types::ByteReadOptions &options) const
    {
        return Terminal::readBytes(defaultStream_, outputBuffer, options);
    }

    Writer::Writer(Types::OutputStream defaultStream) noexcept
        : defaultStream_(defaultStream)
    {
    }

    Types::OutputStream Writer::defaultStream() const noexcept
    {
        return defaultStream_;
    }

    void Writer::setDefaultStream(Types::OutputStream stream) noexcept
    {
        defaultStream_ = stream;
    }

    Types::OutputCapabilityResult Writer::getCapabilities() const
    {
        return getOutputCapabilities(defaultStream_);
    }

    Types::OutputCapabilityResult Writer::getCapabilities(Types::OutputStream stream) const
    {
        return getOutputCapabilities(stream);
    }

    Types::TerminalSizeResult Writer::getTerminalSize() const
    {
        return Terminal::getTerminalSize(defaultStream_);
    }

    Types::TerminalSizeResult Writer::getTerminalSize(Types::OutputStream stream) const
    {
        return Terminal::getTerminalSize(stream);
    }

    IO::Types::Status Writer::writeText(std::string_view utf8Text, const Types::TextWriteOptions &options) const
    {
        return Terminal::writeText(defaultStream_, utf8Text, options);
    }

    IO::Types::Status Writer::writeLine(std::string_view utf8Text, const Types::LineWriteOptions &options) const
    {
        return Terminal::writeLine(defaultStream_, utf8Text, options);
    }

    IO::Types::WriteResult Writer::writeBytes(std::span<const std::byte> bytes, const Types::ByteWriteOptions &options) const
    {
        return Terminal::writeBytes(defaultStream_, bytes, options);
    }

    IO::Types::Status Writer::writeSegments(std::span<const Types::WriteSegment> segments, const Types::SegmentWriteOptions &options) const
    {
        return Terminal::writeSegments(defaultStream_, segments, options);
    }

    IO::Types::Status Writer::flush(IO::Types::FlushMode mode) const
    {
        return Terminal::flush(defaultStream_, mode);
    }

    IO::Types::Status Writer::flush(Types::OutputStream stream, IO::Types::FlushMode mode) const
    {
        return Terminal::flush(stream, mode);
    }

    IO::Types::Status Writer::resetStyle(const Types::ControlOptions &options) const
    {
        return Terminal::resetStyle(defaultStream_, options);
    }

    IO::Types::Status Writer::resetStyle(Types::OutputStream stream, const Types::ControlOptions &options) const
    {
        return Terminal::resetStyle(stream, options);
    }

    IO::Types::Status Writer::moveCursor(Types::CursorMoveDirection direction, std::uint32_t amount, const Types::ControlOptions &options) const
    {
        return Terminal::moveCursor(defaultStream_, direction, amount, options);
    }

    IO::Types::Status Writer::moveCursor(
        Types::OutputStream stream,
        Types::CursorMoveDirection direction,
        std::uint32_t amount,
        const Types::ControlOptions &options) const
    {
        return Terminal::moveCursor(stream, direction, amount, options);
    }

    IO::Types::Status Writer::setCursorPosition(Types::CursorPosition position, const Types::ControlOptions &options) const
    {
        return Terminal::setCursorPosition(defaultStream_, position, options);
    }

    IO::Types::Status Writer::setCursorPosition(Types::OutputStream stream, Types::CursorPosition position, const Types::ControlOptions &options)
        const
    {
        return Terminal::setCursorPosition(stream, position, options);
    }

    Types::CursorPositionResult Writer::getCursorPosition(const Types::CursorPositionQueryOptions &options) const
    {
        return Terminal::getCursorPosition(defaultStream_, options);
    }

    Types::CursorPositionResult Writer::getCursorPosition(Types::OutputStream stream, const Types::CursorPositionQueryOptions &options) const
    {
        return Terminal::getCursorPosition(stream, options);
    }

    IO::Types::Status Writer::saveCursorPosition(const Types::ControlOptions &options) const
    {
        return Terminal::saveCursorPosition(defaultStream_, options);
    }

    IO::Types::Status Writer::saveCursorPosition(Types::OutputStream stream, const Types::ControlOptions &options) const
    {
        return Terminal::saveCursorPosition(stream, options);
    }

    IO::Types::Status Writer::restoreCursorPosition(const Types::ControlOptions &options) const
    {
        return Terminal::restoreCursorPosition(defaultStream_, options);
    }

    IO::Types::Status Writer::restoreCursorPosition(Types::OutputStream stream, const Types::ControlOptions &options) const
    {
        return Terminal::restoreCursorPosition(stream, options);
    }

    IO::Types::Status Writer::setCursorVisible(bool visible, const Types::ControlOptions &options) const
    {
        return Terminal::setCursorVisible(defaultStream_, visible, options);
    }

    IO::Types::Status Writer::setCursorVisible(Types::OutputStream stream, bool visible, const Types::ControlOptions &options) const
    {
        return Terminal::setCursorVisible(stream, visible, options);
    }

    CursorVisibilityScope Writer::scopedCursorHidden(const Types::ControlOptions &options) const noexcept
    {
        return Terminal::scopedCursorHidden(defaultStream_, options);
    }

    CursorVisibilityScope Writer::scopedCursorHidden(Types::OutputStream stream, const Types::ControlOptions &options) const noexcept
    {
        return Terminal::scopedCursorHidden(stream, options);
    }

    IO::Types::Status Writer::clear(Types::ClearTarget target, const Types::ControlOptions &options) const
    {
        return Terminal::clear(defaultStream_, target, options);
    }

    IO::Types::Status Writer::clear(Types::OutputStream stream, Types::ClearTarget target, const Types::ControlOptions &options) const
    {
        return Terminal::clear(stream, target, options);
    }

    IO::Types::Status Writer::scroll(Types::ScrollDirection direction, std::uint32_t lines, const Types::ControlOptions &options) const
    {
        return Terminal::scroll(defaultStream_, direction, lines, options);
    }

    IO::Types::Status Writer::scroll(
        Types::OutputStream stream,
        Types::ScrollDirection direction,
        std::uint32_t lines,
        const Types::ControlOptions &options) const
    {
        return Terminal::scroll(stream, direction, lines, options);
    }

    IO::Types::Status Writer::enterAlternateScreen(const Types::ControlOptions &options) const
    {
        return Terminal::enterAlternateScreen(defaultStream_, options);
    }

    IO::Types::Status Writer::enterAlternateScreen(Types::OutputStream stream, const Types::ControlOptions &options) const
    {
        return Terminal::enterAlternateScreen(stream, options);
    }

    IO::Types::Status Writer::leaveAlternateScreen(const Types::ControlOptions &options) const
    {
        return Terminal::leaveAlternateScreen(defaultStream_, options);
    }

    IO::Types::Status Writer::leaveAlternateScreen(Types::OutputStream stream, const Types::ControlOptions &options) const
    {
        return Terminal::leaveAlternateScreen(stream, options);
    }

    AlternateScreenScope Writer::scopedAlternateScreen(const Types::ControlOptions &options) const noexcept
    {
        return Terminal::scopedAlternateScreen(defaultStream_, options);
    }

    AlternateScreenScope Writer::scopedAlternateScreen(Types::OutputStream stream, const Types::ControlOptions &options) const noexcept
    {
        return Terminal::scopedAlternateScreen(stream, options);
    }

    IO::Types::Status Writer::setTitle(std::string_view utf8Title, const Types::ControlOptions &options) const
    {
        return Terminal::setTitle(defaultStream_, utf8Title, options);
    }

    IO::Types::Status Writer::setTitle(Types::OutputStream stream, std::string_view utf8Title, const Types::ControlOptions &options) const
    {
        return Terminal::setTitle(stream, utf8Title, options);
    }

    IO::Types::Status Writer::ringBell(const Types::ControlOptions &options) const
    {
        return Terminal::ringBell(defaultStream_, options);
    }

    IO::Types::Status Writer::ringBell(Types::OutputStream stream, const Types::ControlOptions &options) const
    {
        return Terminal::ringBell(stream, options);
    }

    OutputBuffer::OutputBuffer(Types::LineEnding lineEnding)
        : lineEnding_(lineEnding)
    {
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

    IO::Types::Status OutputBuffer::writeTo(const Writer &writer, const Types::TextWriteOptions &options) const
    {
        return writer.writeText(text_, options);
    }

    IO::Types::Status OutputBuffer::flushTo(const Writer &writer, const Types::TextWriteOptions &options)
    {
        IO::Types::Status status = writeTo(writer, options);
        if (status.ok())
        {
            clear();
        }

        return status;
    }

    Types::InputCapabilityResult getInputCapabilities(Types::InputStream stream)
    {
        std::lock_guard lock(inputMutex(stream));
        return Detail::Platform::getInputCapabilities(stream);
    }

    Types::OutputCapabilityResult getOutputCapabilities(Types::OutputStream stream)
    {
        std::lock_guard lock(outputMutex(stream));
        return Detail::Platform::getOutputCapabilities(stream);
    }

    Types::TerminalSizeResult getTerminalSize(Types::OutputStream stream)
    {
        std::lock_guard lock(outputMutex(stream));
        return Detail::Platform::getTerminalSize(stream);
    }

    Types::InputAvailabilityResult getInputAvailability(Types::InputStream stream)
    {
        std::lock_guard lock(inputMutex(stream));
        return Detail::Platform::getInputAvailability(stream);
    }

    Types::InputModeResult getInputMode(Types::InputStream stream)
    {
        std::lock_guard lock(inputMutex(stream));
        return Detail::Platform::getInputMode(stream);
    }

    IO::Types::Status setInputMode(const Types::InputMode &mode)
    {
        return setInputMode(Types::InputStream::Stdin, mode);
    }

    IO::Types::Status setInputMode(Types::InputStream stream, const Types::InputMode &mode)
    {
        std::lock_guard lock(inputMutex(stream));
        return Detail::Platform::setInputMode(stream, mode);
    }

    IO::Types::Status restoreDefaultInputMode(Types::InputStream stream)
    {
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

        const Types::InputModeResult previousMode = getInputMode(stream);
        scope.status_ = previousMode.status;
        if (!scope.status_.ok())
        {
            return scope;
        }

        scope.previousMode_ = previousMode.mode;
        scope.status_ = setInputMode(stream, mode);
        scope.active_ = scope.status_.ok();
        return scope;
    }

    Types::LineReadResult readLine(const Types::LineReadOptions &options)
    {
        return readLine(Types::InputStream::Stdin, options);
    }

    Types::LineReadResult readLine(Types::InputStream stream, const Types::LineReadOptions &options)
    {
        std::lock_guard lock(inputMutex(stream));
        return Detail::Platform::readLine(stream, options);
    }

    Types::TextReadResult readText(const Types::TextReadOptions &options)
    {
        return readText(Types::InputStream::Stdin, options);
    }

    Types::TextReadResult readText(Types::InputStream stream, const Types::TextReadOptions &options)
    {
        std::lock_guard lock(inputMutex(stream));
        return Detail::Platform::readText(stream, options);
    }

    Types::ByteReadResult readBytes(std::span<std::byte> outputBuffer, const Types::ByteReadOptions &options)
    {
        return readBytes(Types::InputStream::Stdin, outputBuffer, options);
    }

    Types::ByteReadResult readBytes(Types::InputStream stream, std::span<std::byte> outputBuffer, const Types::ByteReadOptions &options)
    {
        std::lock_guard lock(inputMutex(stream));
        return Detail::Platform::readBytes(stream, outputBuffer, options);
    }

    IO::Types::Status writeText(std::string_view utf8Text, const Types::TextWriteOptions &options)
    {
        return writeText(Types::OutputStream::Stdout, utf8Text, options);
    }

    IO::Types::Status writeText(Types::OutputStream stream, std::string_view utf8Text, const Types::TextWriteOptions &options)
    {
        std::lock_guard lock(outputMutex(stream));
        return writeTextUnlocked(stream, utf8Text, options);
    }

    IO::Types::Status writeLine(std::string_view utf8Text, const Types::LineWriteOptions &options)
    {
        return writeLine(Types::OutputStream::Stdout, utf8Text, options);
    }

    IO::Types::Status writeLine(Types::OutputStream stream, std::string_view utf8Text, const Types::LineWriteOptions &options)
    {
        std::lock_guard lock(outputMutex(stream));
        return writeLineUnlocked(stream, utf8Text, options);
    }

    IO::Types::WriteResult writeBytes(std::span<const std::byte> bytes, const Types::ByteWriteOptions &options)
    {
        return writeBytes(Types::OutputStream::Stdout, bytes, options);
    }

    IO::Types::WriteResult writeBytes(Types::OutputStream stream, std::span<const std::byte> bytes, const Types::ByteWriteOptions &options)
    {
        std::lock_guard lock(outputMutex(stream));
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
        std::lock_guard lock(outputMutex(stream));
        return writeSegmentsUnlocked(stream, segments, options);
    }

    IO::Types::Status flush(Types::OutputStream stream, IO::Types::FlushMode mode)
    {
        std::lock_guard lock(outputMutex(stream));
        return Detail::Platform::flush(stream, mode);
    }

    IO::Types::Status resetStyle(Types::OutputStream stream, const Types::ControlOptions &options)
    {
        std::lock_guard lock(outputMutex(stream));
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
        std::lock_guard lock(outputMutex(stream));

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

        return writeControlSequenceUnlocked(
            stream,
            makeCountedCsi(amount, command),
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
        std::lock_guard lock(outputMutex(stream));

        const std::string sequence = "\x1b[" + std::to_string(static_cast<std::uint64_t>(position.row) + 1) + ";" +
                                     std::to_string(static_cast<std::uint64_t>(position.column) + 1) + "H";

        return writeControlSequenceUnlocked(
            stream,
            sequence,
            ControlFeature::CursorMovement,
            "Terminal cursor positioning is unsupported for this output stream.",
            options.flushMode);
    }

    Types::CursorPositionResult getCursorPosition(const Types::CursorPositionQueryOptions &options)
    {
        return getCursorPosition(Types::OutputStream::Stdout, options);
    }

    Types::CursorPositionResult getCursorPosition(Types::OutputStream stream, const Types::CursorPositionQueryOptions &options)
    {
        std::lock_guard outputLock(outputMutex(stream));
        std::lock_guard inputLock(inputMutex(Types::InputStream::Stdin));

        static_cast<void>(options.timeout);
        IO::Types::Status status = flushIfRequested(stream, options.flushMode);
        if (!status.ok())
        {
            return {.status = status, .position = {}};
        }

        return Detail::Platform::getCursorPosition(stream);
    }

    IO::Types::Status saveCursorPosition(Types::OutputStream stream, const Types::ControlOptions &options)
    {
        std::lock_guard lock(outputMutex(stream));
        return writeControlSequenceUnlocked(
            stream,
            "\x1b[s",
            ControlFeature::CursorSaveRestore,
            "Terminal cursor save is unsupported for this output stream.",
            options.flushMode);
    }

    IO::Types::Status restoreCursorPosition(Types::OutputStream stream, const Types::ControlOptions &options)
    {
        std::lock_guard lock(outputMutex(stream));
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
        std::lock_guard lock(outputMutex(stream));
        return writeControlSequenceUnlocked(
            stream,
            visible ? "\x1b[?25h" : "\x1b[?25l",
            ControlFeature::CursorVisibility,
            "Terminal cursor visibility is unsupported for this output stream.",
            options.flushMode);
    }

    CursorVisibilityScope scopedCursorHidden(const Types::ControlOptions &options) noexcept
    {
        return scopedCursorHidden(Types::OutputStream::Stdout, options);
    }

    CursorVisibilityScope scopedCursorHidden(Types::OutputStream stream, const Types::ControlOptions &options) noexcept
    {
        CursorVisibilityScope scope;
        scope.stream_ = stream;
        scope.options_ = options;
        scope.status_ = setCursorVisible(stream, false, options);
        scope.active_ = scope.status_.ok();
        return scope;
    }

    IO::Types::Status clear(Types::ClearTarget target, const Types::ControlOptions &options)
    {
        return clear(Types::OutputStream::Stdout, target, options);
    }

    IO::Types::Status clear(Types::OutputStream stream, Types::ClearTarget target, const Types::ControlOptions &options)
    {
        std::lock_guard lock(outputMutex(stream));

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
        std::lock_guard lock(outputMutex(stream));

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

        return writeControlSequenceUnlocked(
            stream,
            makeCountedCsi(lines, command),
            ControlFeature::Scroll,
            "Terminal scrolling is unsupported for this output stream.",
            options.flushMode);
    }

    IO::Types::Status enterAlternateScreen(Types::OutputStream stream, const Types::ControlOptions &options)
    {
        std::lock_guard lock(outputMutex(stream));
        return writeControlSequenceUnlocked(
            stream,
            "\x1b[?1049h",
            ControlFeature::AlternateScreen,
            "Terminal alternate screen is unsupported for this output stream.",
            options.flushMode);
    }

    IO::Types::Status leaveAlternateScreen(Types::OutputStream stream, const Types::ControlOptions &options)
    {
        std::lock_guard lock(outputMutex(stream));
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
        scope.status_ = enterAlternateScreen(stream, options);
        scope.active_ = scope.status_.ok();
        return scope;
    }

    IO::Types::Status setTitle(std::string_view utf8Title, const Types::ControlOptions &options)
    {
        return setTitle(Types::OutputStream::Stdout, utf8Title, options);
    }

    IO::Types::Status setTitle(Types::OutputStream stream, std::string_view utf8Title, const Types::ControlOptions &options)
    {
        std::lock_guard lock(outputMutex(stream));

        const std::string sequence = "\x1b]0;" + sanitizeTitle(utf8Title) + "\x07";
        return writeControlSequenceUnlocked(
            stream,
            sequence,
            ControlFeature::Title,
            "Terminal title controls are unsupported for this output stream.",
            options.flushMode);
    }

    IO::Types::Status ringBell(Types::OutputStream stream, const Types::ControlOptions &options)
    {
        std::lock_guard lock(outputMutex(stream));
        return writeControlSequenceUnlocked(
            stream,
            "\a",
            ControlFeature::Bell,
            "Terminal bell output is unsupported for this output stream.",
            options.flushMode);
    }
} // namespace GameWIP::Terminal
