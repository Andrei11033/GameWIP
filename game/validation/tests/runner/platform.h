/// @file platform.h
/// @brief Platform test hooks for the validation runner.

#pragma once

#include "validation/tests/registry.h"

namespace GameWIP::TestSupport
{
    class Context;
}

namespace GameWIP::Test::RunnerPlatform
{
    /// ------------------------------------------------------------
    /// Report path validation
    /// ------------------------------------------------------------

    using Probe = GameWIP::Validation::TestResult (*)(GameWIP::Validation::Tests::RunOptions options);
    using MakeOptions = GameWIP::Validation::Tests::RunOptions (*)();
    using ReportOutputEnabled = bool (*)();

#if defined(_WIN32)
    /// @brief Verifies Win32 rooted and drive-relative report paths are rejected.
    /// @param context Records expectations in the runner suite.
    /// @param runProbe Executes the runner with the supplied options.
    /// @param makeOptions Creates baseline runner options for each case.
    /// @param reportOutputEnabled Reports whether the last invocation kept file output enabled.
    void testWin32ReportPaths(TestSupport::Context &context, Probe runProbe, MakeOptions makeOptions, ReportOutputEnabled reportOutputEnabled);
#endif
} // namespace GameWIP::Test::RunnerPlatform
