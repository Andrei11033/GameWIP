/// @file platform.h
/// @brief Platform test hooks for the Base suite.

#pragma once

namespace GameWIP::TestSupport
{
    class Runner;
}

namespace GameWIP::Test::BasePlatform
{
    /// ------------------------------------------------------------
    /// Native validation
    /// ------------------------------------------------------------

    void registerSuites(TestSupport::Runner &runner);
} // namespace GameWIP::Test::BasePlatform
