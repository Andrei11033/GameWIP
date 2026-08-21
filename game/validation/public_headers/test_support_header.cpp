/// @file test_support_header.cpp
/// @brief TestSupport umbrella public-header self-containment compile check.
///
/// This translation unit intentionally includes only `test_support/test_support.h` first. This proves
/// the installed public header can be parsed without relying on include order
/// from another GameWIP header.

#include "test_support/test_support.h"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <type_traits>
#include <utility>

static_assert(sizeof(GameWIP::TestSupport::Types::InfrastructureError) == sizeof(std::uint8_t));
static_assert(sizeof(GameWIP::TestSupport::Types::Process::Outcome) == sizeof(std::uint8_t));
static_assert(GameWIP::TestSupport::Types::InfrastructureStatus{}.ok());
static_assert(noexcept(GameWIP::TestSupport::Types::InfrastructureStatus{}.ok()));
static_assert(noexcept(GameWIP::TestSupport::readTextFile(std::declval<const std::filesystem::path &>())));
static_assert(noexcept(GameWIP::TestSupport::writeTextFile(std::declval<const std::filesystem::path &>(), std::declval<std::string_view>())));
static_assert(noexcept(GameWIP::TestSupport::fileExists(std::declval<const std::filesystem::path &>())));
static_assert(noexcept(GameWIP::TestSupport::fileContains(std::declval<const std::filesystem::path &>(), std::declval<std::string_view>())));
static_assert(noexcept(GameWIP::TestSupport::countFileOccurrences(std::declval<const std::filesystem::path &>(), std::declval<std::string_view>())));
static_assert(noexcept(GameWIP::TestSupport::createDirectories(std::declval<const std::filesystem::path &>())));
static_assert(noexcept(GameWIP::TestSupport::removeIfExists(std::declval<const std::filesystem::path &>())));
static_assert(noexcept(GameWIP::TestSupport::runChildProcess(std::declval<const GameWIP::TestSupport::Types::Process::Options &>())));
static_assert(std::is_nothrow_destructible_v<GameWIP::TestSupport::Types::Process::Result>);
static_assert(std::is_nothrow_constructible_v<GameWIP::TestSupport::ScopedTemporaryDirectory, std::string_view>);
static_assert(std::is_nothrow_destructible_v<GameWIP::TestSupport::ScopedTemporaryDirectory>);
static_assert(std::is_nothrow_constructible_v<GameWIP::TestSupport::ScopedCurrentPath, const std::filesystem::path &>);
static_assert(std::is_nothrow_destructible_v<GameWIP::TestSupport::ScopedCurrentPath>);
static_assert(std::is_nothrow_constructible_v<GameWIP::TestSupport::ScopedEnvironmentVariable, std::string_view, std::string_view>);
static_assert(std::is_nothrow_destructible_v<GameWIP::TestSupport::ScopedEnvironmentVariable>);
static_assert(std::is_nothrow_constructible_v<GameWIP::TestSupport::ScopedUnsetEnvironmentVariable, std::string_view>);
static_assert(std::is_nothrow_destructible_v<GameWIP::TestSupport::ScopedUnsetEnvironmentVariable>);
