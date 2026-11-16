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

    void testWin32ReportPaths(TestSupport::Context &context, Probe runProbe, MakeOptions makeOptions, ReportOutputEnabled reportOutputEnabled);
} // namespace GameWIP::Test::RunnerPlatform
