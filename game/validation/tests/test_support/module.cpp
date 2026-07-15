/// @file module.cpp
/// @brief Registers the TestSupport correctness-test module with the validation runner.

#include "validation/tests/test_support/test_support_test.h"

#include "validation/tests/registry.h"

#include <string_view>

namespace
{
    /// @brief Claims the TestSupport child prefix for routing before validating its exact protocol suffix.
    /// @note An unknown reserved suffix currently reaches the TestSupport module and can fall through to its full suite.
    bool handlesChildArguments(int argc, char **argv)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] != nullptr && std::string_view(argv[index]).starts_with("--test-support-test-child="))
            {
                return true;
            }
        }
        return false;
    }

    /// @brief Maps shared runner policy to TestSupport-specific options and executes the suite.
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::TestSupportTestOptions options;
        options.enableChildProcessTests = invocation.options.enableTestSupportChildProcessTests;
        options.enableStressTests = invocation.options.enableStressTests;
        options.enableManualTests = invocation.options.enableManualUiTests;
        options.verboseConsole = invocation.options.verboseConsole;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runTestSupportTests(invocation.argc, invocation.argv, options);
    }

    /// @brief Process-local static registration for deterministic TestSupport module discovery.
    const GameWIP::Validation::Tests::Registration registration({
        .name = "test_support",
        .order = 40,
        .run = run,
        .handlesChildArguments = handlesChildArguments,
    });
} // namespace
