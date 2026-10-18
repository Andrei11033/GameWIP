/// @file result.h
/// @brief Status-plus-value result used by all-or-nothing Desktop queries.

#pragma once

#include "io/status.h"

#include <exception>
#include <optional>
#include <utility>

namespace GameWIP::Desktop::Types
{
    /// @brief A status paired with an optional value that is meaningful only when status is successful.
    /// @details Query failures never expose a default-looking payload. Callers should test ok() and
    /// then access value, or use value_or() when a fallback is explicitly intended.
    template <typename Value> struct Result
    {
        IO::Types::Status status;   ///< Success or the failure reason.
        std::optional<Value> value; ///< Engaged only on success.

        /// @brief Whether the query succeeded and value is engaged.
        [[nodiscard]] constexpr bool ok() const noexcept
        {
            return status.ok() && value.has_value();
        }

        /// @pre ok() is true; otherwise the process terminates.
        [[nodiscard]] const Value &operator*() const & noexcept
        {
            if (!value)
            {
                std::terminate();
            }
            return *value;
        }

        /// @pre ok() is true; otherwise the process terminates.
        [[nodiscard]] Value &operator*() & noexcept
        {
            if (!value)
            {
                std::terminate();
            }
            return *value;
        }

        /// @pre ok() is true; otherwise behavior is undefined.
        [[nodiscard]] const Value *operator->() const noexcept
        {
            return value ? value.operator->() : nullptr;
        }

        /// @pre ok() is true; otherwise behavior is undefined.
        [[nodiscard]] Value *operator->() noexcept
        {
            return value ? value.operator->() : nullptr;
        }

        /// @brief Returns the value on success, otherwise @p fallback.
        [[nodiscard]] Value value_or(Value fallback) const
        {
            return value ? *value : std::move(fallback);
        }
    };
} // namespace GameWIP::Desktop::Types
