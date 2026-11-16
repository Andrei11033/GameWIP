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

    [[nodiscard]] bool handlesCancellationSignalFailureChild(std::string_view argument) noexcept;
    void registerWin32Suites(TestSupport::Runner &runner);
    [[nodiscard]] int runCancellationSignalFailureChild();
} // namespace GameWIP::Test::TerminalPlatform
