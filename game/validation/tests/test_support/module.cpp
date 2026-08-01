/// @file module.cpp
/// @brief Registers the TestSupport correctness-test module with the validation runner.

#include "validation/tests/test_support/test_support_test.h"

#include "validation/tests/registry.h"

#include <algorithm>
#include <string_view>

namespace
{
    /// @brief Claims only exact TestSupport child protocols recognized by the suite.
    bool handlesChildArguments(int argc, char **argv)
    {
        constexpr std::string_view selectors[] = {
            "--test-support-test-child=environment",
            "--test-support-test-child=echo",
            "--test-support-test-child=sleep",
            "--test-support-test-child=exit-code",
            "--test-support-test-child=output",
            "--test-support-test-child=descendant",
            "--test-support-test-child=handle-inheritance",
        };
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] != nullptr && std::ranges::find(selectors, std::string_view(argv[index])) != std::end(selectors))
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
        options.enableManualTests = invocation.options.enableManualTests;
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
