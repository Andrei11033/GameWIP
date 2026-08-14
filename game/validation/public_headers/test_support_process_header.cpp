/// @file test_support_process_header.cpp
/// @brief TestSupport process public-header self-containment compile check.

#include "test_support/process.h"

#include <cstdint>
#include <type_traits>
#include <utility>

static_assert(sizeof(GameWIP::TestSupport::Types::Process::Outcome) == sizeof(std::uint8_t));
static_assert(std::is_default_constructible_v<GameWIP::TestSupport::Types::Process::Result>);
static_assert(noexcept(GameWIP::TestSupport::runChildProcess(std::declval<const GameWIP::TestSupport::Types::Process::Options &>())));
static_assert(std::is_nothrow_destructible_v<GameWIP::TestSupport::Types::Process::Result>);
