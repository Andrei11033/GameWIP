/// @file module.cpp
/// @brief Registers the Assert correctness-test module with the validation runner.

#include "validation/tests/assert/assert_test.h"

#include "validation/process_arguments.h"
#include "validation/tests/registry.h"

#include <algorithm>
#include <string_view>

namespace
{
    /// @brief Claims only exact Assert child protocols recognized by the suite.
    bool handlesChildArguments(int argc, char **argv)
    {
        constexpr std::string_view selectors[] = {
            "--assert-test-child=assert-failure",
            "--assert-test-child=debug-break",
            "--assert-test-child=unreachable",
            "--assert-test-child=interactive-abort",
            "--assert-test-child=interactive-break",
        };
        const auto arguments = GameWIP::Validation::processArguments(argc, argv);
        for (char *value : arguments.subspan(std::min<std::size_t>(1, arguments.size())))
        {
            if (value != nullptr && std::ranges::find(selectors, std::string_view(value)) != std::end(selectors))
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
        options.enableManualTests = invocation.options.enableManualTests;
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
