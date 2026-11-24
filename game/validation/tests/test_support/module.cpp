/// @file module.cpp
/// @brief Registers the TestSupport correctness-test module with the validation runner.

#include "validation/tests/test_support/test_support_test.h"

#include "validation/tests/registry.h"

namespace
{
    /// @brief Maps shared runner policy to TestSupport-specific options and executes the suite.
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::TestSupportTestOptions options;
        options.enableChildProcessTests = invocation.options.enableTestSupportChildProcessTests;
        options.enableStressTests = invocation.options.enableStressTests;
        options.enableManualTests = invocation.options.enableManualTests;
        options.verboseConsole = invocation.options.verboseConsole;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runTestSupportTests(invocation.argc, invocation.argv, options);
    }

    /// @brief Process-local static registration for deterministic TestSupport module discovery.
    const GameWIP::Validation::Tests::Registration kRegistration({
        .name = "test_support",
        .order = 40,
        .run = run,
        .handlesChildArguments = GameWIP::Test::Internal::hasTestSupportChildArguments,
    });
} // namespace
