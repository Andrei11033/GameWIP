/// @file layout_template.h
/// @brief Compile-time validation and runtime compilation for Logger layouts.

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace GameWIP::Logger::Detail
{
    /// @cond INTERNAL
    // ------------------------------------------------------------
    // UTF-8 validation
    // ------------------------------------------------------------

    [[nodiscard]] constexpr bool isValidUtf8(std::string_view text) noexcept
    {
        const auto continuation = [](unsigned char value)
        {
            return (value & 0xC0u) == 0x80u;
        };
        for(std::size_t index = 0; index < text.size();)
        {
            const auto first = static_cast<unsigned char>(text[index]);
            if(first <= 0x7Fu)
            {
                ++index;
                continue;
            }
            std::size_t length = 0;
            if(first >= 0xC2u && first <= 0xDFu)
            {
                length = 2;
            }
            else if(first >= 0xE0u && first <= 0xEFu)
            {
                length = 3;
            }
            else if(first >= 0xF0u && first <= 0xF4u)
            {
                length = 4;
            }
            else
            {
                return false;
            }
            if(length > text.size() - index)
            {
                return false;
            }
            const auto second = static_cast<unsigned char>(text[index + 1]);
            if(!continuation(second) || (first == 0xE0u && second < 0xA0u) || (first == 0xEDu && second > 0x9Fu) ||
               (first == 0xF0u && second < 0x90u) || (first == 0xF4u && second > 0x8Fu))
            {
                return false;
            }
            for(std::size_t offset = 2; offset < length; ++offset)
            {
                if(!continuation(static_cast<unsigned char>(text[index + offset])))
                {
                    return false;
                }
            }
            index += length;
        }
        return true;
    }

    // ------------------------------------------------------------
    // Layout grammar and semantic roles
    // ------------------------------------------------------------

    inline constexpr std::size_t kMaxLayoutColorScopeDepth = 8;

    [[nodiscard]] constexpr char layoutColorRoleCode(std::string_view role) noexcept
    {
        if(role == "timestamp")
        {
            return 'T';
        }
        if(role == "severity")
        {
            return 'L';
        }
        if(role == "area")
        {
            return 'A';
        }
        if(role == "message")
        {
            return 'M';
        }
        if(role == "field")
        {
            return 'F';
        }
        if(role == "code")
        {
            return 'C';
        }
        if(role == "path")
        {
            return 'P';
        }
        if(role == "number")
        {
            return 'N';
        }
        if(role == "quoted")
        {
            return 'Q';
        }
        return 0;
    }

    [[nodiscard]] constexpr bool isValidLayoutToken(std::string_view token) noexcept
    {
        constexpr std::array<std::string_view, 9>
            placeholders{"time", "level", "area", "message", "source_file", "source_function", "source_line", "thread_id", "process_id"};
        const std::size_t separator = token.find(':');
        const std::string_view name = token.substr(0, separator);
        if(std::ranges::find(placeholders, name) == placeholders.end())
        {
            return separator == std::string_view::npos && token == "fields";
        }
        if(separator == std::string_view::npos)
        {
            return true;
        }
        const std::string_view spec = token.substr(separator + 1);
        if(spec.size() < 2 || (spec.front() != '<' && spec.front() != '^' && spec.front() != '>'))
        {
            return false;
        }
        const std::string_view width = spec.substr(1);
        if(width == "auto")
        {
            return name == "area" || name == "level";
        }
        if(width.empty())
        {
            return false;
        }
        constexpr std::size_t kMaximumLayoutWidth = 4096;
        std::size_t parsedWidth = 0;
        for(const char character : width)
        {
            if(character < '0' || character > '9')
            {
                return false;
            }
            const std::size_t digit = static_cast<std::size_t>(character - '0');
            if(parsedWidth > (kMaximumLayoutWidth - digit) / 10)
            {
                return false;
            }
            parsedWidth = parsedWidth * 10 + digit;
        }
        return parsedWidth <= kMaximumLayoutWidth;
    }

    [[nodiscard]] constexpr bool isValidLayoutTemplate(std::string_view layout) noexcept
    {
        std::size_t colorScopeDepth = 0;
        for(std::size_t index = 0; index < layout.size(); ++index)
        {
            if(layout[index] == '{')
            {
                if(index + 1 < layout.size() && layout[index + 1] == '{')
                {
                    ++index;
                    continue;
                }
                const std::size_t close = layout.find('}', index + 1);
                if(close == std::string_view::npos)
                {
                    return false;
                }
                const std::string_view token = layout.substr(index + 1, close - index - 1);
                if(token == "/")
                {
                    if(colorScopeDepth == 0)
                    {
                        return false;
                    }
                    --colorScopeDepth;
                }
                else if(token.starts_with('@'))
                {
                    if(layoutColorRoleCode(token.substr(1)) == 0 || colorScopeDepth == kMaxLayoutColorScopeDepth)
                    {
                        return false;
                    }
                    ++colorScopeDepth;
                }
                else if(!isValidLayoutToken(token))
                {
                    return false;
                }
                index = close;
            }
            else if(layout[index] == '}')
            {
                if(index + 1 < layout.size() && layout[index + 1] == '}')
                {
                    ++index;
                    continue;
                }
                return false;
            }
        }
        return colorScopeDepth == 0;
    }

    // ------------------------------------------------------------
    // Compiled layout instructions
    // ------------------------------------------------------------

    inline constexpr char kCompiledLayoutMarker = static_cast<char>(0xF6);

    [[nodiscard]] constexpr char layoutPlaceholderCode(std::string_view name) noexcept
    {
        if(name == "time")
        {
            return 't';
        }
        if(name == "level")
        {
            return 'l';
        }
        if(name == "area")
        {
            return 'a';
        }
        if(name == "message")
        {
            return 'm';
        }
        if(name == "fields")
        {
            return 'f';
        }
        if(name == "source_file")
        {
            return 'F';
        }
        if(name == "source_function")
        {
            return 'u';
        }
        if(name == "source_line")
        {
            return 'n';
        }
        if(name == "thread_id")
        {
            return 'i';
        }
        if(name == "process_id")
        {
            return 'p';
        }
        return 0;
    }

    /// @brief Compiles a validated layout into compact renderer instructions.
    [[nodiscard]] inline std::string compileLayoutTemplate(std::string_view layout)
    {
        std::string compiled;
        compiled.reserve(layout.size());
        const auto appendInstruction = [&compiled](char operation)
        {
            compiled.push_back(kCompiledLayoutMarker);
            compiled.push_back(operation);
        };

        for(std::size_t index = 0; index < layout.size(); ++index)
        {
            if((layout[index] == '{' || layout[index] == '}') && index + 1 < layout.size() && layout[index + 1] == layout[index])
            {
                compiled.push_back(layout[index]);
                ++index;
                continue;
            }
            if(layout[index] != '{')
            {
                compiled.push_back(layout[index]);
                continue;
            }

            const std::size_t close = layout.find('}', index + 1);
            const std::string_view token = layout.substr(index + 1, close - index - 1);
            if(token == "/")
            {
                appendInstruction('E');
            }
            else if(token.starts_with('@'))
            {
                appendInstruction('S');
                compiled.push_back(layoutColorRoleCode(token.substr(1)));
            }
            else
            {
                const std::size_t separator = token.find(':');
                const std::string_view name = token.substr(0, separator);
                const char placeholder = layoutPlaceholderCode(name);
                if(separator == std::string_view::npos)
                {
                    appendInstruction(placeholder);
                }
                else
                {
                    appendInstruction('=');
                    compiled.push_back(placeholder);
                    const std::string_view spec = token.substr(separator + 1);
                    compiled.push_back(spec.front());
                    const std::string_view widthText = spec.substr(1);
                    std::uint16_t width = std::numeric_limits<std::uint16_t>::max();
                    if(widthText != "auto")
                    {
                        width = 0;
                        for(const char character : widthText)
                        {
                            width = static_cast<std::uint16_t>(width * 10u + static_cast<std::uint16_t>(character - '0'));
                        }
                    }
                    compiled.push_back(static_cast<char>(width & 0xFFu));
                    compiled.push_back(static_cast<char>(width >> 8u));
                }
            }
            index = close;
        }
        return compiled;
    }
    /// @endcond
} // namespace GameWIP::Logger::Detail
