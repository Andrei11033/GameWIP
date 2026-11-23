/// @file status.cpp
/// @brief Shared private infrastructure-status helpers for TestSupport.

#include "test_support/internal/status.h"

namespace GameWIP::TestSupport::Internal
{
    /// ------------------------------------------------------------
    /// Infrastructure status construction
    /// ------------------------------------------------------------

    Types::InfrastructureStatus failureStatus(Types::InfrastructureError error, std::uint64_t code) noexcept
    {
        return Types::InfrastructureStatus{.error = error, .nativeCode = code};
    }

    std::uint64_t nativeCode(const std::error_code &error) noexcept
    {
        return static_cast<std::uint64_t>(static_cast<std::uint32_t>(error.value()));
    }
} // namespace GameWIP::TestSupport::Internal
