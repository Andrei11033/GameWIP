/// @file test_support_stress.cpp
/// @brief Installed TestSupport stress header check.

#include "test_support/stress.h"

#include <utility>

static_assert(noexcept(std::declval<GameWIP::TestSupport::StopFlag &>().requestStop()));
