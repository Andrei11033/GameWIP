/// @file input_value_format.cpp
/// @brief Formats typed Input values for diagnostics.

#include "input_event_format.h"

#include <format>
#include <type_traits>

namespace
{
    /// ------------------------------------------------------------
    /// Numeric and vector formatting
    /// ------------------------------------------------------------

    template <typename T> [[nodiscard]] std::string formatNumber(T value)
    {
        if constexpr(std::is_floating_point_v<T>)
        {
            return std::format("{:.3f}", value);
        }
        else
        {
            return std::format("{}", value);
        }
    }

    template <typename T> [[nodiscard]] std::string formatVector2(const T &value)
    {
        return std::format("({}, {})", formatNumber(value.x), formatNumber(value.y));
    }

    template <typename T> [[nodiscard]] std::string formatVector3(const T &value)
    {
        return std::format("({}, {}, {})", formatNumber(value.x), formatNumber(value.y), formatNumber(value.z));
    }
} // namespace

namespace GameWIP::Game::RuntimeDiagnostics::InputEventFormat
{
    namespace Input = GameWIP::Input;

    /// ------------------------------------------------------------
    /// Typed value formatting
    /// ------------------------------------------------------------

    std::string value(const Input::Types::Value &inputValue)
    {
        return std::visit(
            [](const auto &item) -> std::string
            {
                using T = std::remove_cvref_t<decltype(item)>;
                if constexpr(std::is_same_v<T, std::monostate>)
                {
                    return "none";
                }
                else if constexpr(std::is_same_v<T, bool>)
                {
                    return item ? "true" : "false";
                }
                else if constexpr(std::is_arithmetic_v<T>)
                {
                    return formatNumber(item);
                }
                else if constexpr(requires {
                                      item.position;
                                      item.orientation;
                                      item.positionValid;
                                  })
                {
                    return std::format(
                        "position={} ({}), orientation={} ({})",
                        formatVector3(item.position),
                        item.positionValid ? "valid" : "unavailable",
                        std::format(
                            "({}, {}, {}, {})",
                            formatNumber(item.orientation.x),
                            formatNumber(item.orientation.y),
                            formatNumber(item.orientation.z),
                            formatNumber(item.orientation.w)),
                        item.orientationValid ? "valid" : "unavailable");
                }
                else if constexpr(requires { item.w; })
                {
                    return std::format("({}, {}, {}, {})", formatNumber(item.x), formatNumber(item.y), formatNumber(item.z), formatNumber(item.w));
                }
                else if constexpr(requires { item.z; })
                {
                    return formatVector3(item);
                }
                else if constexpr(requires {
                                      item.x;
                                      item.y;
                                  })
                {
                    return formatVector2(item);
                }
                else
                {
                    return "unknown";
                }
            },
            inputValue);
    }

} // namespace GameWIP::Game::RuntimeDiagnostics::InputEventFormat
