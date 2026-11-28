/// @file style.cpp
/// @brief Terminal style capability resolution and SGR encoding.

#include "terminal/internal/style.h"

#include "terminal/internal/status.h"
#include "terminal/platform/backend.h"
#include "terminal/style.h"

#include <array>
#include <charconv>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <string_view>

namespace GameWIP::Terminal::Internal
{
    namespace
    {
        using ErrorCode = GameWIP::Status::Types::Code;
        /// @brief Returns whether a color requests the terminal default.
        [[nodiscard]] bool isDefaultColor(const Types::Style::Color &color) noexcept
        {
            return color.kind() == Types::Style::ColorKind::Default;
        }

        /// @brief Checks one color request against observed stream capabilities.
        [[nodiscard]] bool colorSupported(const Types::Style::Color &color, const Types::Style::Capabilities &capabilities) noexcept
        {
            switch(color.kind())
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

        /// @brief Approximates an unsupported RGB color with the nearest portable basic color.
        [[nodiscard]] Types::Style::Color fallbackColor(Types::Style::Color color, const Types::Style::Capabilities &capabilities) noexcept
        {
            if(color.kind() != Types::Style::ColorKind::Rgb || capabilities.rgbColor || !capabilities.basicColor)
            {
                return color;
            }

            constexpr std::array<std::array<int, 3>, 16> palette{
                {{{0, 0, 0}},
                 {{128, 0, 0}},
                 {{0, 128, 0}},
                 {{128, 128, 0}},
                 {{0, 0, 128}},
                 {{128, 0, 128}},
                 {{0, 128, 128}},
                 {{192, 192, 192}},
                 {{128, 128, 128}},
                 {{255, 0, 0}},
                 {{0, 255, 0}},
                 {{255, 255, 0}},
                 {{0, 0, 255}},
                 {{255, 0, 255}},
                 {{0, 255, 255}},
                 {{255, 255, 255}}}};
            const std::array<int, 3> requested{color.red(), color.green(), color.blue()};
            std::size_t closestIndex = 0;
            int closestDistance = std::numeric_limits<int>::max();

            for(std::size_t index = 0; index < palette.size(); ++index)
            {
                const int red = requested[0] - palette[index][0];
                const int green = requested[1] - palette[index][1];
                const int blue = requested[2] - palette[index][2];
                const int distance = red * red + green * green + blue * blue;

                if(distance < closestDistance)
                {
                    closestDistance = distance;
                    closestIndex = index;
                }
            }

            return basicColor(static_cast<Types::Style::BasicColor>(closestIndex));
        }

        /// @brief Replaces unsupported RGB colors when automatic styling can use basic colors.
        [[nodiscard]] Types::Style::Request fallbackColors(Types::Style::Request style, const Types::Style::Capabilities &capabilities) noexcept
        {
            style.foreground = fallbackColor(style.foreground, capabilities);
            style.background = fallbackColor(style.background, capabilities);
            return style;
        }

        /// @brief Converts a basic color to its foreground or background SGR code.
        [[nodiscard]] int basicColorCode(Types::Style::BasicColor color, bool foreground) noexcept
        {
            const int value = static_cast<int>(color);
            const bool bright = value >= 8;
            const int colorIndex = bright ? value - 8 : value;
            return (foreground ? (bright ? 90 : 30) : (bright ? 100 : 40)) + colorIndex;
        }

        /// @brief Appends known-bounded literal bytes to a style sequence.
        void appendSequenceText(StyleSequence &sequence, std::string_view text) noexcept
        {
            for(const char character : text)
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
            if(hasParameter)
            {
                sequence.bytes[sequence.size++] = ';';
            }
            appendSequenceNumber(sequence, parameter);
            hasParameter = true;
        }

        /// @brief Appends basic or RGB SGR parameters for one color channel.
        void appendColorSgr(StyleSequence &sequence, const Types::Style::Color &color, bool foreground, bool &hasParameter) noexcept
        {
            switch(color.kind())
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

    } // namespace

    /// @brief Returns whether a style needs no SGR output.
    [[nodiscard]] bool isDefaultStyle(const Types::Style::Request &style) noexcept
    {
        return isDefaultColor(style.foreground) && isDefaultColor(style.background) && !style.bold && !style.dim && !style.italic &&
               !style.underline && !style.inverse && !style.strikethrough;
    }

    /// @brief Builds the complete SGR prefix for a previously validated style.
    [[nodiscard]] StyleSequence makeStyleSequence(const Types::Style::Request &style) noexcept
    {
        StyleSequence sequence;
        appendSequenceText(sequence, "\x1b[");
        bool hasParameter = false;

        if(style.bold)
        {
            appendSgrParameter(sequence, 1, hasParameter);
        }

        if(style.dim)
        {
            appendSgrParameter(sequence, 2, hasParameter);
        }

        if(style.italic)
        {
            appendSgrParameter(sequence, 3, hasParameter);
        }

        if(style.underline)
        {
            appendSgrParameter(sequence, 4, hasParameter);
        }

        if(style.inverse)
        {
            appendSgrParameter(sequence, 7, hasParameter);
        }

        if(style.strikethrough)
        {
            appendSgrParameter(sequence, 9, hasParameter);
        }

        appendColorSgr(sequence, style.foreground, true, hasParameter);
        appendColorSgr(sequence, style.background, false, hasParameter);

        if(!hasParameter)
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
        switch(mode)
        {
        case Types::Style::Mode::Never:
            return {};
        case Types::Style::Mode::Auto:
        case Types::Style::Mode::Required:
            break;
        default:
            return {.status = statusWithMessage(ErrorCode::InvalidArgument, "Unknown terminal style mode.")};
        }

        if(isDefaultStyle(style))
        {
            return {};
        }

        const Types::Style::Request resolvedStyle = mode == Types::Style::Mode::Auto ? fallbackColors(style, capabilities) : style;

        if(styleSupported(resolvedStyle, capabilities))
        {
            return {.status = GameWIP::Status::Types::Status{}, .emitStyle = true, .style = resolvedStyle};
        }

        if(mode == Types::Style::Mode::Required)
        {
            return {.status = statusWithMessage(ErrorCode::Unsupported, "Terminal output stream does not support the requested style.")};
        }

        return {};
    }

    /// @brief Prepares output when needed and resolves the style plan for one stream.
    [[nodiscard]] StylePlan stylePlan(Types::Output::Stream stream, Types::Style::Mode mode, const Types::Style::Request &style)
    {
        switch(mode)
        {
        case Types::Style::Mode::Never:
            return {};
        case Types::Style::Mode::Auto:
        case Types::Style::Mode::Required:
            break;
        default:
            return {.status = statusWithMessage(ErrorCode::InvalidArgument, "Unknown terminal style mode.")};
        }

        if(isDefaultStyle(style))
        {
            return {};
        }

        if(mode == Types::Style::Mode::Never)
        {
            return {};
        }
        Types::Output::CapabilitiesResult capabilities = Platform::getOutputCapabilities(stream);

        if(capabilities.ok())
        {
            StylePlan plan = stylePlanForCapabilities(mode, style, capabilities.value().style);

            if(plan.status.ok() && plan.emitStyle)
            {
                return plan;
            }

            if(capabilities.value().kind == Types::StreamKind::Redirected)
            {
                return plan;
            }
        }

        capabilities = Platform::prepareOutput(stream);

        if(capabilities.ok())
        {
            StylePlan plan = stylePlanForCapabilities(mode, style, capabilities.value().style);

            if(plan.status.ok() && plan.emitStyle)
            {
                return plan;
            }
        }

        if(mode == Types::Style::Mode::Auto)
        {
            return {};
        }

        if(!capabilities.ok())
        {
            return {.status = capabilities.status()};
        }

        return {.status = statusWithMessage(ErrorCode::Unsupported, "Terminal output stream does not support the requested style.")};
    }

    /// @brief Returns whether a stream supports at least one style feature.
    [[nodiscard]] bool anyStyleSupported(const Types::Style::Capabilities &capabilities) noexcept
    {
        return capabilities.basicColor || capabilities.rgbColor || capabilities.bold || capabilities.dim || capabilities.italic ||
               capabilities.underline || capabilities.inverse || capabilities.strikethrough;
    }
} // namespace GameWIP::Terminal::Internal

namespace GameWIP::Terminal
{
    namespace
    {
        /// @brief Validates all supported basic and bright color enum values.
        [[nodiscard]] bool isKnownBasicColor(Types::Style::BasicColor color) noexcept
        {
            switch(color)
            {
            case Types::Style::BasicColor::Black:
            case Types::Style::BasicColor::Red:
            case Types::Style::BasicColor::Green:
            case Types::Style::BasicColor::Yellow:
            case Types::Style::BasicColor::Blue:
            case Types::Style::BasicColor::Magenta:
            case Types::Style::BasicColor::Cyan:
            case Types::Style::BasicColor::White:
            case Types::Style::BasicColor::BrightBlack:
            case Types::Style::BasicColor::BrightRed:
            case Types::Style::BasicColor::BrightGreen:
            case Types::Style::BasicColor::BrightYellow:
            case Types::Style::BasicColor::BrightBlue:
            case Types::Style::BasicColor::BrightMagenta:
            case Types::Style::BasicColor::BrightCyan:
            case Types::Style::BasicColor::BrightWhite:
                return true;
            }

            return false;
        }
    } // namespace

    Types::Style::Color defaultColor() noexcept
    {
        return {};
    }

    Types::Style::Color::Color(Types::Style::BasicColor color) noexcept
        : kind_(Types::Style::ColorKind::Basic)
        , basic_(color)
    {
    }

    Types::Style::Color::Color(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept
        : kind_(Types::Style::ColorKind::Rgb)
        , red_(red)
        , green_(green)
        , blue_(blue)
    {
    }

    Types::Style::Color basicColor(Types::Style::BasicColor color) noexcept
    {
        if(!isKnownBasicColor(color))
        {
            return {};
        }
        return Types::Style::Color(color);
    }

    Types::Style::Color rgbColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept
    {
        return Types::Style::Color(red, green, blue);
    }
} // namespace GameWIP::Terminal
