/// @file platform.h
/// @brief Platform hooks for Terminal validation.

#pragma once

#include <string_view>

namespace GameWIP::TestSupport
{
    class Runner;
}

namespace GameWIP::Test::TerminalPlatform
{
    /// ------------------------------------------------------------
    /// Native validation
    /// ------------------------------------------------------------

    inline constexpr std::string_view kCancellationSignalFailureChildArgument = "--terminal-test-child=cancellation-signal-failure";

    /// @brief Returns whether one argument selects the cancellation-signal failure child.
    [[nodiscard]] bool handlesCancellationSignalFailureChild(std::string_view argument) noexcept;

    /// @brief Registers native Terminal cases with the shared runner.
    void registerWin32Suites(TestSupport::Runner &runner);

    /// @brief Runs the isolated cancellation-signal failure child protocol.
    [[nodiscard]] int runCancellationSignalFailureChild();
} // namespace GameWIP::Test::TerminalPlatform
