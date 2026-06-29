#include "validation/tests/logger/logger_test.h"

#include "validation/tests/registry.h"

#include <string_view>

namespace
{
    bool handlesChildArguments(int argc, char **argv)
    {
        for (int index = 1; index < argc; ++index)
        {
            if (argv[index] != nullptr && std::string_view(argv[index]) == "--logger-test-child=fatal-terminate")
            {
                return true;
            }
        }
        return false;
    }

    int run(const GameWIP::Validation::Tests::ModuleInvocation &invocation)
    {
        GameWIP::Test::LoggerTestOptions options;
        options.enableStressTests = invocation.options.enableStressTests;
        options.enableChildCrashTests = invocation.options.enableChildCrashTests;
        options.enableManualUiTests = invocation.options.enableManualUiTests;
        options.enableLoggerPopupTest = invocation.options.enableLoggerPopupTest;
        options.verboseConsole = invocation.options.verboseConsole;
        options.stressThreadCount = invocation.options.stressThreadCount;
        options.stressIterationsPerThread = invocation.options.loggerStressIterationsPerThread;
        options.writeReport = invocation.options.writeReport;
        options.appendReport = invocation.appendReport;
        options.reportPath = invocation.options.reportPath;
        return GameWIP::Test::runLoggerTests(invocation.argc, invocation.argv, options);
    }

    const GameWIP::Validation::Tests::Registration registration({
        .name = "logger",
        .order = 50,
        .run = run,
        .handlesChildArguments = handlesChildArguments,
    });
} // namespace
