/// @file message_format.h
/// @brief Explicit colors and semantic roles for formatted Logger arguments.

#pragma once

#include "logger/config.h"
#include "logger/detail/formatting.h"

#include <format>
#include <type_traits>
#include <utility>

namespace GameWIP::Logger
{
    /// @cond INTERNAL
    namespace Detail::Core
    {
        enum class TextRole : std::uint8_t
        {
            Code,
            Path,
            Number,
            Quoted
        };

        template <typename Value> struct ColoredValue
        {
            Value value;
            Color color;
        };

        template <typename Value> struct RoleValue
        {
            Value value;
            TextRole role;
        };
    } // namespace Detail::Core
    /// @endcond

    /// @brief Colors one formatted argument with an explicit per-call color.
    template <typename Value> [[nodiscard]] auto color(Value &&value, Color selectedColor)
    {
        return Detail::Core::ColoredValue<std::decay_t<Value>>{std::forward<Value>(value), selectedColor};
    }

    /// @brief Marks one formatted argument with the theme's code color.
    template <typename Value> [[nodiscard]] auto code(Value &&value)
    {
        return Detail::Core::RoleValue<std::decay_t<Value>>{std::forward<Value>(value), Detail::Core::TextRole::Code};
    }

    /// @brief Marks one formatted argument with the theme's path color.
    template <typename Value> [[nodiscard]] auto path(Value &&value)
    {
        return Detail::Core::RoleValue<std::decay_t<Value>>{std::forward<Value>(value), Detail::Core::TextRole::Path};
    }

    /// @brief Marks one formatted argument with the theme's number color.
    template <typename Value> [[nodiscard]] auto number(Value &&value)
    {
        return Detail::Core::RoleValue<std::decay_t<Value>>{std::forward<Value>(value), Detail::Core::TextRole::Number};
    }

    /// @brief Marks one formatted argument with the theme's quoted-text color.
    template <typename Value> [[nodiscard]] auto quoted(Value &&value)
    {
        return Detail::Core::RoleValue<std::decay_t<Value>>{std::forward<Value>(value), Detail::Core::TextRole::Quoted};
    }

    /// @brief Marks a format string as runtime-provided for Logger formatting overloads.
    constexpr Types::RuntimeFormat runtimeFormat(std::string_view format) noexcept
    {
        return {format};
    }
} // namespace GameWIP::Logger

/// @cond INTERNAL
template <typename Value> struct std::formatter<GameWIP::Logger::Detail::Core::ColoredValue<Value>, char> : std::formatter<Value, char>
{
    template <typename Context> auto format(const GameWIP::Logger::Detail::Core::ColoredValue<Value> &wrapped, Context &context) const
    {
        auto output = context.out();
        *output++ = GameWIP::Logger::Detail::Core::Protocol::kStyleBegin;
        *output++ = 'X';
        *output++ = static_cast<char>(wrapped.color.kind);
        *output++ = static_cast<char>(wrapped.color.basic);
        *output++ = static_cast<char>(wrapped.color.red);
        *output++ = static_cast<char>(wrapped.color.green);
        *output++ = static_cast<char>(wrapped.color.blue);

        output = std::formatter<Value, char>::format(wrapped.value, context);
        *output++ = GameWIP::Logger::Detail::Core::Protocol::kStyleEnd;
        return output;
    }
};

template <typename Value> struct std::formatter<GameWIP::Logger::Detail::Core::RoleValue<Value>, char> : std::formatter<Value, char>
{
    template <typename Context> auto format(const GameWIP::Logger::Detail::Core::RoleValue<Value> &wrapped, Context &context) const
    {
        auto output = context.out();
        *output++ = GameWIP::Logger::Detail::Core::Protocol::kStyleBegin;
        switch(wrapped.role)
        {
        case GameWIP::Logger::Detail::Core::TextRole::Code:
            *output++ = 'C';
            break;
        case GameWIP::Logger::Detail::Core::TextRole::Path:
            *output++ = 'P';
            break;
        case GameWIP::Logger::Detail::Core::TextRole::Number:
            *output++ = 'N';
            break;
        case GameWIP::Logger::Detail::Core::TextRole::Quoted:
            *output++ = 'Q';
            break;
        }

        output = std::formatter<Value, char>::format(wrapped.value, context);
        *output++ = GameWIP::Logger::Detail::Core::Protocol::kStyleEnd;
        return output;
    }
};
/// @endcond
