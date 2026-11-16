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

    void registerSuites(TestSupport::Runner &runner);
} // namespace GameWIP::Test::InputPlatform
