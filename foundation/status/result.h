/// @file result.h
/// @brief Portable status-bearing results for fallible operations.

#pragma once

#include "status/status.h"

#include <exception>
#include <optional>
#include <type_traits>
#include <utility>

/// @brief Status-bearing values for fallible GameWIP operations.
namespace GameWIP::Status::Types
{
    /// @brief A portable status paired with an optional complete or partial value.
    /// @details A successful result always has a value. A failed result may keep useful partial progress.
    template <typename T> class [[nodiscard]] Result
    {
        static_assert(!std::is_void_v<T>, "Use Result<void> for operations without a value.");
        static_assert(!std::is_reference_v<T>, "Result values must own their value object.");

    public:
        /// @brief Creates a successful result containing value.
        [[nodiscard]] static Result success(T value)
        {
            return Result(Status(Code::Success), std::optional<T>(std::in_place, std::move(value)));
        }

        /// @brief Creates a failed result without a value.
        /// @pre status must not be successful; otherwise the process terminates.
        [[nodiscard]] static Result failure(Status status) noexcept
        {
            if(status.ok())
            {
                std::terminate();
            }

            return Result(std::move(status), std::nullopt);
        }

        /// @brief Creates a failed result that preserves useful partial progress.
        /// @pre status must not be successful; otherwise the process terminates.
        [[nodiscard]] static Result failure(Status status, T partialValue)
        {
            if(status.ok())
            {
                std::terminate();
            }

            return Result(std::move(status), std::optional<T>(std::in_place, std::move(partialValue)));
        }

        /// @brief Returns the exact operation outcome.
        [[nodiscard]] const Status &status() const noexcept
        {
            return status_;
        }

        /// @brief Reports success only when status is successful and value is present.
        [[nodiscard]] bool ok() const noexcept
        {
            return status_.ok() && value_.has_value();
        }

        /// @brief Reports whether a complete or partial value is available.
        [[nodiscard]] bool hasValue() const noexcept
        {
            return value_.has_value();
        }

        /// @pre hasValue() is true; otherwise the process terminates.
        [[nodiscard]] T &value() & noexcept
        {
            if(!value_)
            {
                std::terminate();
            }

            return *value_;
        }

        /// @pre hasValue() is true; otherwise the process terminates.
        [[nodiscard]] const T &value() const & noexcept
        {
            if(!value_)
            {
                std::terminate();
            }

            return *value_;
        }

        /// @pre hasValue() is true; otherwise the process terminates.
        [[nodiscard]] T &&value() && noexcept
        {
            if(!value_)
            {
                std::terminate();
            }

            return std::move(*value_);
        }

    private:
        Result(Status status, std::optional<T> value) noexcept(std::is_nothrow_move_constructible_v<T>)
            : status_(std::move(status))
            , value_(std::move(value))
        {
        }

        Result(Status status, std::nullopt_t) noexcept
            : status_(std::move(status))
        {
        }

        // Operation outcome and optional payload
        Status status_;
        std::optional<T> value_;
    };

    /// @brief Status-only result for a fallible operation with no payload.
    template <> class [[nodiscard]] Result<void>
    {
    public:
        /// @brief Creates a successful result.
        Result() noexcept = default;

        /// @brief Wraps the outcome of an operation with no payload.
        Result(Status status) noexcept
            : status_(std::move(status))
        {
        }

        /// @brief Creates a successful result.
        [[nodiscard]] static Result success() noexcept
        {
            return Result(Status(Code::Success));
        }

        /// @brief Creates a failed result.
        /// @pre status must not be successful; otherwise the process terminates.
        [[nodiscard]] static Result failure(Status status) noexcept
        {
            if(status.ok())
            {
                std::terminate();
            }

            return Result(std::move(status));
        }

        /// @brief Returns the exact operation outcome.
        [[nodiscard]] const Status &status() const noexcept
        {
            return status_;
        }

        /// @brief Reports whether the operation completed successfully.
        [[nodiscard]] bool ok() const noexcept
        {
            return status_.ok();
        }

    private:
        // Operation outcome
        Status status_;
    };
} // namespace GameWIP::Status::Types
