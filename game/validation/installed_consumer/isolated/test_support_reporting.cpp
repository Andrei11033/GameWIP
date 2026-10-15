/// @file test_support_reporting.cpp
/// @brief Installed TestSupport reporting header check.

#include "test_support/reporting.h"

#include <type_traits>

static_assert(std::is_default_constructible_v<GameWIP::TestSupport::Types::Reporting::Options>);
static_assert(std::is_default_constructible_v<GameWIP::TestSupport::Types::Reporting::SinkStatus>);
static_assert(
    std::is_same_v<decltype(std::declval<GameWIP::TestSupport::Runner &>().reportStatus()), GameWIP::TestSupport::Types::Reporting::SinkStatus>);
static_assert(
    std::is_same_v<decltype(std::declval<GameWIP::TestSupport::Context &>().finalize()), GameWIP::TestSupport::Types::Reporting::SinkStatus>);
