/// @file test_support_types_header.cpp
/// @brief TestSupport shared-types public-header self-containment compile check.

#include "test_support/types.h"

#include <cstdint>

static_assert(sizeof(GameWIP::Status::Types::Code) == sizeof(std::uint8_t));
static_assert(noexcept(GameWIP::TestSupport::Types::Status{}.ok()));
static_assert(GameWIP::Status::Types::Code::EncodingFailed != GameWIP::Status::Types::Code::Success);
