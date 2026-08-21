/// @file test_support_files.cpp
/// @brief Installed TestSupport files header check.

#include "test_support/files.h"

#include <utility>

#include <filesystem>
#include <string_view>

static_assert(noexcept(GameWIP::TestSupport::readTextFile(std::declval<const std::filesystem::path &>())));
static_assert(noexcept(GameWIP::TestSupport::writeTextFile(std::declval<const std::filesystem::path &>(), std::declval<std::string_view>())));
