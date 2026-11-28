/// @file results.h
/// @brief Small helpers for building Input operation results.
#pragma once

#include "status/result.h"

#include <utility>

namespace GameWIP::Input::Internal
{
    // Status-only operation results

    [[nodiscard]] inline GameWIP::Status::Types::Result<void> result(GameWIP::Status::Types::Status status) noexcept
    {
        return status.ok() ? GameWIP::Status::Types::Result<void>::success() : GameWIP::Status::Types::Result<void>::failure(std::move(status));
    }

    // Failed operations with typed results

    template <typename T> [[nodiscard]] GameWIP::Status::Types::Result<T> failedResult(GameWIP::Status::Types::Status status) noexcept
    {
        return GameWIP::Status::Types::Result<T>::failure(std::move(status));
    }
} // namespace GameWIP::Input::Internal
