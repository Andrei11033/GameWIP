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

    /// @brief Signals the event shared with a child-process test.
    void signalChildEvent(std::uintptr_t eventHandle) noexcept;

    /// @brief Runs one child mode that creates a descendant process.
    [[nodiscard]] int runDescendantChild(const std::filesystem::path &executablePath, std::string_view childArgument);

    /// ------------------------------------------------------------
    /// Native process checks
    /// ------------------------------------------------------------

    /// @brief Verifies a child launch does not inherit handles unless requested.
    void testHandleInheritance(TestSupport::Context &context, const std::filesystem::path &executablePath, std::string_view childArgument);

    /// @brief Verifies child process exit-code boundaries on the native backend.
    void testExitCodeBoundaries(TestSupport::Context &context, const std::filesystem::path &executablePath, std::string_view childArgument);
} // namespace GameWIP::Test::TestSupportPlatform
