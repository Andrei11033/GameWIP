/// @file value.cpp
/// @brief Implements private value helpers for the portable Input system.

#include "input/internal/value.h"

#include <cmath>
#include <limits>
#include <type_traits>
#include <variant>

namespace GameWIP::Input::Internal
{
    namespace
    {
        template <typename T> [[nodiscard]] bool addIntegral(T &current, T delta) noexcept
        {
            if constexpr(std::is_signed_v<T>)
            {
                if((delta > 0 && current > std::numeric_limits<T>::max() - delta) ||
                   (delta < 0 && current < std::numeric_limits<T>::lowest() - delta))
                {
                    return false;
                }
            }
            else if(current > std::numeric_limits<T>::max() - delta)
            {
                return false;
            }
            current += delta;
            return true;
        }

        template <typename T> [[nodiscard]] bool addFloating(T &current, T delta) noexcept
        {
            const T result = current + delta;

            if(!std::isfinite(result))
            {
                return false;
            }

            current = result;
            return true;
        }

        template <typename T> [[nodiscard]] bool addVector2(Types::Vector2<T> &current, const Types::Vector2<T> &delta) noexcept
        {
            T x = current.x;
            T y = current.y;

            const bool xAdded = [&]
            {
                if constexpr(std::is_floating_point_v<T>)
                {
                    return addFloating(x, delta.x);
                }
                else
                {
                    return addIntegral(x, delta.x);
                }
            }();

            const bool yAdded = [&]
            {
                if constexpr(std::is_floating_point_v<T>)
                {
                    return addFloating(y, delta.y);
                }
                else
                {
                    return addIntegral(y, delta.y);
                }
            }();

            if(!xAdded || !yAdded)
            {
                return false;
            }

            current = {x, y};
            return true;
        }

        template <typename T> [[nodiscard]] bool addVector3(Types::Vector3<T> &current, const Types::Vector3<T> &delta) noexcept
        {
            T x = current.x;
            T y = current.y;
            T z = current.z;

            const bool xAdded = [&]
            {
                if constexpr(std::is_floating_point_v<T>)
                {
                    return addFloating(x, delta.x);
                }
                else
                {
                    return addIntegral(x, delta.x);
                }
            }();

            const bool yAdded = [&]
            {
                if constexpr(std::is_floating_point_v<T>)
                {
                    return addFloating(y, delta.y);
                }
                else
                {
                    return addIntegral(y, delta.y);
                }
            }();

            const bool zAdded = [&]
            {
                if constexpr(std::is_floating_point_v<T>)
                {
                    return addFloating(z, delta.z);
                }
                else
                {
                    return addIntegral(z, delta.z);
                }
            }();

            if(!xAdded || !yAdded || !zAdded)
            {
                return false;
            }

            current = {x, y, z};
            return true;
        }
    } // namespace

    Types::ValueKind valueKind(const Types::Value &value) noexcept
    {
        return static_cast<Types::ValueKind>(value.index());
    }

    Types::Value zeroValue(Types::ValueKind kind) noexcept
    {
        switch(kind)
        {
        case Types::ValueKind::None:
            return std::monostate{};
        case Types::ValueKind::Boolean:
            return false;
        case Types::ValueKind::SignedInteger:
            return std::int64_t{0};
        case Types::ValueKind::UnsignedInteger:
            return std::uint64_t{0};
        case Types::ValueKind::Float32:
            return 0.0F;
        case Types::ValueKind::Float64:
            return 0.0;
        case Types::ValueKind::Vector2I64:
            return Types::Vector2I64{};
        case Types::ValueKind::Vector2U64:
            return Types::Vector2U64{};
        case Types::ValueKind::Vector2F32:
            return Types::Vector2F32{};
        case Types::ValueKind::Vector2F64:
            return Types::Vector2F64{};
        case Types::ValueKind::Vector3I64:
            return Types::Vector3I64{};
        case Types::ValueKind::Vector3U64:
            return Types::Vector3U64{};
        case Types::ValueKind::Vector3F32:
            return Types::Vector3F32{};
        case Types::ValueKind::Vector3F64:
            return Types::Vector3F64{};
        case Types::ValueKind::QuaternionF32:
            return Types::QuaternionF32{};
        case Types::ValueKind::QuaternionF64:
            return Types::QuaternionF64{};
        case Types::ValueKind::Spatial:
            return Types::Spatial{};
        }

        return std::monostate{};
    }

    bool hasValueKind(const Types::Value &value, Types::ValueKind kind) noexcept
    {
        return valueKind(value) == kind;
    }

