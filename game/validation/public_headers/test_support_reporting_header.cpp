/// @file test_support_reporting_header.cpp
/// @brief TestSupport reporting public-header self-containment compile check.

#include "test_support/reporting.h"

#include <type_traits>
#include <utility>

static_assert(std::is_default_constructible_v<GameWIP::TestSupport::Types::Reporting::Options>);
static_assert(noexcept(std::declval<const GameWIP::TestSupport::Types::Reporting::Summary &>().ok()));
