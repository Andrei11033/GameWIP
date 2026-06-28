#include "validation/tests/assert/assert_test.h"

#include "validation/tests/registry.h"

#include <string_view>

namespace
{
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

    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::AssertTestOptions options;
        options.enableChildCrashTests = invocation.options.enableChildCrashTests;
        options.enableStressTests = invocation.options.enableStressTests;
        options.enableAutomatedInteractiveTests = invocation.options.enableAutomatedInteractiveTests;
        options.enableManualUiTests = invocation.options.enableManualUiTests;
        options.stressThreadCount = invocation.options.stressThreadCount;
        options.stressIterations = invocation.options.assertStressIterations;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runAssertTests(invocation.argc, invocation.argv, options);
    }

    const GameWIP::Validation::Tests::Registration registration({
        .name = "assert",
        .order = 60,
        .run = run,
        .handlesChildArguments = handlesChildArguments,
    });
} // namespace