    std::optional<Types::Scalar> component(const Types::Value &value, std::size_t index) noexcept
    {
        return std::visit(
            [index](const auto &current) -> std::optional<Types::Scalar>
            {
                using T = std::remove_cvref_t<decltype(current)>;
                if constexpr(
                    std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t> || std::is_same_v<T, float> || std::is_same_v<T, double>)
                {
                    if(index == 0)
                    {
                        return std::optional<Types::Scalar>{Types::Scalar{current}};
                    }

                    return std::nullopt;
                }
                else if constexpr(
                    std::is_same_v<T, Types::Vector2I64> || std::is_same_v<T, Types::Vector2U64> || std::is_same_v<T, Types::Vector2F32> ||
                    std::is_same_v<T, Types::Vector2F64>)
                {
                    if(index == 0)
                    {
                        return Types::Scalar{current.x};
                    }

                    if(index == 1)
                    {
                        return Types::Scalar{current.y};
                    }

                    return std::nullopt;
                }
                else if constexpr(
                    std::is_same_v<T, Types::Vector3I64> || std::is_same_v<T, Types::Vector3U64> || std::is_same_v<T, Types::Vector3F32> ||
                    std::is_same_v<T, Types::Vector3F64>)
                {
                    if(index == 0)
                    {
                        return Types::Scalar{current.x};
                    }

                    if(index == 1)
                    {
                        return Types::Scalar{current.y};
                    }

                    if(index == 2)
                    {
                        return Types::Scalar{current.z};
                    }

                    return std::nullopt;
                }
                else if constexpr(std::is_same_v<T, Types::QuaternionF32> || std::is_same_v<T, Types::QuaternionF64>)
                {
                    if(index == 0)
                    {
                        return Types::Scalar{current.x};
                    }

                    if(index == 1)
                    {
                        return Types::Scalar{current.y};
                    }

                    if(index == 2)
                    {
                        return Types::Scalar{current.z};
                    }

                    if(index == 3)
                    {
                        return Types::Scalar{current.w};
                    }
                    return std::nullopt;
                }
                else if constexpr(std::is_same_v<T, Types::Spatial>)
                {
                    if(index < 3)
                    {
                        if(!current.positionValid)
                        {
                            return std::nullopt;
                        }

                        if(index == 0)
                        {
                            return Types::Scalar{current.position.x};
                        }

                        if(index == 1)
                        {
                            return Types::Scalar{current.position.y};
                        }
                        return Types::Scalar{current.position.z};
                    }

                    if(index < 7)
                    {
                        if(!current.orientationValid)
                        {
                            return std::nullopt;
                        }
                        const std::size_t orientationIndex = index - 3;
                        if(orientationIndex == 0)
                        {
                            return Types::Scalar{current.orientation.x};
                        }

                        if(orientationIndex == 1)
                        {
                            return Types::Scalar{current.orientation.y};
                        }

                        if(orientationIndex == 2)
                        {
                            return Types::Scalar{current.orientation.z};
                        }
                        return Types::Scalar{current.orientation.w};
                    }
                    return std::nullopt;
                }
                else
                {
                    return std::nullopt;
                }
            },
            value);
    }

    bool addRelative(Types::Value &current, const Types::Value &delta) noexcept
    {
        if(valueKind(current) != valueKind(delta))
        {
            return false;
        }

        return std::visit(
            [](auto &left, const auto &right) -> bool
            {
                using Left = std::remove_cvref_t<decltype(left)>;
                using Right = std::remove_cvref_t<decltype(right)>;
                if constexpr(std::is_same_v<Left, Right> && std::is_integral_v<Left> && !std::is_same_v<Left, bool>)
                {
                    return addIntegral(left, right);
                }
                else if constexpr(std::is_same_v<Left, Right> && std::is_floating_point_v<Left>)
                {
                    return addFloating(left, right);
                }
                else if constexpr(
                    std::is_same_v<Left, Right> && (std::is_same_v<Left, Types::Vector3I64> || std::is_same_v<Left, Types::Vector3U64> ||
                                                    std::is_same_v<Left, Types::Vector3F32> || std::is_same_v<Left, Types::Vector3F64>))
                {
                    return addVector3(left, right);
                }
                else if constexpr(
                    std::is_same_v<Left, Right> && (std::is_same_v<Left, Types::Vector2I64> || std::is_same_v<Left, Types::Vector2U64> ||
                                                    std::is_same_v<Left, Types::Vector2F32> || std::is_same_v<Left, Types::Vector2F64>))
                {
                    return addVector2(left, right);
                }
                else
                {
                    return false;
                }
            },
            current,
            delta);
    }
} // namespace GameWIP::Input::Internal
