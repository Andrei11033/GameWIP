/// @file platform.h
/// @brief Platform hooks for TestSupport validation.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace GameWIP::TestSupport
{
    class Context;
}

namespace GameWIP::Test::TestSupportPlatform
{
    /// ------------------------------------------------------------
    /// Child process helpers
    /// ------------------------------------------------------------

    void signalChildEvent(std::uintptr_t eventHandle) noexcept;
    [[nodiscard]] int runDescendantChild(const std::filesystem::path &executablePath, std::string_view childArgument);

    /// ------------------------------------------------------------
    /// Native process checks
    /// ------------------------------------------------------------

    void testHandleInheritance(TestSupport::Context &context, const std::filesystem::path &executablePath, std::string_view childArgument);
    void testExitCodeBoundaries(TestSupport::Context &context, const std::filesystem::path &executablePath, std::string_view childArgument);
} // namespace GameWIP::Test::TestSupportPlatform
