/// @file status.h
/// @brief Shared private infrastructure-status helpers for TestSupport.

#pragma once

#include "status/status.h"

#include <cstdint>
#include <optional>
#include <system_error>

namespace GameWIP::TestSupport::Internal
{
    /// ------------------------------------------------------------
    /// Infrastructure status construction
    /// ------------------------------------------------------------

    [[nodiscard]] GameWIP::Status::Types::Status failureStatus(
        GameWIP::Status::Types::Code code,
        std::optional<GameWIP::Status::Types::NativeCode> nativeCode = std::nullopt) noexcept;
    [[nodiscard]] std::optional<GameWIP::Status::Types::NativeCode> nativeCode(const std::error_code &error) noexcept;
    [[nodiscard]] std::optional<GameWIP::Status::Types::NativeCode> errnoCode(int value) noexcept;
} // namespace GameWIP::TestSupport::Internal
