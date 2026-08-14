/// @file test_support_stress_header.cpp
/// @brief TestSupport stress public-header self-containment compile check.

#include "test_support/stress.h"

#include <utility>

static_assert(noexcept(std::declval<GameWIP::TestSupport::StopFlag &>().requestStop()));
static_assert(noexcept(std::declval<const GameWIP::TestSupport::StopFlag &>().stopRequested()));
