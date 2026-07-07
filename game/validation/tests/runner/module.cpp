/// @file module.cpp
/// @brief Registers validation-runner correctness tests.

#include "validation/tests/runner/runner_test.h"

#include "validation/tests/registry.h"

namespace
{
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::RunnerTestOptions options;
        options.verboseConsole = invocation.options.verboseConsole;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runRunnerTests(options);
    }

    const GameWIP::Validation::Tests::Registration registration({
        .name = "runner",
        .order = 5,
        .run = run,
    });
} // namespace
