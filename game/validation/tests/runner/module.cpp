/// @file module.cpp
/// @brief Registers validation-runner correctness tests.

#include "validation/tests/runner/runner_test.h"

#include "validation/tests/registry.h"

namespace
{
    /// @brief Maps shared runner report policy into the validation-runner self-test suite.
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::RunnerTestOptions options;
        options.verboseConsole = invocation.options.verboseConsole;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runRunnerTests(options);
    }

    /// @brief Process-local static registration for deterministic runner-module discovery.
    const GameWIP::Validation::Tests::Registration registration({
        .name = "runner",
        .order = 5,
        .run = run,
    });
} // namespace
