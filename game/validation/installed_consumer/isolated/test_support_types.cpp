/// @file test_support_types.cpp
/// @brief Installed TestSupport shared-types header check.

#include "test_support/types.h"

#include <cstdint>

static_assert(sizeof(GameWIP::Status::Types::Code) == sizeof(std::uint8_t));
static_assert(GameWIP::Status::Types::Code::EncodingFailed != GameWIP::Status::Types::Code::Success);
static_assert(noexcept(GameWIP::TestSupport::Types::Status{}.ok()));
