/// @file style.h
/// @brief Public terminal styling types and color factories.

#pragma once

#include <cstdint>

#include "terminal/terminal_export.h"

namespace GameWIP::Terminal
{
    namespace Types::Style
    {
        enum class BasicColor;
        struct Color;
    } // namespace Types::Style

    /// @brief Creates the terminal default color.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Style::Color defaultColor() noexcept;

    /// @brief Creates a portable basic terminal color.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Style::Color basicColor(Types::Style::BasicColor color) noexcept;

    /// @brief Creates an RGB terminal color.
    [[nodiscard]] GAMEWIP_TERMINAL_EXPORT Types::Style::Color rgbColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept;

    namespace Types::Style
    {
        /// @brief Styling behavior requested by text output calls.
        enum class Mode
        {
            /// @brief Never emit styling; write plain text.
            Never,

            /// @brief Emit styling only when supported.
            Auto,

            /// @brief Require the requested styling and fail with Unsupported when it is unavailable.
            Required
        };

        /// @brief Kind of terminal color stored in Types::Style::Color.
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
            [[nodiscard]] Types::Style::ColorKind kind() const noexcept
            {
                return kind_;
            }

            /// @brief Returns the stored basic color.
            /// @note Meaningful only when kind() is Types::Style::ColorKind::Basic.
            [[nodiscard]] Types::Style::BasicColor basic() const noexcept
            {
                return basic_;
            }

            /// @brief Returns the stored red channel.
            /// @note Meaningful only when kind() is Types::Style::ColorKind::Rgb.
            [[nodiscard]] std::uint8_t red() const noexcept
            {
                return red_;
            }

            /// @brief Returns the stored green channel.
            /// @note Meaningful only when kind() is Types::Style::ColorKind::Rgb.
            [[nodiscard]] std::uint8_t green() const noexcept
            {
                return green_;
            }

            /// @brief Returns the stored blue channel.
            /// @note Meaningful only when kind() is Types::Style::ColorKind::Rgb.
            [[nodiscard]] std::uint8_t blue() const noexcept
            {
                return blue_;
            }

        private:
            friend GAMEWIP_TERMINAL_EXPORT Color GameWIP::Terminal::basicColor(Types::Style::BasicColor color) noexcept;
            friend GAMEWIP_TERMINAL_EXPORT Color GameWIP::Terminal::rgbColor(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept;

            explicit Color(Types::Style::BasicColor color) noexcept;
            Color(std::uint8_t red, std::uint8_t green, std::uint8_t blue) noexcept;

            Types::Style::ColorKind kind_ = Types::Style::ColorKind::Default;
            Types::Style::BasicColor basic_ = Types::Style::BasicColor::White;
            std::uint8_t red_ = 0;
            std::uint8_t green_ = 0;
            std::uint8_t blue_ = 0;
        };

        /// @brief Portable terminal text style request.
        struct Request
        {
            /// @brief Requested foreground color.
            Types::Style::Color foreground{};

            /// @brief Requested background color.
            Types::Style::Color background{};

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
        struct Capabilities
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
    } // namespace Types::Style
} // namespace GameWIP::Terminal
