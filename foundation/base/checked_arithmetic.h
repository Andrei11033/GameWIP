/// @file checked_arithmetic.h
/// @brief Narrow checked-arithmetic predicates shared by internal GameWIP implementations.

#pragma once

#include <concepts>
#include <limits>

namespace GameWIP::Base
{
    /// @brief Returns whether adding two unsigned integral values would exceed their type's maximum.
    /// @tparam Value Unsigned integral type shared by both operands.
    /// @param left Left operand of the proposed addition.
    /// @param right Right operand of the proposed addition.
    /// @return true when `left + right` is not representable as Value; otherwise false.
    template <std::unsigned_integral Value> [[nodiscard]] constexpr bool wouldAddOverflow(Value left, Value right) noexcept
    {
        return right > (std::numeric_limits<Value>::max)() - left;
    }

    /// @brief Returns whether multiplying two unsigned integral values would exceed their type's maximum.
    /// @tparam Value Unsigned integral type shared by both operands.
    /// @param left Left operand of the proposed multiplication.
    /// @param right Right operand of the proposed multiplication.
    /// @return true when `left * right` is not representable as Value; otherwise false.
    template <std::unsigned_integral Value> [[nodiscard]] constexpr bool wouldMultiplyOverflow(Value left, Value right) noexcept
    {
        return left != 0 && right > (std::numeric_limits<Value>::max)() / left;
    }
} // namespace GameWIP::Base
