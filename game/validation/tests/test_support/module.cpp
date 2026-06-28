#include "validation/tests/test_support/test_support_test.h"

#include "validation/tests/registry.h"

#include <string_view>

namespace
{
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

    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::TestSupportTestOptions options;
        options.enableChildProcessTests = invocation.options.enableTestSupportChildProcessTests;
        options.enableStressTests = invocation.options.enableStressTests;
        options.enableManualTests = invocation.options.enableManualUiTests;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runTestSupportTests(invocation.argc, invocation.argv, options);
    }

    const GameWIP::Validation::Tests::Registration registration({
        .name = "test_support",
        .order = 40,
        .run = run,
        .handlesChildArguments = handlesChildArguments,
    });
} // namespace
