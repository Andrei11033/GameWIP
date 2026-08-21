/// @file style.cpp
/// @brief Terminal color and styling value implementation.

#include "terminal/style.h"

namespace GameWIP::Terminal
{
    namespace
    {
        /// @brief Validates all supported basic and bright color enum values.
        [[nodiscard]] bool isKnownBasicColor(Types::Style::BasicColor color) noexcept
        {
            switch (color)
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
        if (!isKnownBasicColor(color))
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
