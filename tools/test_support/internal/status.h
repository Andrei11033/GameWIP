/// @file status.h
/// @brief Shared private infrastructure-status helpers for TestSupport.

#pragma once

#include "test_support/types.h"

#include <cstdint>
#include <system_error>

namespace GameWIP::TestSupport::Internal
{
    /// ------------------------------------------------------------
    /// Infrastructure status construction
    /// ------------------------------------------------------------

    [[nodiscard]] Types::InfrastructureStatus failureStatus(Types::InfrastructureError error, std::uint64_t code = 0) noexcept;
    [[nodiscard]] std::uint64_t nativeCode(const std::error_code &error) noexcept;
} // namespace GameWIP::TestSupport::Internal
