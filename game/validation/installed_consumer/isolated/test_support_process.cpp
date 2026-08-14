/// @file test_support_process.cpp
/// @brief Installed TestSupport process header check.

#include "test_support/process.h"

#include <type_traits>

static_assert(std::is_default_constructible_v<GameWIP::TestSupport::Types::Process::Result>);
