/// @file test_support_files_header.cpp
/// @brief TestSupport files public-header self-containment compile check.

#include "test_support/files.h"

#include <filesystem>
#include <string_view>
#include <type_traits>
#include <utility>

static_assert(noexcept(GameWIP::TestSupport::readTextFile(std::declval<const std::filesystem::path &>())));
static_assert(noexcept(GameWIP::TestSupport::writeTextFile(std::declval<const std::filesystem::path &>(), std::declval<std::string_view>())));
static_assert(std::is_nothrow_destructible_v<GameWIP::TestSupport::ScopedTemporaryDirectory>);
