/// @file module.cpp
/// @brief Registers the Assert correctness-test module with the validation runner.

#include "validation/tests/assert/assert_test.h"

#include "validation/tests/registry.h"

#include <string_view>

namespace
{
    /// @brief Claims the Assert child prefix for routing before validating its exact protocol suffix.
    /// @note An unknown reserved suffix currently reaches the Assert module and can fall through to its full suite.
    bool handlesChildArguments(int argc, char **argv)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] != nullptr && std::string_view(argv[index]).starts_with("--assert-test-child="))
            {
                return true;
            }
        }
        return false;
    }

    /// @brief Maps shared runner policy to Assert-specific options and executes the suite.
    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::AssertTestOptions options;
        options.enableChildCrashTests = invocation.options.enableChildCrashTests;
        options.enableStressTests = invocation.options.enableStressTests;
        options.enableAutomatedInteractiveTests = invocation.options.enableAutomatedInteractiveTests;
        options.enableManualUiTests = invocation.options.enableManualUiTests;
        options.verboseConsole = invocation.options.verboseConsole;
        options.stressThreadCount = invocation.options.stressThreadCount;
        options.stressIterations = invocation.options.assertStressIterations;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runAssertTests(invocation.argc, invocation.argv, options);
    }

    /// @brief Process-local static registration for deterministic Assert module discovery.
    const GameWIP::Validation::Tests::Registration registration({
        .name = "assert",
        .order = 60,
        .run = run,
        .handlesChildArguments = handlesChildArguments,
    });
} // namespace
