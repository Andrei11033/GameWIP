/// @file test_support_types.cpp
/// @brief Installed TestSupport shared-types header check.

#include "test_support/types.h"

static_assert(GameWIP::TestSupport::Types::InfrastructureStatus{}.ok());
static_assert(GameWIP::TestSupport::Types::InfrastructureError::EncodingFailed != GameWIP::TestSupport::Types::InfrastructureError::None);
