/// @file result.h
/// @brief Status-plus-value result used by all-or-nothing Desktop queries.

#pragma once

#include "io/status.h"

#include <optional>
#include <utility>

namespace GameWIP::Desktop::Types
{
    /// @brief A status paired with an optional value that is meaningful only when status is successful.
    /// @details Query failures never expose a default-looking payload. Callers should test ok() and
    /// then access value, or use value_or() when a fallback is explicitly intended.
    template <typename Value> struct Result
    {
        IO::Types::Status status;
        std::optional<Value> value;

        [[nodiscard]] constexpr bool ok() const noexcept
        {
            return status.ok() && value.has_value();
        }

        [[nodiscard]] const Value &operator*() const & noexcept
        {
            return *value;
        }

        [[nodiscard]] Value &operator*() & noexcept
        {
            return *value;
        }

        [[nodiscard]] const Value *operator->() const noexcept
        {
            return &*value;
        }

        [[nodiscard]] Value *operator->() noexcept
        {
            return &*value;
        }

        [[nodiscard]] Value value_or(Value fallback) const
        {
            return value ? *value : std::move(fallback);
        }
    };
} // namespace GameWIP::Desktop::Types
