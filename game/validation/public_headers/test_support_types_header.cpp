/// @file test_support_types_header.cpp
/// @brief TestSupport shared-types public-header self-containment compile check.

#include "test_support/types.h"

#include <cstdint>

static_assert(sizeof(GameWIP::TestSupport::Types::InfrastructureError) == sizeof(std::uint8_t));
static_assert(GameWIP::TestSupport::Types::InfrastructureStatus{}.ok());
static_assert(noexcept(GameWIP::TestSupport::Types::InfrastructureStatus{}.ok()));
static_assert(GameWIP::TestSupport::Types::InfrastructureError::EncodingFailed != GameWIP::TestSupport::Types::InfrastructureError::None);
