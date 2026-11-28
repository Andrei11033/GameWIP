/// @file platform.h
/// @brief Platform test hooks for the Input suite.

#pragma once

namespace GameWIP::TestSupport
{
    class Runner;
}

namespace GameWIP::Test::InputPlatform
{
    /// ------------------------------------------------------------
    /// Native validation
    /// ------------------------------------------------------------

    /// @brief Registers platform-specific Input cases with the shared runner.
    void registerSuites(TestSupport::Runner &runner);
} // namespace GameWIP::Test::InputPlatform
